#include "media/FfmpegVideoDecoder.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
}

#include "media/DecodedVideoFrame.h"
#include "media/FfmpegHardwareDevice.h"
#include "media/ffmpeg/FfmpegFrameMetadata.h"
#include "media/ffmpeg/FfmpegVideoDecodeFallback.h"

namespace {
constexpr std::size_t maximumQueuedPackets = 64;
constexpr std::size_t maximumQueuedPacketBytes =
    4U * 1024U * 1024U;

struct FormatContextDeleter {
    void operator()(AVFormatContext *context) const {
        avformat_close_input(&context);
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext *context) const {
        avcodec_free_context(&context);
    }
};

struct CodecParametersDeleter {
    void operator()(AVCodecParameters *parameters) const {
        avcodec_parameters_free(&parameters);
    }
};

struct PacketDeleter {
    void operator()(AVPacket *packet) const {
        av_packet_free(&packet);
    }
};

struct FrameDeleter {
    void operator()(AVFrame *frame) const {
        av_frame_free(&frame);
    }
};

using FormatContextPtr =
    std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr =
    std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using CodecParametersPtr =
    std::unique_ptr<AVCodecParameters, CodecParametersDeleter>;
using PacketPtr =
    std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr =
    std::unique_ptr<AVFrame, FrameDeleter>;

QString ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0)
        return QStringLiteral("FFmpeg error %1").arg(code);
    return QString::fromUtf8(buffer);
}

struct HardwareDecodeState {
    enum AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    std::optional<std::uint64_t> graphicsDeviceGeneration;
    QString apiName;
    QString fallbackReason;
    bool *hardwareSelected = nullptr;
};

enum AVPixelFormat selectHardwareFormat(
        AVCodecContext *context,
        const enum AVPixelFormat *formats) {
    auto &state =
        *static_cast<HardwareDecodeState *>(context->opaque);
    for (const enum AVPixelFormat *format = formats;
            *format != AV_PIX_FMT_NONE;
            ++format) {
        if (*format == state.pixelFormat) {
            Q_ASSERT(state.hardwareSelected);
            *state.hardwareSelected = true;
            return *format;
        }
    }
    if (state.fallbackReason.isEmpty()) {
        state.fallbackReason = QStringLiteral(
            "The decoder did not offer the requested %1 format")
            .arg(state.apiName);
    }
    return avcodec_default_get_format(context, formats);
}

