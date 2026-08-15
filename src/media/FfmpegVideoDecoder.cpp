#include "media/FfmpegVideoDecoder.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <QElapsedTimer>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include "diagnostics/LogCategories.h"
#include "media/ffmpeg/FfmpegStreamMetadata.h"
#include "media/ffmpeg/FfmpegVideoDecodeFallback.h"
#include "media/ffmpeg/FfmpegVideoPacketDecoder.h"

namespace {
constexpr std::size_t maximumQueuedPackets = 64;
constexpr std::size_t maximumQueuedPacketBytes = 4U * 1024U * 1024U;

struct FormatContextDeleter {
    void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};

struct CodecParametersDeleter {
    void operator()(AVCodecParameters* parameters) const { avcodec_parameters_free(&parameters); }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecParametersPtr = std::unique_ptr<AVCodecParameters, CodecParametersDeleter>;

QString ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
        return QStringLiteral("FFmpeg error %1").arg(code);
    }
    return QString::fromUtf8(buffer);
}

qint64 ioBytesRead(AVFormatContext const& context) { return context.pb ? context.pb->bytes_read : -1; }

qint64 ioPosition(AVFormatContext& context) { return context.pb ? avio_tell(context.pb) : -1; }

struct InterruptState {
    std::stop_token operationStopToken;
    std::stop_token demuxStopToken;
};

int interruptFfmpeg(void* opaque) {
    auto const& state = *static_cast<InterruptState const*>(opaque);
    return state.operationStopToken.stop_requested() || state.demuxStopToken.stop_requested() ? 1 : 0;
}

class BoundedPacketChannel final {
  public:
    bool push(FfmpegAvPacketPtr packet, std::stop_token stopToken) {
        Q_ASSERT(packet);
        std::size_t const bytes = packet->size > 0 ? static_cast<std::size_t>(packet->size) : 0;
        std::unique_lock lock(m_mutex);
        bool const ready = m_wake.wait(lock, stopToken, [this, bytes] {
            if (m_terminal != FfmpegVideoPacketTerminal::Packet) {
                return true;
            }
            bool const countAvailable = m_packets.size() < maximumQueuedPackets;
            bool const bytesAvailable =
                m_packets.empty() ||
                bytes <= maximumQueuedPacketBytes - std::min(m_queuedBytes, maximumQueuedPacketBytes);
            return countAvailable && bytesAvailable;
        });
        if (!ready || m_terminal != FfmpegVideoPacketTerminal::Packet) {
            return false;
        }
        m_queuedBytes += bytes;
        m_packets.push_back({
            .packet = std::move(packet),
            .bytes = bytes,
        });
        lock.unlock();
        m_wake.notify_all();
        return true;
    }

