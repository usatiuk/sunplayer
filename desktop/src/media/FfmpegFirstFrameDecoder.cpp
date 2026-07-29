#include "media/FfmpegFirstFrameDecoder.h"

#include <memory>
#include <optional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include "media/DecodedVideoFrame.h"
#include "media/ffmpeg/FfmpegFrameMetadata.h"

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

FfmpegFirstFrameResult decodedResult(
        AVFrame &frame,
        AVStream &stream,
        const AVCodec &decoder,
        AVFormatContext &formatContext,
        int streamIndex,
        const VideoFrameIdentity &identity) {
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
    std::shared_ptr<const DecodedVideoFrame> decoded =
        DecodedVideoFrame::clone(
            frame,
            identity,
            {
                stream.time_base.num,
                stream.time_base.den,
            },
            std::nullopt,
            &frameError);
    if (!decoded)
        return failure(frameError);

    FfmpegFirstFrameResult result;
    result.frame = std::move(decoded);
    result.diagnostics.containerFormat =
        formatContext.iformat && formatContext.iformat->name
        ? QString::fromLatin1(formatContext.iformat->name)
        : QStringLiteral("unknown");
    result.diagnostics.decoderName =
        decoder.name
        ? QString::fromLatin1(decoder.name)
        : QStringLiteral("unknown");
    result.diagnostics.videoStreamIndex = streamIndex;
    return result;
}
}

bool FfmpegFirstFrameDiagnostics::isValid() const {
    return !containerFormat.isEmpty()
        && !decoderName.isEmpty()
        && videoStreamIndex >= 0;
}

bool FfmpegFirstFrameResult::isSuccess() const {
    return frame
        && diagnostics.isValid()
        && error.isEmpty();
}

FfmpegFirstFrameResult decodeFirstVideoFrame(
        const QString &path,
        const VideoFrameIdentity &identity) {
    if (path.isEmpty())
        return failure(QStringLiteral("Media path is empty"));

    if (!identity.isValid()) {
        return failure(QStringLiteral(
            "First-frame decode identity is invalid"));
    }

    AVFormatContext *rawFormatContext = nullptr;
    const QByteArray encodedPath = path.toUtf8();
    int status = avformat_open_input(
        &rawFormatContext,
        encodedPath.constData(),
        nullptr,
        nullptr);
    if (status < 0) {
        return failure(QStringLiteral(
            "Could not open media: %1")
            .arg(ffmpegError(status)));
    }
    FormatContextPtr formatContext(rawFormatContext);

    status = avformat_find_stream_info(
        formatContext.get(), nullptr);
    if (status < 0) {
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
    status = avcodec_open2(
        codecContext.get(), decoder, nullptr);
    if (status < 0) {
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
        const int receiveStatus =
            avcodec_receive_frame(
                codecContext.get(), frame.get());
        if (receiveStatus == AVERROR(EAGAIN)
                || receiveStatus == AVERROR_EOF) {
            return std::nullopt;
        }
        if (receiveStatus < 0) {
            return failure(QStringLiteral(
                "Video decoding failed: %1")
                .arg(ffmpegError(receiveStatus)));
        }
        return decodedResult(
            *frame,
            stream,
            *decoder,
            *formatContext,
            streamIndex,
            identity);
    };

    while ((status = av_read_frame(
                formatContext.get(), packet.get())) >= 0) {
        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet.get());
            continue;
        }

        const int sendStatus =
            avcodec_send_packet(
                codecContext.get(), packet.get());
        av_packet_unref(packet.get());
        if (sendStatus < 0) {
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
    if (status != AVERROR_EOF) {
        return failure(QStringLiteral(
            "Media read failed: %1")
            .arg(ffmpegError(status)));
    }

    status = avcodec_send_packet(codecContext.get(), nullptr);
    if (status < 0 && status != AVERROR_EOF) {
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

    return failure(QStringLiteral(
        "The selected video stream produced no decoded frame"));
}
