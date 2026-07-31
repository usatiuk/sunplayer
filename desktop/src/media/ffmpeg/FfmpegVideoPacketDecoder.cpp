#include "media/ffmpeg/FfmpegVideoPacketDecoder.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <QElapsedTimer>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
}

#include "diagnostics/LogCategories.h"
#include "media/DecodedVideoFrame.h"
#include "media/FfmpegHardwareDevice.h"
#include "media/ffmpeg/FfmpegFrameMetadata.h"
#include "media/ffmpeg/FfmpegStreamMetadata.h"

namespace {
struct CodecContextDeleter {
    void operator()(AVCodecContext *context) const {
        avcodec_free_context(&context);
    }
};

struct FrameDeleter {
    void operator()(AVFrame *frame) const {
        av_frame_free(&frame);
    }
};

using CodecContextPtr =
    std::unique_ptr<AVCodecContext, CodecContextDeleter>;
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
                && configuration->pix_fmt != AV_PIX_FMT_NONE
                && (configuration->methods
                    & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
            return configuration;
        }
    }
}

enum class DrainResult {
    NeedInput,
    Drained,
    Stopped,
    Cancelled,
    Failed,
};
}

void FfmpegAvPacketDeleter::operator()(AVPacket *packet) const {
    av_packet_free(&packet);
}