const AVCodecHWConfig *hardwareConfiguration(
        const AVCodec &decoder,
        enum AVHWDeviceType deviceType) {
    for (int index = 0;; ++index) {
        const AVCodecHWConfig *configuration =
            avcodec_get_hw_config(&decoder, index);
        if (!configuration)
            return nullptr;
        if (configuration->device_type == deviceType
                && configuration->pix_fmt
                    != AV_PIX_FMT_NONE
                && (configuration->methods
                    & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
            return configuration;
        }
    }
}

struct InterruptState {
    std::stop_token operationStopToken;
    std::stop_token demuxStopToken;
};

int interruptFfmpeg(void *opaque) {
    const auto &state =
        *static_cast<const InterruptState *>(opaque);
    return state.operationStopToken.stop_requested()
            || state.demuxStopToken.stop_requested()
        ? 1
        : 0;
}

enum class PacketTerminal {
    Open,
    EndOfStream,
    Failed,
    Cancelled,
};

struct PacketRead {
    PacketPtr packet;
    PacketTerminal terminal = PacketTerminal::Open;
    QString error;
};

class BoundedPacketChannel final {
public:
    bool push(
            PacketPtr packet,
            std::stop_token stopToken) {
        Q_ASSERT(packet);
        const std::size_t bytes =
            packet->size > 0
            ? static_cast<std::size_t>(packet->size)
            : 0;
        std::unique_lock lock(m_mutex);
        const bool ready = m_wake.wait(
            lock,
            stopToken,
            [this, bytes] {
                if (m_terminal != PacketTerminal::Open)
                    return true;
                const bool countAvailable =
                    m_packets.size() < maximumQueuedPackets;
                const bool bytesAvailable =
                    m_packets.empty()
                    || bytes <= maximumQueuedPacketBytes
                        - std::min(
                            m_queuedBytes,
                            maximumQueuedPacketBytes);
                return countAvailable && bytesAvailable;
            });
        if (!ready
                || m_terminal != PacketTerminal::Open) {
            return false;
        }
        m_queuedBytes += bytes;
        m_packets.push_back(
            {.packet = std::move(packet), .bytes = bytes});
        lock.unlock();
        m_wake.notify_all();
        return true;
    }

    PacketRead pop(std::stop_token stopToken) {
        std::unique_lock lock(m_mutex);
        const bool ready = m_wake.wait(
            lock,
            stopToken,
            [this] {
                return !m_packets.empty()
                    || m_terminal != PacketTerminal::Open;
            });
        if (!ready) {
            return {
                .terminal = PacketTerminal::Cancelled,
            };
        }
        if (!m_packets.empty()) {
            Entry entry = std::move(m_packets.front());
            m_packets.pop_front();
            m_queuedBytes -= entry.bytes;
            lock.unlock();
            m_wake.notify_all();
            return {.packet = std::move(entry.packet)};
        }
        return {
            .terminal = m_terminal,
            .error = m_error,
        };
    }

    void finish(
            PacketTerminal terminal,
            QString error = {}) {
        Q_ASSERT(terminal != PacketTerminal::Open);
        {
            std::lock_guard lock(m_mutex);
            if (m_terminal != PacketTerminal::Open)
                return;
            m_terminal = terminal;
            m_error = std::move(error);
        }
        m_wake.notify_all();
    }

private:
    struct Entry {
        PacketPtr packet;
        std::size_t bytes = 0;
    };

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Entry> m_packets;
    std::size_t m_queuedBytes = 0;
    PacketTerminal m_terminal = PacketTerminal::Open;
    QString m_error;
};

std::optional<std::int64_t> positiveDurationMicroseconds(
        std::int64_t value,
        AVRational timeBase) {
    if (value <= 0)
        return std::nullopt;
    const std::int64_t converted =
        av_rescale_q(value, timeBase, AV_TIME_BASE_Q);
    return converted > 0
        ? std::optional<std::int64_t>(converted)
        : std::nullopt;
}

VideoTimelineOrigin timelineOrigin(
        std::int64_t timestamp,
        AVRational timeBase) {
    return {
        .timestamp = timestamp,
        .timeBase = {
            .numerator = timeBase.num,
            .denominator = timeBase.den,
        },
    };
}

FfmpegVideoStreamDiagnostics streamDiagnostics(
        const AVFormatContext &formatContext,
        const AVStream &stream,
        const AVCodec &decoder,
        int streamIndex,
        const HardwareDecodeState &hardwareState,
        bool hardwareAccelerated) {
    FfmpegVideoStreamDiagnostics diagnostics;
    diagnostics.containerFormat =
        formatContext.iformat && formatContext.iformat->name
        ? QString::fromLatin1(formatContext.iformat->name)
        : QStringLiteral("unknown");
    diagnostics.decoderName = decoder.name
        ? QString::fromLatin1(decoder.name)
        : QStringLiteral("unknown");
    diagnostics.hardwareAccelerated = hardwareAccelerated;
    diagnostics.decodePath = hardwareAccelerated
        ? hardwareState.apiName
        : QStringLiteral("Software");
    if (!hardwareAccelerated) {
        diagnostics.hardwareFallbackReason =
            hardwareState.fallbackReason;
    }
    diagnostics.videoStreamIndex = streamIndex;
    diagnostics.seekable = formatContext.pb
        && (formatContext.pb->seekable
            & AVIO_SEEKABLE_NORMAL);
    if (formatContext.duration > 0
            && formatContext.duration != AV_NOPTS_VALUE) {
        diagnostics.durationMicroseconds =
            formatContext.duration;
    } else {
        diagnostics.durationMicroseconds =
            positiveDurationMicroseconds(
                stream.duration, stream.time_base);
    }
    if (formatContext.start_time != AV_NOPTS_VALUE) {
        diagnostics.timelineOrigin =
            timelineOrigin(
                formatContext.start_time,
                AV_TIME_BASE_Q);
    } else if (stream.start_time != AV_NOPTS_VALUE) {
        diagnostics.timelineOrigin =
            timelineOrigin(
                stream.start_time,
                stream.time_base);
    }
    const AVRational frameRate =
        av_guess_frame_rate(
            const_cast<AVFormatContext *>(&formatContext),
            const_cast<AVStream *>(&stream),
            nullptr);
    if (frameRate.num > 0 && frameRate.den > 0) {
        diagnostics.nominalFrameDurationMicroseconds =
            positiveDurationMicroseconds(
                1, av_inv_q(frameRate));
    }
    return diagnostics;
}

enum class DrainResult {
    NeedInput,
    Drained,
    Stopped,
    Cancelled,
    Failed,
};
}