    FfmpegVideoPacketRead pop(std::stop_token stopToken) {
        std::unique_lock lock(m_mutex);
        bool const ready = m_wake.wait(
            lock, stopToken, [this] { return !m_packets.empty() || m_terminal != FfmpegVideoPacketTerminal::Packet; });
        if (!ready) {
            return {
                .terminal = FfmpegVideoPacketTerminal::Cancelled,
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

    void finish(FfmpegVideoPacketTerminal terminal, QString error = {}) {
        Q_ASSERT(terminal != FfmpegVideoPacketTerminal::Packet);
        {
            std::lock_guard lock(m_mutex);
            if (m_terminal != FfmpegVideoPacketTerminal::Packet) {
                return;
            }
            m_terminal = terminal;
            m_error = std::move(error);
        }
        m_wake.notify_all();
    }

  private:
    struct Entry {
        FfmpegAvPacketPtr packet;
        std::size_t bytes = 0;
    };

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Entry> m_packets;
    std::size_t m_queuedBytes = 0;
    FfmpegVideoPacketTerminal m_terminal = FfmpegVideoPacketTerminal::Packet;
    QString m_error;
};

std::optional<std::int64_t> positiveDurationMicroseconds(std::int64_t value, AVRational timeBase) {
    if (value <= 0 || value == AV_NOPTS_VALUE) {
        return std::nullopt;
    }
    std::int64_t const converted = av_rescale_q(value, timeBase, AV_TIME_BASE_Q);
    return converted > 0 ? std::optional<std::int64_t>(converted) : std::nullopt;
}

VideoTimelineOrigin timelineOrigin(std::int64_t timestamp, AVRational timeBase) {
    return {
        .timestamp = timestamp,
        .timeBase = {timeBase.num, timeBase.den},
    };
}

FfmpegVideoStreamDiagnostics streamDiagnostics(AVFormatContext const& formatContext, AVStream const& stream,
                                               AVCodec const& decoder, int streamIndex,
                                               FfmpegVideoDecodeRequest const& request) {
    FfmpegVideoStreamDiagnostics diagnostics{
        .containerFormat = formatContext.iformat && formatContext.iformat->name
                               ? QString::fromLatin1(formatContext.iformat->name)
                               : QStringLiteral("unknown"),
        .decoderName = decoder.name ? QString::fromLatin1(decoder.name) : QStringLiteral("unknown"),
        .decodePath = QStringLiteral("Software"),
        .hardwareFallbackReason = request.hardwareDecode.unavailableReason,
        .videoStreamIndex = streamIndex,
        .hardwareAccelerated = false,
        .seekable = formatContext.pb && (formatContext.pb->seekable & AVIO_SEEKABLE_NORMAL),
    };
    diagnostics.durationMicroseconds = ffmpegProvisionalDurationMicroseconds(formatContext, stream);
    if (formatContext.start_time != AV_NOPTS_VALUE) {
        diagnostics.timelineOrigin = timelineOrigin(formatContext.start_time, AV_TIME_BASE_Q);
    } else if (stream.start_time != AV_NOPTS_VALUE) {
        diagnostics.timelineOrigin = timelineOrigin(stream.start_time, stream.time_base);
    }
    AVRational const frameRate =
        av_guess_frame_rate(const_cast<AVFormatContext*>(&formatContext), const_cast<AVStream*>(&stream), nullptr);
    if (frameRate.num > 0 && frameRate.den > 0) {
        diagnostics.nominalFrameDurationMicroseconds = positiveDurationMicroseconds(1, av_inv_q(frameRate));
    }
    return diagnostics;
}

FfmpegVideoDecodeResult decodeVideoFramesAttempt(FfmpegVideoDecodeRequest const& request,
                                                 FfmpegVideoFrameSink const& sink, std::stop_token stopToken,
                                                 bool* hardwareSelected) {
    Q_ASSERT(hardwareSelected);
    Q_ASSERT(sink);
    *hardwareSelected = false;
    std::uint64_t const generation = request.firstFrameIdentity.playbackGeneration;
    QElapsedTimer operationTimer;
    operationTimer.start();

    FfmpegVideoDecodeResult result;
    auto const fail = [&result](QString message) {
        result.error = std::move(message);
        return result;
    };
    auto const cancel = [&result] {
        result.error.clear();
        result.cancelled = true;
        return result;
    };
    if (stopToken.stop_requested()) {
        return cancel();
    }
    if (!request.isValid()) {
        return fail(QStringLiteral("Video decode request is invalid"));
    }

    qCDebug(sunplayerLogMediaDecode).noquote()
        << "event=decode.attempt_start"
        << "generation=" + QString::number(generation)
        << "targetUs=" + (request.start.targetPositionMicroseconds
                              ? QString::number(*request.start.targetPositionMicroseconds)
                              : QStringLiteral("none"))
        << "demuxSeek=" + QString(request.start.performDemuxSeek ? QStringLiteral("true") : QStringLiteral("false"))
        << "path=" + request.path;

    AVFormatContext* rawFormatContext = avformat_alloc_context();
    if (!rawFormatContext) {
        return fail(QStringLiteral("Could not allocate the media container context"));
    }
    InterruptState interruptState{
        .operationStopToken = stopToken,
    };
    rawFormatContext->interrupt_callback = {
        interruptFfmpeg,
        &interruptState,
    };
    QByteArray const encodedPath = request.path.toUtf8();
    int status = avformat_open_input(&rawFormatContext, encodedPath.constData(), nullptr, nullptr);
    if (status < 0) {
        if (rawFormatContext) {
            avformat_close_input(&rawFormatContext);
        }
        if (stopToken.stop_requested()) {
            return cancel();
        }
        return fail(QStringLiteral("Could not open media: %1").arg(ffmpegError(status)));
    }
    FormatContextPtr formatContext(rawFormatContext);
    qCDebug(sunplayerLogMediaIo).noquote() << "event=media.open_complete"
                                         << "generation=" + QString::number(generation)
                                         << "elapsedMs=" + QString::number(operationTimer.elapsed())
                                         << "bytesRead=" + QString::number(ioBytesRead(*formatContext));

    status = avformat_find_stream_info(formatContext.get(), nullptr);
    if (status < 0) {
        if (stopToken.stop_requested()) {
            return cancel();
        }
        return fail(QStringLiteral("Could not discover media streams: %1").arg(ffmpegError(status)));
    }
    qCDebug(sunplayerLogMediaIo).noquote() << "event=media.probe_complete"
                                         << "generation=" + QString::number(generation)
                                         << "elapsedMs=" + QString::number(operationTimer.elapsed())
                                         << "bytesRead=" + QString::number(ioBytesRead(*formatContext));

    AVCodec const* decoder = nullptr;
    int const streamIndex = av_find_best_stream(formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (stopToken.stop_requested()) {
        return cancel();
    }
    if (streamIndex < 0 || !decoder) {
        return fail(QStringLiteral("Could not select a video stream: %1").arg(ffmpegError(streamIndex)));
    }
    AVStream const& stream = *formatContext->streams[streamIndex];
    AVRational const streamTimeBase = stream.time_base;
    AVRational streamAspectRatio = stream.sample_aspect_ratio;
    if ((streamAspectRatio.num <= 0 || streamAspectRatio.den <= 0) && stream.codecpar->sample_aspect_ratio.num > 0 &&
        stream.codecpar->sample_aspect_ratio.den > 0) {
        streamAspectRatio = stream.codecpar->sample_aspect_ratio;
    }

    if (request.start.performDemuxSeek) {
        if (!formatContext->pb || !(formatContext->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
            return fail(QStringLiteral("The selected media source is not seekable"));
        }
        VideoTimelineOrigin const& origin = *request.start.timelineOrigin;
        auto const targetTimestamp = videoStreamTimestampForPosition(origin, {streamTimeBase.num, streamTimeBase.den},
                                                                     *request.start.targetPositionMicroseconds);
        if (!targetTimestamp) {
            return fail(QStringLiteral("The requested video seek timestamp "
                                       "cannot be represented"));
        }
        qCDebug(sunplayerLogMediaIo).noquote()
            << "event=media.seek_request"
            << "generation=" + QString::number(generation) << "stream=" + QString::number(streamIndex)
            << "targetUs=" + QString::number(*request.start.targetPositionMicroseconds)
            << "targetTs=" + QString::number(*targetTimestamp);
        status = avformat_seek_file(formatContext.get(), streamIndex, std::numeric_limits<std::int64_t>::min(),
                                    *targetTimestamp, *targetTimestamp, 0);
        if (status < 0) {
            if (stopToken.stop_requested()) {
                return cancel();
            }
            return fail(QStringLiteral("Could not seek video to %1 ms: %2")
                            .arg(*request.start.targetPositionMicroseconds / 1'000)
                            .arg(ffmpegError(status)));
        }
        qCDebug(sunplayerLogMediaIo).noquote()
            << "event=media.seek_complete"
            << "generation=" + QString::number(generation) << "elapsedMs=" + QString::number(operationTimer.elapsed())
            << "bytesRead=" + QString::number(ioBytesRead(*formatContext))
            << "bytePosition=" + QString::number(ioPosition(*formatContext));
    }

    for (unsigned int index = 0; index < formatContext->nb_streams; ++index) {
        formatContext->streams[index]->discard =
            static_cast<int>(index) == streamIndex ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }

    CodecParametersPtr streamParameters(avcodec_parameters_alloc());
    if (!streamParameters || avcodec_parameters_copy(streamParameters.get(), stream.codecpar) < 0) {
        return fail(QStringLiteral("Could not retain video stream parameters"));
    }
    FfmpegVideoStreamDiagnostics const diagnostics =
        streamDiagnostics(*formatContext, stream, *decoder, streamIndex, request);

    BoundedPacketChannel packets;
    std::atomic_bool firstSelectedPacket = true;
    std::jthread demuxWorker([&](std::stop_token demuxStopToken) {
        interruptState.demuxStopToken = demuxStopToken;
        while (!stopToken.stop_requested() && !demuxStopToken.stop_requested()) {
            FfmpegAvPacketPtr packet(av_packet_alloc());
            if (!packet) {
                packets.finish(FfmpegVideoPacketTerminal::Failed,
                               QStringLiteral("Could not allocate FFmpeg packet storage"));
                return;
            }
            int const readStatus = av_read_frame(formatContext.get(), packet.get());
            if (readStatus < 0) {
                if (stopToken.stop_requested() || demuxStopToken.stop_requested()) {
                    packets.finish(FfmpegVideoPacketTerminal::Cancelled);
                } else if (readStatus == AVERROR_EOF) {
                    packets.finish(FfmpegVideoPacketTerminal::EndOfStream);
                } else {
                    packets.finish(FfmpegVideoPacketTerminal::Failed,
                                   QStringLiteral("Media read failed: %1").arg(ffmpegError(readStatus)));
                }
                return;
            }
            if (packet->stream_index != streamIndex) {
                continue;
            }
            if (firstSelectedPacket.exchange(false, std::memory_order_relaxed)) {
                qCDebug(sunplayerLogMediaIo).noquote()
                    << "event=demux.first_video_packet"
                    << "generation=" + QString::number(generation) << "pts=" + QString::number(packet->pts)
                    << "dts=" + QString::number(packet->dts) << "duration=" + QString::number(packet->duration)
                    << "position=" + QString::number(packet->pos)
                    << "key=" +
                           QString(packet->flags & AV_PKT_FLAG_KEY ? QStringLiteral("true") : QStringLiteral("false"))
                    << "bytesRead=" + QString::number(ioBytesRead(*formatContext));
            }
            if (!packets.push(std::move(packet), demuxStopToken)) {
                packets.finish(FfmpegVideoPacketTerminal::Cancelled);
                return;
            }
        }
        packets.finish(FfmpegVideoPacketTerminal::Cancelled);
    });

    result = decodeFfmpegVideoPackets(
        request, *decoder, *streamParameters, {streamTimeBase.num, streamTimeBase.den},
        {streamAspectRatio.num, streamAspectRatio.den}, diagnostics,
        [&packets](std::stop_token packetStopToken) { return packets.pop(packetStopToken); }, sink, stopToken,
        hardwareSelected);
    demuxWorker.request_stop();
    packets.finish(FfmpegVideoPacketTerminal::Cancelled);
    demuxWorker.join();
    return result;
}
} // namespace

bool VideoTimelineOrigin::isValid() const { return timeBase.isValid(); }

std::optional<std::int64_t> VideoTimelineOrigin::microseconds() const {
    if (!isValid()) {
        return std::nullopt;
    }
    return VideoFrameTiming{
        .pts = timestamp,
        .timeBase = timeBase,
    }
        .ptsMicroseconds();
}

std::optional<std::int64_t> videoStreamTimestampForPosition(VideoTimelineOrigin const& origin,
                                                            VideoFrameRational const& streamTimeBase,
                                                            std::int64_t targetPositionMicroseconds) {
    if (!origin.isValid() || !streamTimeBase.isValid() || targetPositionMicroseconds < 0 ||
        origin.timestamp == AV_NOPTS_VALUE) {
        return std::nullopt;
    }
    AVRational const originTimeBase{
        origin.timeBase.numerator,
        origin.timeBase.denominator,
    };
    AVRational const targetTimeBase{
        streamTimeBase.numerator,
        streamTimeBase.denominator,
    };
    std::int64_t const originTimestamp = av_rescale_q(origin.timestamp, originTimeBase, targetTimeBase);
    std::int64_t const offsetTimestamp = av_rescale_q(targetPositionMicroseconds, AV_TIME_BASE_Q, targetTimeBase);
    if (originTimestamp == AV_NOPTS_VALUE || offsetTimestamp < 0 ||
        originTimestamp > std::numeric_limits<std::int64_t>::max() - offsetTimestamp) {
        return std::nullopt;
    }
    return originTimestamp + offsetTimestamp;
}

bool VideoDecodeStart::isValid() const {
    return (!targetPositionMicroseconds || *targetPositionMicroseconds >= 0) &&
           (!timelineOrigin || timelineOrigin->isValid()) && (!targetPositionMicroseconds || timelineOrigin) &&
           (!performDemuxSeek || targetPositionMicroseconds);
}

bool FfmpegVideoDecodeRequest::isValid() const {
    return !path.isEmpty() && firstFrameIdentity.isValid() && extraHardwareFrames >= 0 && start.isValid();
}

bool FfmpegVideoStreamDiagnostics::isValid() const {
    return !containerFormat.isEmpty() && !decoderName.isEmpty() && !decodePath.isEmpty() &&
           (hardwareAccelerated ? decodePath != QStringLiteral("Software") && hardwareFallbackReason.isEmpty()
                                : decodePath == QStringLiteral("Software")) &&
           videoStreamIndex >= 0 && (!timelineOrigin || timelineOrigin->isValid()) &&
           (!durationMicroseconds || *durationMicroseconds > 0) &&
           (!nominalFrameDurationMicroseconds || *nominalFrameDurationMicroseconds > 0);
}

bool FfmpegVideoDecodeResult::isSuccess() const {
    return diagnostics.isValid() && framesDecoded != 0 && error.isEmpty() && !cancelled && (endOfStream || stopped);
}

bool FfmpegVideoDecodeResult::isCancelled() const { return cancelled && error.isEmpty() && !endOfStream && !stopped; }

FfmpegVideoDecodeResult decodeVideoFrames(FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink,
                                          std::stop_token stopToken) {
    return decodeVideoFramesWithFallback(
        request.hardwareDecode, [&](VideoHardwareDecodeCapability const& capability, bool& hardwareSelected) {
            FfmpegVideoDecodeRequest attemptRequest = request;
            attemptRequest.hardwareDecode = capability;
            return decodeVideoFramesAttempt(attemptRequest, sink, stopToken, &hardwareSelected);
        });
}
