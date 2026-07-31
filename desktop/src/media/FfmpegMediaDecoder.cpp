#include "media/FfmpegMediaDecoder.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include "diagnostics/LogCategories.h"
#include "media/DecodedVideoFrame.h"
#include "media/ffmpeg/FfmpegPacketRouter.h"
#include "media/ffmpeg/FfmpegStreamMetadata.h"
#include "media/ffmpeg/FfmpegVideoPacketDecoder.h"

namespace {
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

struct FrameDeleter {
    void operator()(AVFrame *frame) const {
        av_frame_free(&frame);
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext *context) const {
        swr_free(&context);
    }
};

using FormatContextPtr =
    std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr =
    std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using CodecParametersPtr =
    std::unique_ptr<AVCodecParameters, CodecParametersDeleter>;
using PacketPtr = FfmpegAvPacketPtr;
using FramePtr =
    std::unique_ptr<AVFrame, FrameDeleter>;
using SwrContextPtr =
    std::unique_ptr<SwrContext, SwrContextDeleter>;

QString ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0)
        return QStringLiteral("FFmpeg error %1").arg(code);
    return QString::fromUtf8(buffer);
}

struct InterruptState {
    std::stop_token stopToken;
};

int interruptFfmpeg(void *opaque) {
    return static_cast<const InterruptState *>(opaque)
            ->stopToken.stop_requested()
        ? 1
        : 0;
}

std::optional<std::int64_t> positiveDurationMicroseconds(
        std::int64_t value,
        AVRational timeBase) {
    if (value <= 0 || value == AV_NOPTS_VALUE)
        return std::nullopt;
    const std::int64_t converted =
        av_rescale_q(value, timeBase, AV_TIME_BASE_Q);
    return converted > 0
        ? std::optional<std::int64_t>(converted)
        : std::nullopt;
}

std::optional<std::int64_t> normalizedTimestampMicroseconds(
        std::int64_t timestamp,
        AVRational timeBase,
        const std::optional<VideoTimelineOrigin> &origin) {
    if (timestamp == AV_NOPTS_VALUE)
        return std::nullopt;
    const std::int64_t absolute =
        av_rescale_q(timestamp, timeBase, AV_TIME_BASE_Q);
    const std::optional<std::int64_t> originMicroseconds =
        origin ? origin->microseconds() : std::nullopt;
    return originMicroseconds
        ? checkedTimestampSubtract(absolute, *originMicroseconds)
        : absolute;
}