bool VideoTimelineOrigin::isValid() const {
    return timeBase.isValid();
}

std::optional<std::int64_t>
VideoTimelineOrigin::microseconds() const {
    if (!isValid())
        return std::nullopt;
    return VideoFrameTiming{
        .pts = timestamp,
        .timeBase = timeBase,
    }.ptsMicroseconds();
}

bool VideoDecodeStart::isValid() const {
    return (!targetPositionMicroseconds
            || *targetPositionMicroseconds >= 0)
        && (!timelineOrigin
            || timelineOrigin->isValid())
        && (!targetPositionMicroseconds
            || timelineOrigin)
        && (!performDemuxSeek
            || targetPositionMicroseconds);
}

bool FfmpegVideoDecodeRequest::isValid() const {
    return !path.isEmpty()
        && firstFrameIdentity.isValid()
        && extraHardwareFrames >= 0
        && start.isValid();
}

bool FfmpegVideoStreamDiagnostics::isValid() const {
    return !containerFormat.isEmpty()
        && !decoderName.isEmpty()
        && !decodePath.isEmpty()
        && (hardwareAccelerated
            ? decodePath != QStringLiteral("Software")
                && hardwareFallbackReason.isEmpty()
            : decodePath == QStringLiteral("Software"))
        && videoStreamIndex >= 0
        && (!timelineOrigin
            || timelineOrigin->isValid())
        && (!durationMicroseconds
            || *durationMicroseconds > 0)
        && (!nominalFrameDurationMicroseconds
            || *nominalFrameDurationMicroseconds > 0);
}

bool FfmpegVideoDecodeResult::isSuccess() const {
    return diagnostics.isValid()
        && framesDecoded != 0
        && error.isEmpty()
        && !cancelled
        && (endOfStream || stopped);
}

bool FfmpegVideoDecodeResult::isCancelled() const {
    return cancelled
        && error.isEmpty()
        && !endOfStream
        && !stopped;
}