FfmpegVideoDecodeResult decodeFfmpegVideoPackets(
        const FfmpegVideoDecodeRequest &request,
        const AVCodec &decoder,
        const AVCodecParameters &streamParameters,
        VideoFrameRational streamTimeBase,
        VideoFrameRational streamAspectRatio,
        FfmpegVideoStreamDiagnostics baseDiagnostics,
        const FfmpegVideoPacketSource &source,
        const FfmpegVideoFrameSink &sink,
        std::stop_token stopToken,
        bool *hardwareSelected) {
    Q_ASSERT(request.isValid());
    Q_ASSERT(streamTimeBase.isValid());
    Q_ASSERT(baseDiagnostics.isValid());
    Q_ASSERT(source);
    Q_ASSERT(sink);
    Q_ASSERT(hardwareSelected);
    *hardwareSelected = false;

    QElapsedTimer operationTimer;
    operationTimer.start();
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
            const std::optional<std::int64_t> duration =
                observedPlaybackDurationMicroseconds(
                    result.observedEndMicroseconds,
                    std::nullopt);
            if (duration) {
                result.diagnostics.durationMicroseconds =
                    duration;
                result.diagnostics.durationFinal = true;
            }
        }
        return result;
    };
    if (stopToken.stop_requested())
        return cancel();

    // AVCodecContext::opaque points here, so this state must outlive codec
    // teardown and every get_format callback.
    HardwareDecodeState hardwareState;
    hardwareState.hardwareSelected = hardwareSelected;
    hardwareState.fallbackReason =
        request.hardwareDecode.unavailableReason;

    CodecContextPtr codecContext(
        avcodec_alloc_context3(&decoder));
    if (!codecContext) {
        return fail(QStringLiteral(
            "Could not allocate the video decoder"));
    }
    int status = avcodec_parameters_to_context(
        codecContext.get(), &streamParameters);
    if (status < 0) {
        return fail(QStringLiteral(
            "Could not configure the video decoder: %1")
            .arg(ffmpegError(status)));
    }
    codecContext->pkt_timebase = {
        streamTimeBase.numerator,
        streamTimeBase.denominator,
    };

    if (request.hardwareDecode.device) {
        AVBufferRef *deviceReference =
            request.hardwareDecode.device
                ->referenceDeviceContext();
        if (!deviceReference) {
            hardwareState.fallbackReason = QStringLiteral(
                "Could not retain the %1 device")
                .arg(request.hardwareDecode.device->apiName());
        } else {
            const auto *const deviceContext =
                reinterpret_cast<const AVHWDeviceContext *>(
                    deviceReference->data);
            const AVCodecHWConfig *configuration = deviceContext
                ? hardwareConfiguration(
                    decoder, deviceContext->type)
                : nullptr;
            if (!configuration) {
                hardwareState.fallbackReason = QStringLiteral(
                    "Decoder %1 does not support %2")
                    .arg(
                        QString::fromLatin1(decoder.name),
                        request.hardwareDecode.device->apiName());
                av_buffer_unref(&deviceReference);
            } else {
                hardwareState.pixelFormat = configuration->pix_fmt;
                hardwareState.graphicsDeviceGeneration =
                    request.hardwareDecode.device
                        ->graphicsDeviceGeneration();
                hardwareState.apiName =
                    request.hardwareDecode.device->apiName();
                codecContext->opaque = &hardwareState;
                codecContext->get_format = selectHardwareFormat;
                codecContext->hw_device_ctx = deviceReference;
                codecContext->extra_hw_frames =
                    request.extraHardwareFrames;
            }
        }
    }

    status = avcodec_open2(codecContext.get(), &decoder, nullptr);
    if (status < 0) {
        if (stopToken.stop_requested())
            return cancel();
        return fail(QStringLiteral(
            "Could not open video decoder %1: %2")
            .arg(
                QString::fromLatin1(decoder.name),
                ffmpegError(status)));
    }

    FramePtr frame(av_frame_alloc());
    if (!frame) {
        return fail(QStringLiteral(
            "Could not allocate FFmpeg frame storage"));
    }

    FfmpegVideoStreamDiagnostics softwareDiagnostics =
        baseDiagnostics;
    softwareDiagnostics.hardwareAccelerated = false;
    softwareDiagnostics.decodePath = QStringLiteral("Software");
    softwareDiagnostics.hardwareFallbackReason =
        hardwareState.fallbackReason;
    FfmpegVideoStreamDiagnostics hardwareDiagnostics =
        baseDiagnostics;
    hardwareDiagnostics.hardwareAccelerated = true;
    hardwareDiagnostics.decodePath = hardwareState.apiName;
    hardwareDiagnostics.hardwareFallbackReason.clear();
    std::optional<VideoTimelineOrigin> resolvedTimelineOrigin =
        request.start.timelineOrigin
        ? request.start.timelineOrigin
        : baseDiagnostics.timelineOrigin;
    std::uint64_t nextFrameId =
        request.firstFrameIdentity.frameId;
    ObservedVideoRange observedVideoRange;

    const auto drainDecoder =
        [&](bool flushing) -> DrainResult {
            while (true) {
                if (stopToken.stop_requested())
                    return DrainResult::Cancelled;
                const int receiveStatus = avcodec_receive_frame(
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
                        *frame, streamParameters)) {
                    av_frame_unref(frame.get());
                    result.error = QStringLiteral(
                        "Could not retain stream-level video metadata");
                    return DrainResult::Failed;
                }
                if ((frame->sample_aspect_ratio.num <= 0
                        || frame->sample_aspect_ratio.den <= 0)
                        && streamAspectRatio.isValid()) {
                    frame->sample_aspect_ratio = {
                        streamAspectRatio.numerator,
                        streamAspectRatio.denominator,
                    };
                }
                if (nextFrameId == 0) {
                    av_frame_unref(frame.get());
                    result.error = QStringLiteral(
                        "Decoded frame identity overflowed");
                    return DrainResult::Failed;
                }
                const VideoFrameIdentity identity{
                    .playbackGeneration = request
                        .firstFrameIdentity.playbackGeneration,
                    .decoderRevision = request
                        .firstFrameIdentity.decoderRevision,
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
                        streamTimeBase,
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
                    resolvedTimelineOrigin = VideoTimelineOrigin{
                        .timestamp = *decoded->timing().pts,
                        .timeBase = decoded->timing().timeBase,
                    };
                }
                diagnostics.timelineOrigin = resolvedTimelineOrigin;
                if (!hardwareFrame) {
                    diagnostics.hardwareFallbackReason =
                        hardwareState.fallbackReason;
                }
                if (!result.diagnostics.isValid())
                    result.diagnostics = diagnostics;
                const std::optional<std::int64_t> pts =
                    decoded->timing().ptsMicroseconds();
                const std::optional<std::int64_t> duration =
                    decoded->timing().durationMicroseconds();
                const std::optional<std::int64_t> origin =
                    resolvedTimelineOrigin
                    ? resolvedTimelineOrigin->microseconds()
                    : std::nullopt;
                observedVideoRange.observeFrame(
                    pts && origin
                        ? checkedTimestampSubtract(*pts, *origin)
                        : std::nullopt,
                    duration);
                result.observedEndMicroseconds =
                    observedVideoRange.endMicroseconds();
                ++result.framesDecoded;
                if (result.framesDecoded == 1
                        || (request.start
                                .targetPositionMicroseconds
                            && result.framesDecoded % 64 == 0)) {
                    qCDebug(sunroomLogMediaDecode).noquote()
                        << (result.framesDecoded == 1
                            ? "event=decode.first_frame"
                            : "event=decode.seek_progress")
                        << "generation=" + QString::number(
                            request.firstFrameIdentity
                                .playbackGeneration)
                        << "frames=" + QString::number(
                            result.framesDecoded)
                        << "pts=" + (decoded->timing().pts
                            ? QString::number(*decoded->timing().pts)
                            : QStringLiteral("none"))
                        << "ptsUs=" + (
                            decoded->timing().ptsMicroseconds()
                            ? QString::number(
                                *decoded->timing()
                                    .ptsMicroseconds())
                            : QStringLiteral("none"))
                        << "hardware=" + QString(
                            hardwareFrame
                            ? QStringLiteral("true")
                            : QStringLiteral("false"))
                        << "elapsedMs=" + QString::number(
                            operationTimer.elapsed());
                }
                if (!sink(std::move(decoded), diagnostics)) {
                    return stopToken.stop_requested()
                        ? DrainResult::Cancelled
                        : DrainResult::Stopped;
                }
            }
        };

    while (!stopToken.stop_requested()) {
        FfmpegVideoPacketRead input = source(stopToken);
        if (stopToken.stop_requested()
                || input.terminal
                    == FfmpegVideoPacketTerminal::Cancelled) {
            return cancel();
        }
        if (input.terminal
                == FfmpegVideoPacketTerminal::Failed) {
            return fail(input.error.isEmpty()
                ? QStringLiteral("Media demuxing failed")
                : input.error);
        }
        if (input.terminal
                == FfmpegVideoPacketTerminal::EndOfStream) {
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
                if (progress == DrainResult::Drained)
                    return endOfStream();
            }
            if (status < 0 && status != AVERROR_EOF) {
                return fail(QStringLiteral(
                    "Could not flush video decoder: %1")
                    .arg(ffmpegError(status)));
            }
            if (status == AVERROR_EOF)
                return endOfStream();
            const DrainResult drained = drainDecoder(true);
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

        if (!input.packet) {
            return fail(QStringLiteral(
                "The video packet source returned no packet"));
        }
        while (true) {
            status = avcodec_send_packet(
                codecContext.get(), input.packet.get());
            if (status != AVERROR(EAGAIN))
                break;
            const std::uint64_t before = result.framesDecoded;
            const DrainResult progress = drainDecoder(false);
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
            if (progress == DrainResult::Drained)
                return endOfStream();
        }
        if (status < 0) {
            if (stopToken.stop_requested())
                return cancel();
            return fail(QStringLiteral(
                "Could not submit video packet: %1")
                .arg(ffmpegError(status)));
        }

        const DrainResult drained = drainDecoder(false);
        if (drained == DrainResult::Stopped) {
            result.stopped = true;
            return result;
        }
        if (drained == DrainResult::Cancelled)
            return cancel();
        if (drained == DrainResult::Failed)
            return result;
        if (drained == DrainResult::Drained)
            return endOfStream();
    }
    return cancel();
}
