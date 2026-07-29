#include "media/FfmpegFirstFrameDecoder.h"

#include <memory>
#include <optional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
}

#include "media/DecodedVideoFrame.h"
#include "media/FfmpegHardwareDevice.h"
#include "media/ffmpeg/FfmpegFirstFrameDecodeFallback.h"
#include "media/ffmpeg/FfmpegFrameMetadata.h"

namespace {
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
        *static_cast<HardwareDecodeState *>(
            context->opaque);
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

FfmpegFirstFrameResult failure(const QString &message) {
    FfmpegFirstFrameResult result;
    result.error = message;
    return result;
}

FfmpegFirstFrameResult cancellation() {
    FfmpegFirstFrameResult result;
    result.cancelled = true;
    return result;
}

struct InterruptState {
    std::stop_token stopToken;
};

int interruptFfmpeg(void *opaque) {
    const auto &state =
        *static_cast<const InterruptState *>(opaque);
    return state.stopToken.stop_requested() ? 1 : 0;
}

FfmpegFirstFrameResult decodedResult(
        AVFrame &frame,
        AVStream &stream,
        const AVCodec &decoder,
        AVFormatContext &formatContext,
        int streamIndex,
        const VideoFrameIdentity &identity,
        HardwareDecodeState &hardwareState) {
    if (!mergeStreamVideoMetadata(
            frame, *stream.codecpar)) {
        return failure(QStringLiteral(
            "Could not retain stream-level video metadata"));
    }
    const AVRational effectiveAspectRatio =
        av_guess_sample_aspect_ratio(
            &formatContext, &stream, &frame);
    if (effectiveAspectRatio.num > 0
            && effectiveAspectRatio.den > 0) {
        frame.sample_aspect_ratio =
            effectiveAspectRatio;
    }

    QString frameError;
    const bool hardwareFrame =
        frame.format == hardwareState.pixelFormat
        && frame.hw_frames_ctx;
    std::shared_ptr<const DecodedVideoFrame> decoded =
        DecodedVideoFrame::clone(
            frame,
            identity,
            {
                stream.time_base.num,
                stream.time_base.den,
            },
            hardwareFrame
                ? hardwareState.graphicsDeviceGeneration
                : std::nullopt,
            &frameError);
    if (!decoded)
        return failure(frameError);

    FfmpegFirstFrameResult result;
    result.diagnostics.containerFormat =
        formatContext.iformat && formatContext.iformat->name
        ? QString::fromLatin1(formatContext.iformat->name)
        : QStringLiteral("unknown");
    result.diagnostics.decoderName =
        decoder.name
        ? QString::fromLatin1(decoder.name)
        : QStringLiteral("unknown");
    result.diagnostics.hardwareAccelerated =
        decoded->storage().isHardware();
    result.diagnostics.decodePath =
        result.diagnostics.hardwareAccelerated
        ? hardwareState.apiName
        : QStringLiteral("Software");
    if (!result.diagnostics.hardwareAccelerated)
        result.diagnostics.hardwareFallbackReason =
            hardwareState.fallbackReason;
    result.diagnostics.videoStreamIndex = streamIndex;
    result.frame = std::move(decoded);
    return result;
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
}

bool FfmpegFirstFrameDiagnostics::isValid() const {
    return !containerFormat.isEmpty()
        && !decoderName.isEmpty()
        && !decodePath.isEmpty()
        && (hardwareAccelerated
            ? decodePath != QStringLiteral("Software")
                && hardwareFallbackReason.isEmpty()
            : decodePath == QStringLiteral("Software"))
        && videoStreamIndex >= 0;
}

bool FfmpegFirstFrameResult::isSuccess() const {
    return frame
        && diagnostics.isValid()
        && error.isEmpty()
        && !cancelled;
}

bool FfmpegFirstFrameResult::isCancelled() const {
    return cancelled
        && !frame
        && error.isEmpty();
}

namespace {
FfmpegFirstFrameResult decodeFirstVideoFrameAttempt(
        const QString &path,
        const VideoFrameIdentity &identity,
        const VideoHardwareDecodeCapability &hardwareDecode,
        std::stop_token stopToken,
        bool *hardwareSelected) {
    Q_ASSERT(hardwareSelected);
    *hardwareSelected = false;
    HardwareDecodeState hardwareState;
    hardwareState.hardwareSelected = hardwareSelected;
    hardwareState.fallbackReason =
        hardwareDecode.unavailableReason;

    if (stopToken.stop_requested())
        return cancellation();
    if (path.isEmpty())
        return failure(QStringLiteral("Media path is empty"));

    if (!identity.isValid()) {
        return failure(QStringLiteral(
            "First-frame decode identity is invalid"));
    }

    InterruptState interruptState{stopToken};
    AVFormatContext *rawFormatContext =
        avformat_alloc_context();
    if (!rawFormatContext) {
        return failure(QStringLiteral(
            "Could not allocate the media container context"));
    }
    rawFormatContext->interrupt_callback = {
        interruptFfmpeg,
        &interruptState,
    };
    const QByteArray encodedPath = path.toUtf8();
    int status = avformat_open_input(
        &rawFormatContext,
        encodedPath.constData(),
        nullptr,
        nullptr);
    if (status < 0) {
        if (rawFormatContext)
            avformat_close_input(&rawFormatContext);
        if (stopToken.stop_requested())
            return cancellation();
        return failure(QStringLiteral(
            "Could not open media: %1")
            .arg(ffmpegError(status)));
    }
    FormatContextPtr formatContext(rawFormatContext);

    status = avformat_find_stream_info(
        formatContext.get(), nullptr);
    if (status < 0) {
        if (stopToken.stop_requested())
            return cancellation();
        return failure(QStringLiteral(
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
        return cancellation();
    if (streamIndex < 0 || !decoder) {
        return failure(QStringLiteral(
            "Could not select a video stream: %1")
            .arg(ffmpegError(streamIndex)));
    }
    AVStream &stream =
        *formatContext->streams[streamIndex];
    CodecContextPtr codecContext(
        avcodec_alloc_context3(decoder));
    if (!codecContext) {
        return failure(QStringLiteral(
            "Could not allocate the video decoder"));
    }

    status = avcodec_parameters_to_context(
        codecContext.get(), stream.codecpar);
    if (status < 0) {
        return failure(QStringLiteral(
            "Could not configure the video decoder: %1")
            .arg(ffmpegError(status)));
    }
    codecContext->pkt_timebase = stream.time_base;

    if (hardwareDecode.device) {
        AVBufferRef *deviceReference =
            hardwareDecode.device
                ->referenceDeviceContext();
        if (!deviceReference) {
            hardwareState.fallbackReason =
                QStringLiteral(
                    "Could not retain the %1 device")
                    .arg(hardwareDecode.device->apiName());
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
                            hardwareDecode.device->apiName());
                av_buffer_unref(&deviceReference);
            } else {
                hardwareState.pixelFormat =
                    configuration->pix_fmt;
                hardwareState.graphicsDeviceGeneration =
                    hardwareDecode.device
                        ->graphicsDeviceGeneration();
                hardwareState.apiName =
                    hardwareDecode.device->apiName();
                codecContext->opaque = &hardwareState;
                codecContext->get_format =
                    selectHardwareFormat;
                codecContext->hw_device_ctx =
                    deviceReference;
                // One published first frame plus a small bounded margin.
                codecContext->extra_hw_frames = 2;
            }
        }
    }

    status = avcodec_open2(
        codecContext.get(), decoder, nullptr);
    if (status < 0) {
        if (stopToken.stop_requested())
            return cancellation();
        return failure(QStringLiteral(
            "Could not open video decoder %1: %2")
            .arg(
                QString::fromLatin1(decoder->name),
                ffmpegError(status)));
    }

    PacketPtr packet(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    if (!packet || !frame) {
        return failure(QStringLiteral(
            "Could not allocate FFmpeg packet/frame storage"));
    }

    const auto receiveFrame = [&]()
            -> std::optional<FfmpegFirstFrameResult> {
        if (stopToken.stop_requested())
            return cancellation();
        const int receiveStatus =
            avcodec_receive_frame(
                codecContext.get(), frame.get());
        if (receiveStatus == AVERROR(EAGAIN)
                || receiveStatus == AVERROR_EOF) {
            return std::nullopt;
        }
        if (receiveStatus < 0) {
            if (stopToken.stop_requested())
                return cancellation();
            return failure(QStringLiteral(
                "Video decoding failed: %1")
                .arg(ffmpegError(receiveStatus)));
        }
        if (stopToken.stop_requested())
            return cancellation();
        return decodedResult(
            *frame,
            stream,
            *decoder,
            *formatContext,
            streamIndex,
            identity,
            hardwareState);
    };

    while ((status = av_read_frame(
                formatContext.get(), packet.get())) >= 0) {
        if (stopToken.stop_requested())
            return cancellation();
        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet.get());
            continue;
        }

        const int sendStatus =
            avcodec_send_packet(
                codecContext.get(), packet.get());
        av_packet_unref(packet.get());
        if (sendStatus < 0) {
            if (stopToken.stop_requested())
                return cancellation();
            return failure(QStringLiteral(
                "Could not submit video packet: %1")
                .arg(ffmpegError(sendStatus)));
        }

        while (true) {
            std::optional<FfmpegFirstFrameResult> result =
                receiveFrame();
            if (!result)
                break;
            return std::move(*result);
        }
    }
    if (stopToken.stop_requested())
        return cancellation();
    if (status != AVERROR_EOF) {
        return failure(QStringLiteral(
            "Media read failed: %1")
            .arg(ffmpegError(status)));
    }

    status = avcodec_send_packet(codecContext.get(), nullptr);
    if (status < 0 && status != AVERROR_EOF) {
        if (stopToken.stop_requested())
            return cancellation();
        return failure(QStringLiteral(
            "Could not flush video decoder: %1")
            .arg(ffmpegError(status)));
    }
    while (true) {
        std::optional<FfmpegFirstFrameResult> result =
            receiveFrame();
        if (!result)
            break;
        return std::move(*result);
    }

    if (stopToken.stop_requested())
        return cancellation();
    return failure(QStringLiteral(
        "The selected video stream produced no decoded frame"));
}
}

FfmpegFirstFrameResult decodeFirstVideoFrame(
        const QString &path,
        const VideoFrameIdentity &identity,
        std::stop_token stopToken) {
    return decodeFirstVideoFrame(
        path,
        identity,
        VideoHardwareDecodeCapability{},
        stopToken);
}

FfmpegFirstFrameResult decodeFirstVideoFrame(
        const QString &path,
        const VideoFrameIdentity &identity,
        const VideoHardwareDecodeCapability &hardwareDecode,
        std::stop_token stopToken) {
    return decodeFirstVideoFrameWithFallback(
        hardwareDecode,
        [&](const VideoHardwareDecodeCapability &capability,
            bool &hardwareSelected) {
            return decodeFirstVideoFrameAttempt(
                path,
                identity,
                capability,
                stopToken,
                &hardwareSelected);
        });
}