namespace {
FfmpegVideoDecodeResult decodeVideoFramesAttempt(
        const FfmpegVideoDecodeRequest &request,
        const FfmpegVideoFrameSink &sink,
        std::stop_token stopToken,
        bool *hardwareSelected) {
    Q_ASSERT(hardwareSelected);
    Q_ASSERT(sink);
    *hardwareSelected = false;

    FfmpegVideoDecodeResult result;
    const auto fail = [&result](QString message) {
        result.error = std::move(message);
        return result;
    };
    const auto cancel = [&result] {
        result.error.clear();
        result.cancelled = true;
        return result;
    };
    const auto endOfStream = [&result] {
        if (result.framesDecoded == 0) {
            result.error = QStringLiteral(
                "The selected video stream produced no decoded frame");
        } else {
            result.endOfStream = true;
        }
        return result;
    };

    if (stopToken.stop_requested())
        return cancel();
    if (!request.isValid())
        return fail(QStringLiteral("Video decode request is invalid"));

    // AVCodecContext::opaque points here, so this state must outlive codec
    // teardown as well as every get_format callback.
    HardwareDecodeState hardwareState;
    hardwareState.hardwareSelected = hardwareSelected;
    hardwareState.fallbackReason =
        request.hardwareDecode.unavailableReason;

    AVFormatContext *rawFormatContext =
        avformat_alloc_context();
    if (!rawFormatContext) {
        return fail(QStringLiteral(
            "Could not allocate the media container context"));
    }
    // Open/probe still runs on the decoder supervisor before AVFormatContext
    // is handed exclusively to the demux worker.
    InterruptState openingInterrupt{
        .operationStopToken = stopToken,
    };
    rawFormatContext->interrupt_callback = {
        interruptFfmpeg,
        &openingInterrupt,
    };
    const QByteArray encodedPath = request.path.toUtf8();
    int status = avformat_open_input(
        &rawFormatContext,
        encodedPath.constData(),
        nullptr,
        nullptr);
    if (status < 0) {
        if (rawFormatContext)
            avformat_close_input(&rawFormatContext);
        if (stopToken.stop_requested())
            return cancel();
        return fail(QStringLiteral(
            "Could not open media: %1")
            .arg(ffmpegError(status)));
    }
    FormatContextPtr formatContext(rawFormatContext);

    status = avformat_find_stream_info(
        formatContext.get(), nullptr);
    if (status < 0) {
        if (stopToken.stop_requested())
            return cancel();
        return fail(QStringLiteral(
            "Could not discover media streams: %1")
            .arg(ffmpegError(status)));
    }

    const AVCodec *decoder = nullptr;
    const int streamIndex = av_find_best_stream(
        formatContext.get(),
        AVMEDIA_TYPE_VIDEO,
        -1,
        -1,
        &decoder,
        0);
    if (stopToken.stop_requested())
        return cancel();
    if (streamIndex < 0 || !decoder) {
        return fail(QStringLiteral(
            "Could not select a video stream: %1")
            .arg(ffmpegError(streamIndex)));
    }
    const AVStream &stream =
        *formatContext->streams[streamIndex];
    const AVRational streamTimeBase = stream.time_base;
    AVRational streamAspectRatio =
        stream.sample_aspect_ratio;
    if ((streamAspectRatio.num <= 0
            || streamAspectRatio.den <= 0)
            && stream.codecpar->sample_aspect_ratio.num > 0
            && stream.codecpar->sample_aspect_ratio.den > 0) {
        streamAspectRatio =
            stream.codecpar->sample_aspect_ratio;
    }

    if (request.start.performDemuxSeek) {
        if (!formatContext->pb
                || !(formatContext->pb->seekable
                    & AVIO_SEEKABLE_NORMAL)) {
            return fail(QStringLiteral(
                "The selected media source is not seekable"));
        }

        for (unsigned int index = 0;
                index < formatContext->nb_streams;
                ++index) {
            formatContext->streams[index]->discard =
                static_cast<int>(index) == streamIndex
                ? AVDISCARD_DEFAULT
                : AVDISCARD_ALL;
        }

        const VideoTimelineOrigin &origin =
            *request.start.timelineOrigin;
        const AVRational originTimeBase{
            origin.timeBase.numerator,
            origin.timeBase.denominator,
        };
        const std::int64_t originInStreamTimeBase =
            av_rescale_q(
                origin.timestamp,
                originTimeBase,
                streamTimeBase);
        const std::int64_t targetTimestamp =
            av_add_stable(
                streamTimeBase,
                originInStreamTimeBase,
                AV_TIME_BASE_Q,
                *request.start
                    .targetPositionMicroseconds);
        status = avformat_seek_file(
            formatContext.get(),
            streamIndex,
            std::numeric_limits<std::int64_t>::min(),
            targetTimestamp,
            targetTimestamp,
            0);
        if (status < 0) {
            if (stopToken.stop_requested())
                return cancel();
            return fail(QStringLiteral(
                "Could not seek video to %1 ms: %2")
                .arg(
                    *request.start
                        .targetPositionMicroseconds
                        / 1'000)
                .arg(ffmpegError(status)));
        }
    }

    CodecParametersPtr streamParameters(
        avcodec_parameters_alloc());
    if (!streamParameters) {
        return fail(QStringLiteral(
            "Could not retain video stream parameters"));
    }
    status = avcodec_parameters_copy(
        streamParameters.get(), stream.codecpar);
    if (status < 0) {
        return fail(QStringLiteral(
            "Could not retain video stream parameters: %1")
            .arg(ffmpegError(status)));
    }

    CodecContextPtr codecContext(
        avcodec_alloc_context3(decoder));
    if (!codecContext) {
        return fail(QStringLiteral(
            "Could not allocate the video decoder"));
    }
    status = avcodec_parameters_to_context(
        codecContext.get(), stream.codecpar);
    if (status < 0) {
        return fail(QStringLiteral(
            "Could not configure the video decoder: %1")
            .arg(ffmpegError(status)));
    }
    codecContext->pkt_timebase = streamTimeBase;

    if (request.hardwareDecode.device) {
        AVBufferRef *deviceReference =
            request.hardwareDecode.device
                ->referenceDeviceContext();
        if (!deviceReference) {
            hardwareState.fallbackReason =
                QStringLiteral(
                    "Could not retain the %1 device")
                    .arg(
                        request.hardwareDecode.device
                            ->apiName());
        } else {
            const auto *const deviceContext =
                reinterpret_cast<const AVHWDeviceContext *>(
                    deviceReference->data);
            const AVCodecHWConfig *configuration =
                deviceContext
                ? hardwareConfiguration(
                    *decoder, deviceContext->type)
                : nullptr;
            if (!configuration) {
                hardwareState.fallbackReason =
                    QStringLiteral(
                        "Decoder %1 does not support %2")
                        .arg(
                            QString::fromLatin1(decoder->name),
                            request.hardwareDecode.device
                                ->apiName());
                av_buffer_unref(&deviceReference);
            } else {
                hardwareState.pixelFormat =
                    configuration->pix_fmt;
                hardwareState.graphicsDeviceGeneration =
                    request.hardwareDecode.device
                        ->graphicsDeviceGeneration();
                hardwareState.apiName =
                    request.hardwareDecode.device
                        ->apiName();
                codecContext->opaque = &hardwareState;
                codecContext->get_format =
                    selectHardwareFormat;
                codecContext->hw_device_ctx =
                    deviceReference;
                codecContext->extra_hw_frames =
                    request.extraHardwareFrames;
            }
        }
    }

    status = avcodec_open2(
        codecContext.get(), decoder, nullptr);
    if (status < 0) {
        if (stopToken.stop_requested())
            return cancel();
        return fail(QStringLiteral(
            "Could not open video decoder %1: %2")
            .arg(
                QString::fromLatin1(decoder->name),
                ffmpegError(status)));
    }

    FramePtr frame(av_frame_alloc());
    if (!frame) {
        return fail(QStringLiteral(
            "Could not allocate FFmpeg frame storage"));
    }

    const FfmpegVideoStreamDiagnostics softwareDiagnostics =
        streamDiagnostics(
            *formatContext,
            stream,
            *decoder,
            streamIndex,
            hardwareState,
            false);
    const FfmpegVideoStreamDiagnostics hardwareDiagnostics =
        streamDiagnostics(
            *formatContext,
            stream,
            *decoder,
            streamIndex,
            hardwareState,
            true);
    std::optional<VideoTimelineOrigin> resolvedTimelineOrigin =
        request.start.timelineOrigin
        ? request.start.timelineOrigin
        : softwareDiagnostics.timelineOrigin;

    BoundedPacketChannel packets;
    std::jthread demuxWorker(
        [&](std::stop_token demuxStopToken) {
            // openingInterrupt was declared before formatContext and
            // therefore also outlives close_input during teardown.
            openingInterrupt.demuxStopToken =
                demuxStopToken;

            while (!stopToken.stop_requested()
                    && !demuxStopToken.stop_requested()) {
                PacketPtr packet(av_packet_alloc());
                if (!packet) {
                    packets.finish(
                        PacketTerminal::Failed,
                        QStringLiteral(
                            "Could not allocate FFmpeg packet storage"));
                    return;
                }
                const int readStatus =
                    av_read_frame(
                        formatContext.get(), packet.get());
                if (readStatus < 0) {
                    if (stopToken.stop_requested()
                            || demuxStopToken.stop_requested()) {
                        packets.finish(PacketTerminal::Cancelled);
                    } else if (readStatus == AVERROR_EOF) {
                        packets.finish(
                            PacketTerminal::EndOfStream);
                    } else {
                        packets.finish(
                            PacketTerminal::Failed,
                            QStringLiteral(
                                "Media read failed: %1")
                                .arg(ffmpegError(readStatus)));
                    }
                    return;
                }
                if (packet->stream_index != streamIndex)
                    continue;
                if (!packets.push(
                        std::move(packet),
                        demuxStopToken)) {
                    packets.finish(PacketTerminal::Cancelled);
                    return;
                }
            }
            packets.finish(PacketTerminal::Cancelled);
        });

    std::uint64_t nextFrameId =
        request.firstFrameIdentity.frameId;
    const auto drainDecoder =
        [&](bool flushing) -> DrainResult {
            while (true) {
                if (stopToken.stop_requested())
                    return DrainResult::Cancelled;
                const int receiveStatus =
                    avcodec_receive_frame(
                        codecContext.get(), frame.get());
                if (receiveStatus == AVERROR(EAGAIN)) {
                    if (flushing) {
                        result.error = QStringLiteral(
                            "Video decoder requested input after "
                            "the end-of-stream flush");
                        return DrainResult::Failed;
                    }
                    return DrainResult::NeedInput;
                }
                if (receiveStatus == AVERROR_EOF)
                    return DrainResult::Drained;
                if (receiveStatus < 0) {
                    if (stopToken.stop_requested())
                        return DrainResult::Cancelled;
                    result.error = QStringLiteral(
                        "Video decoding failed: %1")
                        .arg(ffmpegError(receiveStatus));
                    return DrainResult::Failed;
                }

                if (!mergeStreamVideoMetadata(
                        *frame, *streamParameters)) {
                    av_frame_unref(frame.get());
                    result.error = QStringLiteral(
                        "Could not retain stream-level video metadata");
                    return DrainResult::Failed;
                }
                if ((frame->sample_aspect_ratio.num <= 0
                        || frame->sample_aspect_ratio.den <= 0)
                        && streamAspectRatio.num > 0
                        && streamAspectRatio.den > 0) {
                    frame->sample_aspect_ratio =
                        streamAspectRatio;
                }
                if (nextFrameId == 0) {
                    av_frame_unref(frame.get());
                    result.error = QStringLiteral(
                        "Decoded frame identity overflowed");
                    return DrainResult::Failed;
                }
                const VideoFrameIdentity identity{
                    .playbackGeneration =
                        request.firstFrameIdentity
                            .playbackGeneration,
                    .decoderRevision =
                        request.firstFrameIdentity
                            .decoderRevision,
                    .frameId = nextFrameId++,
                };
                QString frameError;
                const bool hardwareFrame =
                    frame->format == hardwareState.pixelFormat
                    && frame->hw_frames_ctx;
                std::shared_ptr<const DecodedVideoFrame> decoded =
                    DecodedVideoFrame::clone(
                        *frame,
                        identity,
                        {
                            streamTimeBase.num,
                            streamTimeBase.den,
                        },
                        hardwareFrame
                            ? hardwareState
                                .graphicsDeviceGeneration
                            : std::nullopt,
                        &frameError);
                av_frame_unref(frame.get());
                if (!decoded) {
                    result.error = frameError;
                    return DrainResult::Failed;
                }

                FfmpegVideoStreamDiagnostics diagnostics =
                    hardwareFrame
                    ? hardwareDiagnostics
                    : softwareDiagnostics;
                if (!resolvedTimelineOrigin
                        && decoded->timing().pts) {
                    resolvedTimelineOrigin =
                        VideoTimelineOrigin{
                            .timestamp =
                                *decoded->timing().pts,
                            .timeBase =
                                decoded->timing().timeBase,
                        };
                }
                diagnostics.timelineOrigin =
                    resolvedTimelineOrigin;
                if (!hardwareFrame) {
                    diagnostics.hardwareFallbackReason =
                        hardwareState.fallbackReason;
                }
                if (!result.diagnostics.isValid())
                    result.diagnostics = diagnostics;
                ++result.framesDecoded;
                if (!sink(
                        std::move(decoded),
                        diagnostics)) {
                    if (stopToken.stop_requested())
                        return DrainResult::Cancelled;
                    return DrainResult::Stopped;
                }
            }
        };

    while (!stopToken.stop_requested()) {
        PacketRead input = packets.pop(stopToken);
        if (stopToken.stop_requested()
                || input.terminal
                    == PacketTerminal::Cancelled) {
            return cancel();
        }
        if (input.terminal == PacketTerminal::Failed) {
            return fail(input.error.isEmpty()
                ? QStringLiteral("Media demuxing failed")
                : input.error);
        }
        if (input.terminal
                == PacketTerminal::EndOfStream) {
            while (true) {
                status = avcodec_send_packet(
                    codecContext.get(), nullptr);
                if (status != AVERROR(EAGAIN))
                    break;
                const DrainResult progress =
                    drainDecoder(false);
                if (progress == DrainResult::Stopped) {
                    result.stopped = true;
                    return result;
                }
                if (progress == DrainResult::Cancelled)
                    return cancel();
                if (progress == DrainResult::Failed)
                    return result;
                if (progress == DrainResult::NeedInput) {
                    return fail(QStringLiteral(
                        "Video decoder made no progress "
                        "while entering flush mode"));
                }
                if (progress == DrainResult::Drained) {
                    return endOfStream();
                }
            }
            if (status < 0 && status != AVERROR_EOF) {
                return fail(QStringLiteral(
                    "Could not flush video decoder: %1")
                    .arg(ffmpegError(status)));
            }
            if (status == AVERROR_EOF) {
                return endOfStream();
            }
            const DrainResult drained =
                drainDecoder(true);
            if (drained == DrainResult::Cancelled)
                return cancel();
            if (drained == DrainResult::Stopped) {
                result.stopped = true;
                return result;
            }
            if (drained == DrainResult::Failed)
                return result;
            if (drained != DrainResult::Drained) {
                return fail(QStringLiteral(
                    "Video decoder did not reach end of stream"));
            }
            return endOfStream();
        }

        Q_ASSERT(input.packet);
        while (true) {
            status = avcodec_send_packet(
                codecContext.get(), input.packet.get());
            if (status != AVERROR(EAGAIN))
                break;
            const std::uint64_t before =
                result.framesDecoded;
            const DrainResult progress =
                drainDecoder(false);
            if (progress == DrainResult::Stopped) {
                result.stopped = true;
                return result;
            }
            if (progress == DrainResult::Cancelled)
                return cancel();
            if (progress == DrainResult::Failed)
                return result;
            if (progress != DrainResult::Drained
                    && result.framesDecoded == before) {
                return fail(QStringLiteral(
                    "Video decoder returned EAGAIN from both "
                    "send and receive without progress"));
            }
            if (progress == DrainResult::Drained) {
                return endOfStream();
            }
        }
        if (status < 0) {
            if (stopToken.stop_requested())
                return cancel();
            return fail(QStringLiteral(
                "Could not submit video packet: %1")
                .arg(ffmpegError(status)));
        }

        const DrainResult drained =
            drainDecoder(false);
        if (drained == DrainResult::Stopped) {
            result.stopped = true;
            return result;
        }
        if (drained == DrainResult::Cancelled)
            return cancel();
        if (drained == DrainResult::Failed)
            return result;
        if (drained == DrainResult::Drained) {
            return endOfStream();
        }
    }
    return cancel();
}
}

FfmpegVideoDecodeResult decodeVideoFrames(
        const FfmpegVideoDecodeRequest &request,
        const FfmpegVideoFrameSink &sink,
        std::stop_token stopToken) {
    return decodeVideoFramesWithFallback(
        request.hardwareDecode,
        [&](const VideoHardwareDecodeCapability &capability,
            bool &hardwareSelected) {
            FfmpegVideoDecodeRequest attemptRequest =
                request;
            attemptRequest.hardwareDecode = capability;
            return decodeVideoFramesAttempt(
                attemptRequest,
                sink,
                stopToken,
                &hardwareSelected);
        });
}