FfmpegVideoStreamDiagnostics videoDiagnostics(
        const AVFormatContext &formatContext,
        const AVStream &stream,
        const AVCodec &decoder,
        int streamIndex,
        const FfmpegVideoDecodeRequest &request,
        std::optional<VideoTimelineOrigin> origin) {
    FfmpegVideoStreamDiagnostics diagnostics{
        .containerFormat =
            formatContext.iformat && formatContext.iformat->name
            ? QString::fromLatin1(formatContext.iformat->name)
            : QStringLiteral("unknown"),
        .decoderName = decoder.name
            ? QString::fromLatin1(decoder.name)
            : QStringLiteral("unknown"),
        .decodePath = QStringLiteral("Software"),
        .hardwareFallbackReason =
            request.hardwareDecode.unavailableReason,
        .videoStreamIndex = streamIndex,
        .hardwareAccelerated = false,
        .seekable = formatContext.pb
            && (formatContext.pb->seekable & AVIO_SEEKABLE_NORMAL),
        .timelineOrigin = std::move(origin),
    };
    diagnostics.durationMicroseconds =
        ffmpegProvisionalDurationMicroseconds(
            formatContext, stream);
    const AVRational frameRate = av_guess_frame_rate(
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

struct WorkerStatus {
    QString error;
    bool endOfStream = false;
    bool stopped = false;
    bool cancelled = false;
};

struct AudioWorkerStatus : WorkerStatus {
    std::optional<FfmpegAudioStreamDiagnostics> diagnostics;
    std::uint64_t decodedFrames = 0;
    std::uint64_t outputFrames = 0;
    std::optional<std::int64_t> observedEndMicroseconds;
};

void stopSibling(
        FfmpegPacketRouter &router,
        std::stop_source &operationStop) {
    operationStop.request_stop();
    router.finish(FfmpegPacketRouterTerminal::Cancelled);
}

FfmpegVideoDecodeResult decodeVideoPackets(
        const FfmpegMediaDecodeRequest &request,
        const AVCodecParameters &parameters,
        AVRational streamTimeBase,
        AVRational streamAspectRatio,
        const AVCodec &decoder,
        FfmpegVideoStreamDiagnostics diagnostics,
        FfmpegPacketRouter &router,
        const FfmpegVideoFrameSink &sink,
        std::stop_source &operationStop) {
    const std::stop_token stopToken = operationStop.get_token();
    bool hardwareSelected = false;
    FfmpegVideoDecodeResult result = decodeFfmpegVideoPackets(
        request.video,
        decoder,
        parameters,
        {streamTimeBase.num, streamTimeBase.den},
        {streamAspectRatio.num, streamAspectRatio.den},
        std::move(diagnostics),
        [&router](std::stop_token packetStopToken) {
            FfmpegRoutedPacket input = router.pop(
                FfmpegPacketStream::Video, packetStopToken);
            switch (input.terminal) {
            case FfmpegPacketRouterTerminal::Open:
                return FfmpegVideoPacketRead{
                    .packet = std::move(input.packet),
                };
            case FfmpegPacketRouterTerminal::EndOfStream:
                return FfmpegVideoPacketRead{
                    .terminal =
                        FfmpegVideoPacketTerminal::EndOfStream,
                };
            case FfmpegPacketRouterTerminal::Failed:
                return FfmpegVideoPacketRead{
                    .terminal =
                        FfmpegVideoPacketTerminal::Failed,
                    .error = std::move(input.error),
                };
            case FfmpegPacketRouterTerminal::Cancelled:
                return FfmpegVideoPacketRead{
                    .terminal =
                        FfmpegVideoPacketTerminal::Cancelled,
                };
            }
            Q_UNREACHABLE_RETURN(FfmpegVideoPacketRead{});
        },
        sink,
        stopToken,
        &hardwareSelected);
    if (!result.error.isEmpty() || result.stopped)
        stopSibling(router, operationStop);
    return result;
}

enum class AudioConvertResult {
    Converted,
    Stopped,
    Cancelled,
    Failed,
};

class AudioConverter final {
public:
    AudioConverter(
            const FfmpegMediaDecodeRequest &request,
            const AVCodecParameters &parameters,
            AVRational streamTimeBase,
            int streamIndex,
            const AVCodec &decoder,
            std::optional<VideoTimelineOrigin> origin,
            const FfmpegPcmAudioSink &sink,
            std::stop_token stopToken)
        : m_request(request),
          m_streamTimeBase(streamTimeBase),
          m_origin(std::move(origin)),
          m_sink(sink),
          m_stopToken(stopToken) {
        m_diagnostics = {
            .decoderName = decoder.name
                ? QString::fromLatin1(decoder.name)
                : QStringLiteral("unknown"),
            .audioStreamIndex = streamIndex,
            .sourceSampleRate = parameters.sample_rate,
            .sourceChannelCount =
                parameters.ch_layout.nb_channels,
            .outputFormat = request.audioOutput,
            .timelineOrigin = m_origin,
        };
    }

    AudioConvertResult convert(
            const AVFrame &frame,
            QString &error) {
        if (!ensureContext(frame, error))
            return AudioConvertResult::Failed;
        const std::optional<std::int64_t> frameTime =
            normalizedTimestampMicroseconds(
                frame.best_effort_timestamp,
                m_streamTimeBase,
                m_origin);
        if (!m_anchorMediaMicroseconds) {
            if (!frameTime) {
                error = QStringLiteral(
                    "The first decoded audio frame has no timestamp");
                return AudioConvertResult::Failed;
            }
            m_anchorMediaMicroseconds = *frameTime;
        }
        if (frameTime && m_expectedNextMediaMicroseconds) {
            const std::int64_t tolerance = std::max<std::int64_t>(
                1'000,
                av_rescale_q(
                    2,
                    {1, frame.sample_rate},
                    AV_TIME_BASE_Q));
            const std::optional<std::int64_t> difference =
                checkedTimestampSubtract(
                    *frameTime,
                    *m_expectedNextMediaMicroseconds);
            if (!difference
                    || *difference < -tolerance
                    || *difference > tolerance) {
                error = QStringLiteral(
                    "Decoded audio timestamp discontinuity is not "
                    "supported yet (expected %1 us, got %2 us)")
                    .arg(*m_expectedNextMediaMicroseconds)
                    .arg(*frameTime);
                return AudioConvertResult::Failed;
            }
        }
        const std::int64_t effectiveFrameTime = frameTime
            ? *frameTime
            : *m_expectedNextMediaMicroseconds;
        m_expectedNextMediaMicroseconds = checkedTimestampAdd(
            effectiveFrameTime,
            av_rescale_q(
                frame.nb_samples,
                {1, frame.sample_rate},
                AV_TIME_BASE_Q));
        if (!m_expectedNextMediaMicroseconds) {
            error = QStringLiteral(
                "Decoded audio timestamp range is not representable");
            return AudioConvertResult::Failed;
        }

        const std::int64_t delay =
            swr_get_delay(m_swr.get(), frame.sample_rate);
        const std::int64_t capacity = av_rescale_rnd(
            delay + frame.nb_samples,
            m_request.audioOutput.sampleRate,
            frame.sample_rate,
            AV_ROUND_UP);
        if (capacity <= 0)
            return AudioConvertResult::Converted;
        std::vector<float> samples(
            static_cast<std::size_t>(capacity)
                * m_request.audioOutput.channelCount);
        std::uint8_t *output[] = {
            reinterpret_cast<std::uint8_t *>(samples.data()),
        };
        const int produced = swr_convert(
            m_swr.get(),
            output,
            static_cast<int>(capacity),
            const_cast<const std::uint8_t **>(frame.extended_data),
            frame.nb_samples);
        if (produced < 0) {
            error = QStringLiteral(
                "Audio conversion failed: %1")
                .arg(ffmpegError(produced));
            return AudioConvertResult::Failed;
        }
        samples.resize(
            static_cast<std::size_t>(produced)
                * m_request.audioOutput.channelCount);
        return publish(std::move(samples), produced, error);
    }

    AudioConvertResult flush(QString &error) {
        if (!m_swr)
            return AudioConvertResult::Converted;
        while (true) {
            const std::int64_t delay = swr_get_delay(
                m_swr.get(), m_inputSampleRate);
            if (delay <= 0)
                return AudioConvertResult::Converted;
            const std::int64_t capacity = av_rescale_rnd(
                delay,
                m_request.audioOutput.sampleRate,
                m_inputSampleRate,
                AV_ROUND_UP);
            if (capacity <= 0)
                return AudioConvertResult::Converted;
            std::vector<float> samples(
                static_cast<std::size_t>(capacity)
                    * m_request.audioOutput.channelCount);
            std::uint8_t *output[] = {
                reinterpret_cast<std::uint8_t *>(samples.data()),
            };
            const int produced = swr_convert(
                m_swr.get(), output, static_cast<int>(capacity),
                nullptr, 0);
            if (produced < 0) {
                error = QStringLiteral(
                    "Could not flush audio converter: %1")
                    .arg(ffmpegError(produced));
                return AudioConvertResult::Failed;
            }
            if (produced == 0)
                return AudioConvertResult::Converted;
            samples.resize(
                static_cast<std::size_t>(produced)
                    * m_request.audioOutput.channelCount);
            const AudioConvertResult published = publish(
                std::move(samples), produced, error);
            if (published != AudioConvertResult::Converted)
                return published;
        }
    }

    const FfmpegAudioStreamDiagnostics &diagnostics() const {
        return m_diagnostics;
    }

    std::uint64_t outputFrames() const {
        return m_streamFrameIndex;
    }

    std::optional<std::int64_t> observedEndMicroseconds() const {
        return m_observedEndMicroseconds;
    }

private:
    bool ensureContext(
            const AVFrame &frame,
            QString &error) {
        if (m_swr) {
            if (frame.sample_rate != m_inputSampleRate
                    || frame.format != m_inputSampleFormat
                    || av_channel_layout_compare(
                        &frame.ch_layout,
                        &m_inputLayout) != 0) {
                error = QStringLiteral(
                    "The selected audio stream changed format");
                return false;
            }
            return true;
        }
        if (frame.sample_rate <= 0
                || frame.ch_layout.nb_channels <= 0
                || frame.format == AV_SAMPLE_FMT_NONE) {
            error = QStringLiteral(
                "Decoded audio format is incomplete");
            return false;
        }
        AVChannelLayout outputLayout =
            AV_CHANNEL_LAYOUT_STEREO;
        SwrContext *raw = nullptr;
        const int status = swr_alloc_set_opts2(
            &raw,
            &outputLayout,
            AV_SAMPLE_FMT_FLT,
            m_request.audioOutput.sampleRate,
            &frame.ch_layout,
            static_cast<AVSampleFormat>(frame.format),
            frame.sample_rate,
            0,
            nullptr);
        if (status < 0 || !raw) {
            if (raw)
                swr_free(&raw);
            error = QStringLiteral(
                "Could not configure audio conversion: %1")
                .arg(ffmpegError(status));
            return false;
        }
        m_swr.reset(raw);
        if (swr_init(m_swr.get()) < 0) {
            error = QStringLiteral(
                "Could not initialize audio conversion");
            m_swr.reset();
            return false;
        }
        m_inputSampleRate = frame.sample_rate;
        m_inputSampleFormat = frame.format;
        if (av_channel_layout_copy(
                &m_inputLayout, &frame.ch_layout) < 0) {
            error = QStringLiteral(
                "Could not retain the decoded channel layout");
            m_swr.reset();
            return false;
        }
        m_inputLayoutInitialized = true;
        m_diagnostics.sourceSampleRate = frame.sample_rate;
        m_diagnostics.sourceChannelCount =
            frame.ch_layout.nb_channels;
        return true;
    }

    AudioConvertResult publish(
            std::vector<float> samples,
            int producedFrames,
            QString &error) {
        if (producedFrames == 0)
            return AudioConvertResult::Converted;
        Q_ASSERT(m_anchorMediaMicroseconds);
        if (m_convertedFrames
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            error = QStringLiteral(
                "Converted audio frame index is not representable");
            return AudioConvertResult::Failed;
        }
        const std::optional<std::int64_t> blockStart =
            checkedTimestampAdd(
                *m_anchorMediaMicroseconds,
                av_rescale_q(
                static_cast<std::int64_t>(m_convertedFrames),
                {1, m_request.audioOutput.sampleRate},
                AV_TIME_BASE_Q));
        if (!blockStart) {
            error = QStringLiteral(
                "Converted audio timestamp range is not representable");
            return AudioConvertResult::Failed;
        }
        m_convertedFrames +=
            static_cast<std::uint64_t>(producedFrames);

        std::size_t trimFrames = 0;
        if (m_request.video.start.targetPositionMicroseconds) {
            const std::int64_t target = *m_request.video.start
                .targetPositionMicroseconds;
            if (*blockStart < target) {
                const std::optional<std::int64_t> difference =
                    checkedTimestampSubtract(target, *blockStart);
                if (!difference) {
                    error = QStringLiteral(
                        "Audio seek trim range is not representable");
                    return AudioConvertResult::Failed;
                }
                const std::int64_t required = av_rescale_q_rnd(
                    *difference,
                    AV_TIME_BASE_Q,
                    {1, m_request.audioOutput.sampleRate},
                    AV_ROUND_UP);
                trimFrames = std::min<std::size_t>(
                    producedFrames,
                    static_cast<std::size_t>(
                        std::max<std::int64_t>(0, required)));
            }
        }
        const std::size_t keptFrames =
            static_cast<std::size_t>(producedFrames)
            - trimFrames;
        if (keptFrames == 0)
            return AudioConvertResult::Converted;
        const std::size_t channels = static_cast<std::size_t>(
            m_request.audioOutput.channelCount);
        if (trimFrames != 0) {
            samples.erase(
                samples.begin(),
                samples.begin()
                    + static_cast<std::ptrdiff_t>(
                        trimFrames * channels));
        }
        const std::optional<std::int64_t> mediaStart =
            checkedTimestampAdd(
                *blockStart,
                av_rescale_q(
                static_cast<std::int64_t>(trimFrames),
                {1, m_request.audioOutput.sampleRate},
                AV_TIME_BASE_Q));
        if (!mediaStart) {
            error = QStringLiteral(
                "Trimmed audio timestamp range is not representable");
            return AudioConvertResult::Failed;
        }
        m_observedEndMicroseconds = checkedTimestampAdd(
            *mediaStart,
            av_rescale_q(
                static_cast<std::int64_t>(keptFrames),
                {1, m_request.audioOutput.sampleRate},
                AV_TIME_BASE_Q));
        if (!m_observedEndMicroseconds) {
            error = QStringLiteral(
                "Audio endpoint is not representable");
            return AudioConvertResult::Failed;
        }
        PcmAudioBlock block{
            .playbackGeneration = requestGeneration(),
            .streamFrameIndex = m_streamFrameIndex,
            .mediaStartMicroseconds = *mediaStart,
            .format = m_request.audioOutput,
            .samples = std::move(samples),
        };
        if (!m_sink(
                std::move(block),
                m_diagnostics,
                m_stopToken)) {
            return m_stopToken.stop_requested()
                ? AudioConvertResult::Cancelled
                : AudioConvertResult::Stopped;
        }
        m_streamFrameIndex += keptFrames;
        return AudioConvertResult::Converted;
    }

    std::uint64_t requestGeneration() const {
        return m_request.video.firstFrameIdentity
            .playbackGeneration;
    }

public:
    ~AudioConverter() {
        if (m_inputLayoutInitialized)
            av_channel_layout_uninit(&m_inputLayout);
    }

private:
    const FfmpegMediaDecodeRequest &m_request;
    AVRational m_streamTimeBase;
    std::optional<VideoTimelineOrigin> m_origin;
    const FfmpegPcmAudioSink &m_sink;
    std::stop_token m_stopToken;
    FfmpegAudioStreamDiagnostics m_diagnostics;
    SwrContextPtr m_swr;
    AVChannelLayout m_inputLayout{};
    bool m_inputLayoutInitialized = false;
    int m_inputSampleRate = 0;
    int m_inputSampleFormat = AV_SAMPLE_FMT_NONE;
    std::optional<std::int64_t> m_anchorMediaMicroseconds;
    std::optional<std::int64_t> m_expectedNextMediaMicroseconds;
    std::uint64_t m_convertedFrames = 0;
    std::uint64_t m_streamFrameIndex = 0;
    std::optional<std::int64_t> m_observedEndMicroseconds;
};

AudioWorkerStatus decodeAudioPackets(
        const FfmpegMediaDecodeRequest &request,
        const AVCodecParameters &parameters,
        AVRational streamTimeBase,
        int streamIndex,
        const AVCodec &decoder,
        std::optional<VideoTimelineOrigin> origin,
        FfmpegPacketRouter &router,
        const FfmpegPcmAudioSink &sink,
        std::stop_source &operationStop) {
    AudioWorkerStatus result;
    const std::stop_token stopToken = operationStop.get_token();
    const auto fail = [&](QString error) {
        result.error = std::move(error);
        stopSibling(router, operationStop);
        return result;
    };

    CodecContextPtr codecContext(
        avcodec_alloc_context3(&decoder));
    if (!codecContext)
        return fail(QStringLiteral("Could not allocate audio decoder"));
    int status = avcodec_parameters_to_context(
        codecContext.get(), &parameters);
    if (status < 0) {
        return fail(QStringLiteral(
            "Could not configure audio decoder: %1")
            .arg(ffmpegError(status)));
    }
    codecContext->pkt_timebase = streamTimeBase;
    status = avcodec_open2(codecContext.get(), &decoder, nullptr);
    if (status < 0) {
        return fail(QStringLiteral(
            "Could not open audio decoder %1: %2")
            .arg(
                QString::fromLatin1(decoder.name),
                ffmpegError(status)));
    }
    FramePtr frame(av_frame_alloc());
    if (!frame)
        return fail(QStringLiteral("Could not allocate audio frame"));
    AudioConverter converter(
        request,
        parameters,
        streamTimeBase,
        streamIndex,
        decoder,
        std::move(origin),
        sink,
        stopToken);

    enum class Drain {
        NeedInput,
        Drained,
        Failed,
        Cancelled,
    };
    const auto drain = [&](bool flushing) -> Drain {
        while (true) {
            if (stopToken.stop_requested())
                return Drain::Cancelled;
            const int receive = avcodec_receive_frame(
                codecContext.get(), frame.get());
            if (receive == AVERROR(EAGAIN))
                return flushing ? Drain::Failed : Drain::NeedInput;
            if (receive == AVERROR_EOF)
                return Drain::Drained;
            if (receive < 0) {
                result.error = QStringLiteral(
                    "Audio decoding failed: %1")
                    .arg(ffmpegError(receive));
                return Drain::Failed;
            }
            ++result.decodedFrames;
            QString conversionError;
            const AudioConvertResult converted = converter.convert(
                *frame, conversionError);
            av_frame_unref(frame.get());
            if (converted != AudioConvertResult::Converted) {
                if (converted == AudioConvertResult::Stopped) {
                    result.stopped = true;
                } else if (converted
                        == AudioConvertResult::Cancelled) {
                    return Drain::Cancelled;
                } else {
                    result.error = conversionError;
                }
                return Drain::Failed;
            }
        }
    };

    while (!stopToken.stop_requested()) {
        FfmpegRoutedPacket input = router.pop(
            FfmpegPacketStream::Audio, stopToken);
        if (input.terminal
                == FfmpegPacketRouterTerminal::Cancelled) {
            result.cancelled = true;
            return result;
        }
        if (input.terminal
                == FfmpegPacketRouterTerminal::Failed)
            return fail(input.error);
        const bool flushing =
            input.terminal
                == FfmpegPacketRouterTerminal::EndOfStream;
        while (true) {
            const int sent = avcodec_send_packet(
                codecContext.get(),
                flushing ? nullptr : input.packet.get());
            if (sent != AVERROR(EAGAIN)) {
                if (sent < 0 && sent != AVERROR_EOF) {
                    return fail(QStringLiteral(
                        "Could not submit audio packet: %1")
                        .arg(ffmpegError(sent)));
                }
                break;
            }
            const std::uint64_t before = result.decodedFrames;
            const Drain progress = drain(false);
            if (progress == Drain::Cancelled) {
                result.cancelled = true;
                return result;
            }
            if (progress == Drain::Failed) {
                if (result.stopped) {
                    result.diagnostics = converter.diagnostics();
                    result.outputFrames = converter.outputFrames();
                    result.observedEndMicroseconds =
                        converter.observedEndMicroseconds();
                    stopSibling(router, operationStop);
                    return result;
                }
                return fail(result.error);
            }
            if (progress == Drain::NeedInput
                    && result.decodedFrames == before) {
                return fail(QStringLiteral(
                    "Audio decoder returned EAGAIN from both "
                    "send and receive without progress"));
            }
        }
        const Drain progress = drain(flushing);
        if (progress == Drain::Cancelled) {
            result.cancelled = true;
            return result;
        }
        if (progress == Drain::Failed) {
            if (result.stopped) {
                result.diagnostics = converter.diagnostics();
                result.outputFrames = converter.outputFrames();
                result.observedEndMicroseconds =
                    converter.observedEndMicroseconds();
                stopSibling(router, operationStop);
                return result;
            }
            return fail(result.error.isEmpty()
                ? QStringLiteral(
                    "Audio decoder did not drain at end of stream")
                : result.error);
        }
        if (flushing || progress == Drain::Drained) {
            QString flushError;
            const AudioConvertResult flushed =
                converter.flush(flushError);
            if (flushed != AudioConvertResult::Converted) {
                if (flushed == AudioConvertResult::Stopped) {
                    result.stopped = true;
                    result.diagnostics = converter.diagnostics();
                    result.outputFrames = converter.outputFrames();
                    result.observedEndMicroseconds =
                        converter.observedEndMicroseconds();
                    stopSibling(router, operationStop);
                    return result;
                }
                if (flushed == AudioConvertResult::Cancelled) {
                    result.cancelled = true;
                    return result;
                }
                return fail(flushError);
            }
            result.diagnostics = converter.diagnostics();
            result.outputFrames = converter.outputFrames();
            result.observedEndMicroseconds =
                converter.observedEndMicroseconds();
            result.endOfStream = true;
            return result;
        }
    }
    result.cancelled = true;
    return result;
}
}

bool FfmpegAudioStreamDiagnostics::isValid() const {
    return !decoderName.isEmpty()
        && audioStreamIndex >= 0
        && sourceSampleRate > 0
        && sourceChannelCount > 0
        && outputFormat.isValid()
        && (!timelineOrigin || timelineOrigin->isValid());
}

bool FfmpegMediaDecodeRequest::isValid() const {
    return video.isValid()
        && (!decodeSelectedAudio
            || audioOutput == AudioStreamFormat{48'000, 2});
}

bool FfmpegMediaDecodeResult::isSuccess() const {
    return video.isSuccess()
        && error.isEmpty()
        && !cancelled
        && !video.stopped
        && !audioStopped
        && (!audioStreamPresent
            || (audio && audio->isValid()
                && outputAudioFrames != 0
                && audioEndOfStream));
}

bool FfmpegMediaDecodeResult::isCancelled() const {
    return cancelled
        && error.isEmpty()
        && video.isCancelled();
}

bool FfmpegMediaDecodeResult::isStopped() const {
    return error.isEmpty()
        && !cancelled
        && (video.stopped || audioStopped);
}

FfmpegMediaDecodeResult decodeMediaFrames(
        const FfmpegMediaDecodeRequest &request,
        const FfmpegVideoFrameSink &videoSink,
        const FfmpegPcmAudioSink &audioSink,
        std::stop_token stopToken) {
    FfmpegMediaDecodeResult result;
    const auto fail = [&](QString error) {
        result.error = std::move(error);
        result.video.error = result.error;
        return result;
    };
    if (!request.isValid() || !videoSink
            || (request.decodeSelectedAudio && !audioSink)) {
        return fail(QStringLiteral("Media decode request is invalid"));
    }

    std::stop_source operationStop;
    std::stop_callback externalStop(
        stopToken,
        [&] { operationStop.request_stop(); });
    InterruptState interrupt{operationStop.get_token()};
    const auto cancel = [&] {
        result.error.clear();
        result.cancelled = true;
        result.video.error.clear();
        result.video.cancelled = true;
        return result;
    };
    if (stopToken.stop_requested())
        return cancel();

    AVFormatContext *rawFormat = avformat_alloc_context();
    if (!rawFormat)
        return fail(QStringLiteral("Could not allocate media context"));
    rawFormat->interrupt_callback = {
        interruptFfmpeg,
        &interrupt,
    };
    const QByteArray path = request.video.path.toUtf8();
    int status = avformat_open_input(
        &rawFormat, path.constData(), nullptr, nullptr);
    if (status < 0) {
        if (rawFormat)
            avformat_close_input(&rawFormat);
        if (stopToken.stop_requested())
            return cancel();
        return fail(QStringLiteral("Could not open media: %1")
            .arg(ffmpegError(status)));
    }
    FormatContextPtr formatContext(rawFormat);
    status = avformat_find_stream_info(
        formatContext.get(), nullptr);
    if (status < 0) {
        if (stopToken.stop_requested())
            return cancel();
        return fail(QStringLiteral(
            "Could not discover media streams: %1")
            .arg(ffmpegError(status)));
    }

    const AVCodec *videoDecoder = nullptr;
    const int videoIndex = av_find_best_stream(
        formatContext.get(),
        AVMEDIA_TYPE_VIDEO,
        -1,
        -1,
        &videoDecoder,
        0);
    if (videoIndex < 0 || !videoDecoder) {
        return fail(QStringLiteral(
            "Could not select video stream: %1")
            .arg(ffmpegError(videoIndex)));
    }
    AVStream &videoStream =
        *formatContext->streams[videoIndex];

    const AVCodec *audioDecoder = nullptr;
    int audioIndex = -1;
    AVStream *audioStream = nullptr;
    if (request.decodeSelectedAudio) {
        audioIndex = av_find_best_stream(
            formatContext.get(),
            AVMEDIA_TYPE_AUDIO,
            -1,
            videoIndex,
            &audioDecoder,
            0);
        if (audioIndex >= 0 && audioDecoder) {
            audioStream = formatContext->streams[audioIndex];
            result.audioStreamPresent = true;
        } else if (audioIndex != AVERROR_STREAM_NOT_FOUND) {
            return fail(QStringLiteral(
                "Could not select audio stream: %1")
                .arg(ffmpegError(audioIndex)));
        }
    }

    const std::optional<VideoTimelineOrigin> origin =
        ffmpegSharedTimelineOrigin(
            *formatContext,
            videoStream,
            audioStream,
            request.video.start.timelineOrigin);
    if ((request.video.start.targetPositionMicroseconds
            || audioStream)
            && !origin) {
        return fail(QStringLiteral(
            "The synchronized media timeline has no stable origin"));
    }

    if (request.video.start.performDemuxSeek) {
        if (!formatContext->pb
                || !(formatContext->pb->seekable
                    & AVIO_SEEKABLE_NORMAL)) {
            return fail(QStringLiteral(
                "The selected media source is not seekable"));
        }
        const auto targetTimestamp =
            videoStreamTimestampForPosition(
                *origin,
                {videoStream.time_base.num,
                 videoStream.time_base.den},
                *request.video.start
                    .targetPositionMicroseconds);
        if (!targetTimestamp) {
            return fail(QStringLiteral(
                "The requested media seek cannot be represented"));
        }
        const std::int64_t seekTimestamp =
            *targetTimestamp
                > std::numeric_limits<std::int64_t>::min()
            ? *targetTimestamp - 1
            : *targetTimestamp;
        status = avformat_seek_file(
            formatContext.get(),
            videoIndex,
            std::numeric_limits<std::int64_t>::min(),
            seekTimestamp,
            seekTimestamp,
            0);
        if (status < 0) {
            if (stopToken.stop_requested())
                return cancel();
            return fail(QStringLiteral(
                "Could not seek media: %1")
                .arg(ffmpegError(status)));
        }
    }

    for (unsigned int index = 0;
            index < formatContext->nb_streams;
            ++index) {
        formatContext->streams[index]->discard =
            static_cast<int>(index) == videoIndex
                || static_cast<int>(index) == audioIndex
            ? AVDISCARD_DEFAULT
            : AVDISCARD_ALL;
    }

    CodecParametersPtr videoParameters(
        avcodec_parameters_alloc());
    if (!videoParameters
            || avcodec_parameters_copy(
                videoParameters.get(), videoStream.codecpar) < 0) {
        return fail(QStringLiteral(
            "Could not retain video stream parameters"));
    }
    const AVRational videoTimeBase = videoStream.time_base;
    AVRational videoAspectRatio = videoStream.sample_aspect_ratio;
    if ((videoAspectRatio.num <= 0
            || videoAspectRatio.den <= 0)
            && videoStream.codecpar->sample_aspect_ratio.num > 0
            && videoStream.codecpar->sample_aspect_ratio.den > 0) {
        videoAspectRatio =
            videoStream.codecpar->sample_aspect_ratio;
    }
    FfmpegVideoStreamDiagnostics initialVideoDiagnostics =
        videoDiagnostics(
            *formatContext,
            videoStream,
            *videoDecoder,
            videoIndex,
            request.video,
            origin);

    CodecParametersPtr audioParameters;
    AVRational audioTimeBase{};
    if (audioStream) {
        audioParameters.reset(avcodec_parameters_alloc());
        if (!audioParameters
                || avcodec_parameters_copy(
                    audioParameters.get(), audioStream->codecpar) < 0) {
            return fail(QStringLiteral(
                "Could not retain audio stream parameters"));
        }
        audioTimeBase = audioStream->time_base;
    }

    FfmpegPacketRouter router;
    FfmpegVideoDecodeResult videoResult;
    AudioWorkerStatus audioResult;
    std::jthread videoWorker([&] {
        videoResult = decodeVideoPackets(
            request,
            *videoParameters,
            videoTimeBase,
            videoAspectRatio,
            *videoDecoder,
            initialVideoDiagnostics,
            router,
            videoSink,
            operationStop);
    });
    std::optional<std::jthread> audioWorker;
    if (audioStream) {
        audioWorker.emplace([&] {
            audioResult = decodeAudioPackets(
                request,
                *audioParameters,
                audioTimeBase,
                audioIndex,
                *audioDecoder,
                origin,
                router,
                audioSink,
                operationStop);
        });
    }

    while (!operationStop.stop_requested()) {
        PacketPtr packet(av_packet_alloc());
        if (!packet) {
            router.finish(
                FfmpegPacketRouterTerminal::Failed,
                QStringLiteral("Could not allocate media packet"));
            break;
        }
        const int read = av_read_frame(
            formatContext.get(), packet.get());
        if (read < 0) {
            if (operationStop.stop_requested()) {
                router.finish(
                    FfmpegPacketRouterTerminal::Cancelled);
            } else if (read == AVERROR_EOF) {
                router.finish(
                    FfmpegPacketRouterTerminal::EndOfStream);
            } else {
                router.finish(
                    FfmpegPacketRouterTerminal::Failed,
                    QStringLiteral("Media read failed: %1")
                        .arg(ffmpegError(read)));
            }
            break;
        }
        std::optional<FfmpegPacketStream> destination;
        if (packet->stream_index == videoIndex)
            destination = FfmpegPacketStream::Video;
        else if (packet->stream_index == audioIndex)
            destination = FfmpegPacketStream::Audio;
        if (destination
                && !router.push(
                    *destination,
                    std::move(packet),
                    operationStop.get_token())) {
            router.finish(
                FfmpegPacketRouterTerminal::Cancelled);
            break;
        }
    }

    videoWorker.join();
    if (audioWorker)
        audioWorker->join();

    const FfmpegPacketRouterStatistics routerStatistics =
        router.statistics();
    result.packetCountLimit = routerStatistics.packetCountLimit;
    result.packetByteLimit = routerStatistics.packetByteLimit;
    result.maximumQueuedPacketCount =
        routerStatistics.maximumQueuedPacketCount;
    result.maximumQueuedPacketBytes =
        routerStatistics.maximumQueuedPacketBytes;
    result.largestQueuedPacketBytes =
        routerStatistics.largestQueuedPacketBytes;

    result.video = std::move(videoResult);
    if (audioStream) {
        result.audio = std::move(audioResult.diagnostics);
        result.decodedAudioFrames =
            audioResult.decodedFrames;
        result.outputAudioFrames =
            audioResult.outputFrames;
        result.observedAudioEndMicroseconds =
            audioResult.observedEndMicroseconds;
        result.audioEndOfStream =
            audioResult.endOfStream;
        result.audioStopped = audioResult.stopped;
    }

    if (!result.video.error.isEmpty()) {
        result.error = result.video.error;
        return result;
    }
    if (audioStream && !audioResult.error.isEmpty()) {
        result.error = audioResult.error;
        return result;
    }
    if (result.video.stopped || audioResult.stopped)
        return result;
    if (stopToken.stop_requested()
            || result.video.isCancelled()
            || audioResult.cancelled) {
        result.cancelled = true;
        result.error.clear();
        result.video.error.clear();
        result.video.cancelled = true;
        return result;
    }
    if (!result.video.isSuccess()) {
        result.error = QStringLiteral("Video decoding failed");
        return result;
    }
    if (audioStream
            && !audioResult.endOfStream
            && !audioResult.stopped) {
        result.error = QStringLiteral("Audio decoding failed");
        return result;
    }
    const std::optional<std::int64_t> observedEnd =
        observedPlaybackDurationMicroseconds(
            result.video.observedEndMicroseconds,
            result.observedAudioEndMicroseconds,
            audioStream != nullptr);
    if (observedEnd) {
        result.video.diagnostics.durationMicroseconds = observedEnd;
        result.video.diagnostics.durationFinal = true;
        qCInfo(sunroomLogMediaDecode).noquote()
            << "event=media.duration_finalized"
            << "durationUs=" + QString::number(*observedEnd);
    } else if (audioStream) {
        result.video.diagnostics.durationMicroseconds =
            initialVideoDiagnostics.durationMicroseconds;
        result.video.diagnostics.durationFinal = false;
    }
    qCInfo(sunroomLogMediaDecode).noquote()
        << "event=decode.synchronized_complete"
        << "generation=" + QString::number(
            request.video.firstFrameIdentity
                .playbackGeneration)
        << "videoFrames=" + QString::number(
            result.video.framesDecoded)
        << "audioFrames=" + QString::number(
            result.outputAudioFrames)
        << "maxPacketCount=" + QString::number(
            result.maximumQueuedPacketCount)
        << "maxPacketBytes=" + QString::number(
            result.maximumQueuedPacketBytes)
        << "audioPresent=" + QString(
            result.audioStreamPresent
            ? QStringLiteral("true")
            : QStringLiteral("false"));
    return result;
}
