#include "media/FfmpegMediaDecoder.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <QLocale>
#include <QStringList>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
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
    void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const { avcodec_free_context(&context); }
};

struct CodecParametersDeleter {
    void operator()(AVCodecParameters* parameters) const { avcodec_parameters_free(&parameters); }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

struct SwrContextDeleter {
    void operator()(SwrContext* context) const { swr_free(&context); }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using CodecParametersPtr = std::unique_ptr<AVCodecParameters, CodecParametersDeleter>;
using PacketPtr = FfmpegAvPacketPtr;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

QString ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
        return QStringLiteral("FFmpeg error %1").arg(code);
    }
    return QString::fromUtf8(buffer);
}

struct InterruptState {
    std::stop_token stopToken;
};

int interruptFfmpeg(void* opaque) {
    return static_cast<InterruptState const*>(opaque)->stopToken.stop_requested() ? 1 : 0;
}

std::optional<std::int64_t> positiveDurationMicroseconds(std::int64_t value, AVRational timeBase) {
    if (value <= 0 || value == AV_NOPTS_VALUE) {
        return std::nullopt;
    }
    std::int64_t const converted = av_rescale_q(value, timeBase, AV_TIME_BASE_Q);
    return converted > 0 ? std::optional<std::int64_t>(converted) : std::nullopt;
}

std::optional<std::int64_t> normalizedTimestampMicroseconds(std::int64_t timestamp, AVRational timeBase,
                                                            std::optional<VideoTimelineOrigin> const& origin) {
    if (timestamp == AV_NOPTS_VALUE) {
        return std::nullopt;
    }
    std::int64_t const absolute = av_rescale_q(timestamp, timeBase, AV_TIME_BASE_Q);
    std::optional<std::int64_t> const originMicroseconds = origin ? origin->microseconds() : std::nullopt;
    return originMicroseconds ? checkedTimestampSubtract(absolute, *originMicroseconds) : absolute;
}

FfmpegVideoStreamDiagnostics videoDiagnostics(AVFormatContext const& formatContext, AVStream const& stream,
                                              AVCodec const& decoder, int streamIndex,
                                              FfmpegVideoDecodeRequest const& request,
                                              std::optional<VideoTimelineOrigin> origin) {
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
        .timelineOrigin = std::move(origin),
    };
    diagnostics.durationMicroseconds = ffmpegProvisionalDurationMicroseconds(formatContext, stream);
    AVRational const frameRate =
        av_guess_frame_rate(const_cast<AVFormatContext*>(&formatContext), const_cast<AVStream*>(&stream), nullptr);
    if (frameRate.num > 0 && frameRate.den > 0) {
        diagnostics.nominalFrameDurationMicroseconds = positiveDurationMicroseconds(1, av_inv_q(frameRate));
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

struct SubtitleWorkerStatus : WorkerStatus {};

QString metadataValue(AVDictionary const* metadata, char const* key) {
    AVDictionaryEntry const* entry = av_dict_get(metadata, key, nullptr, 0);
    return entry && entry->value ? QString::fromUtf8(entry->value) : QString{};
}

QString subtitleTrackLabel(AVStream const& stream, int ordinal) {
    QString const languageTag = metadataValue(stream.metadata, "language");
    QString language = languageTag;
    if (!languageTag.isEmpty()) {
        QLocale const locale(languageTag);
        if (locale.language() != QLocale::AnyLanguage && locale.language() != QLocale::C) {
            language = QLocale::languageToString(locale.language());
        }
    }
    QString const title = metadataValue(stream.metadata, "title");
    QString label;
    if (!language.isEmpty() && !title.isEmpty()) {
        label = language + QStringLiteral(" - ") + title;
    } else if (!title.isEmpty()) {
        label = title;
    } else if (!language.isEmpty()) {
        label = language;
    } else {
        label = QStringLiteral("Subtitle %1").arg(ordinal);
    }

    QStringList traits;
    if (stream.disposition & AV_DISPOSITION_DEFAULT) {
        traits.push_back(QStringLiteral("Default"));
    }
    if (stream.disposition & AV_DISPOSITION_FORCED) {
        traits.push_back(QStringLiteral("Forced"));
    }
    if (stream.disposition & AV_DISPOSITION_HEARING_IMPAIRED) {
        traits.push_back(QStringLiteral("SDH"));
    }
    if (stream.disposition & AV_DISPOSITION_COMMENT) {
        traits.push_back(QStringLiteral("Commentary"));
    }
    if (!traits.isEmpty()) {
        label += QStringLiteral(" (%1)").arg(traits.join(QStringLiteral(", ")));
    }
    return label;
}

std::vector<SubtitleTrackDescriptor> discoverSubtitleTracks(AVFormatContext const& formatContext) {
    std::vector<SubtitleTrackDescriptor> tracks;
    int ordinal = 0;
    for (unsigned int index = 0; index < formatContext.nb_streams; ++index) {
        AVStream const& stream = *formatContext.streams[index];
        if (stream.codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            continue;
        }
        ++ordinal;
        AVCodec const* decoder = avcodec_find_decoder(stream.codecpar->codec_id);
        tracks.push_back({
            .streamIndex = static_cast<int>(index),
            .label = subtitleTrackLabel(stream, ordinal),
            .language = metadataValue(stream.metadata, "language"),
            .title = metadataValue(stream.metadata, "title"),
            .codec = QString::fromLatin1(avcodec_get_name(stream.codecpar->codec_id)),
            .isDefault = static_cast<bool>(stream.disposition & AV_DISPOSITION_DEFAULT),
            .isForced = static_cast<bool>(stream.disposition & AV_DISPOSITION_FORCED),
            .isHearingImpaired = static_cast<bool>(stream.disposition & AV_DISPOSITION_HEARING_IMPAIRED),
            .isCommentary = static_cast<bool>(stream.disposition & AV_DISPOSITION_COMMENT),
            .supported = decoder != nullptr,
        });
    }
    return tracks;
}

std::vector<SubtitleFontAttachment> collectSubtitleFonts(AVFormatContext const& formatContext) {
    constexpr std::size_t maximumFonts = 64;
    constexpr std::size_t maximumFontBytes = 32U * 1024U * 1024U;
    std::vector<SubtitleFontAttachment> fonts;
    std::size_t retainedBytes = 0;
    for (unsigned int index = 0; index < formatContext.nb_streams && fonts.size() < maximumFonts; ++index) {
        AVStream const& stream = *formatContext.streams[index];
        if (stream.codecpar->codec_type != AVMEDIA_TYPE_ATTACHMENT ||
            (stream.codecpar->codec_id != AV_CODEC_ID_TTF && stream.codecpar->codec_id != AV_CODEC_ID_OTF) ||
            !stream.codecpar->extradata || stream.codecpar->extradata_size <= 0) {
            continue;
        }
        std::size_t const bytes = static_cast<std::size_t>(stream.codecpar->extradata_size);
        if (bytes > maximumFontBytes - retainedBytes) {
            continue;
        }
        QString name = metadataValue(stream.metadata, "filename");
        if (name.isEmpty()) {
            name = QStringLiteral("attachment-%1").arg(index);
        }
        fonts.push_back({
            .name = std::move(name),
            .bytes =
                QByteArray(reinterpret_cast<char const*>(stream.codecpar->extradata), stream.codecpar->extradata_size),
        });
        retainedBytes += bytes;
    }
    return fonts;
}

std::optional<std::int64_t> subtitleTime(std::int64_t pts, std::uint32_t offsetMilliseconds,
                                         std::optional<VideoTimelineOrigin> const& origin) {
    if (pts == AV_NOPTS_VALUE) {
        return std::nullopt;
    }
    std::optional<std::int64_t> const absolute =
        checkedTimestampAdd(pts, static_cast<std::int64_t>(offsetMilliseconds) * 1'000);
    if (!absolute) {
        return std::nullopt;
    }
    std::optional<std::int64_t> const originMicroseconds = origin ? origin->microseconds() : std::nullopt;
    std::optional<std::int64_t> const normalized =
        originMicroseconds ? checkedTimestampSubtract(*absolute, *originMicroseconds) : absolute;
    return normalized && *normalized >= 0 ? normalized : std::nullopt;
}

QByteArray escapedAssText(char const* text) {
    QByteArray escaped = text ? QByteArray(text) : QByteArray{};
    escaped.replace("\\", "\\\\");
    escaped.replace("{", "\\{");
    escaped.replace("}", "\\}");
    escaped.replace("\r\n", "\\N");
    escaped.replace("\n", "\\N");
    escaped.replace("\r", "\\N");
    return QByteArrayLiteral("0,0,Default,,0,0,0,,") + escaped;
}

std::shared_ptr<SubtitleBitmapComposition const> copyBitmapComposition(AVSubtitle const& subtitle,
                                                                       AVCodecContext const& codecContext,
                                                                       AVCodecParameters const& parameters,
                                                                       QSize const& fallbackCanvas, QString& error) {
    constexpr int maximumDimension = 16'384;
    constexpr std::size_t maximumRegions = 256;
    constexpr std::size_t maximumBytes = 128U * 1024U * 1024U;
    int const canvasWidth = codecContext.width > 0 ? codecContext.width
                            : parameters.width > 0 ? parameters.width
                                                   : fallbackCanvas.width();
    int const canvasHeight = codecContext.height > 0 ? codecContext.height
                             : parameters.height > 0 ? parameters.height
                                                     : fallbackCanvas.height();
    if (canvasWidth <= 0 || canvasHeight <= 0 || canvasWidth > maximumDimension || canvasHeight > maximumDimension) {
        error = QStringLiteral("Bitmap subtitle canvas is invalid");
        return {};
    }

    auto composition = std::make_shared<SubtitleBitmapComposition>();
    composition->canvasSize = {canvasWidth, canvasHeight};
    std::size_t retainedBytes = 0;
    for (unsigned int index = 0; index < subtitle.num_rects; ++index) {
        AVSubtitleRect const* rect = subtitle.rects[index];
        if (!rect || rect->type != SUBTITLE_BITMAP) {
            continue;
        }
        if (composition->regions.size() >= maximumRegions || rect->x < 0 || rect->y < 0 || rect->w <= 0 ||
            rect->h <= 0 || rect->w > maximumDimension || rect->h > maximumDimension || rect->x > canvasWidth ||
            rect->w > canvasWidth - rect->x || rect->y > canvasHeight || rect->h > canvasHeight - rect->y ||
            !rect->data[0] || !rect->data[1] || rect->linesize[0] < rect->w || rect->nb_colors <= 0 ||
            rect->nb_colors > 256) {
            error = QStringLiteral("Bitmap subtitle region is invalid");
            return {};
        }
        std::size_t const pixels = static_cast<std::size_t>(rect->w) * static_cast<std::size_t>(rect->h);
        if (pixels > (maximumBytes - retainedBytes) / 4U) {
            error = QStringLiteral("Bitmap subtitle exceeds its byte budget");
            return {};
        }
        QByteArray rgba(static_cast<qsizetype>(pixels * 4U), Qt::Uninitialized);
        auto const* palette = reinterpret_cast<std::uint32_t const*>(rect->data[1]);
        auto* output = reinterpret_cast<unsigned char*>(rgba.data());
        for (int y = 0; y < rect->h; ++y) {
            std::uint8_t const* row = rect->data[0] + static_cast<std::ptrdiff_t>(y) * rect->linesize[0];
            for (int x = 0; x < rect->w; ++x) {
                std::uint8_t const paletteIndex = row[x];
                if (paletteIndex >= rect->nb_colors) {
                    error = QStringLiteral("Bitmap subtitle palette index is invalid");
                    return {};
                }
                std::uint32_t const color = palette[paletteIndex];
                *output++ = static_cast<unsigned char>((color >> 16U) & 0xffU);
                *output++ = static_cast<unsigned char>((color >> 8U) & 0xffU);
                *output++ = static_cast<unsigned char>(color & 0xffU);
                *output++ = static_cast<unsigned char>((color >> 24U) & 0xffU);
            }
        }
        retainedBytes += pixels * 4U;
        composition->regions.push_back({
            .x = rect->x,
            .y = rect->y,
            .size = {rect->w, rect->h},
            .rgba = std::move(rgba),
        });
    }
    if (!composition->isValid()) {
        error = QStringLiteral("Bitmap subtitle composition is empty");
        return {};
    }
    return composition;
}

bool publishSubtitle(AVSubtitle const& subtitle, AVCodecContext const& codecContext,
                     AVCodecParameters const& parameters, bool bitmapCodec, QSize const& fallbackCanvas,
                     std::optional<VideoTimelineOrigin> const& origin, std::uint64_t playbackGeneration,
                     FfmpegSubtitleOutputSink const& sink, std::stop_token stopToken, QString& error) {
    std::optional<std::int64_t> const start = subtitleTime(subtitle.pts, subtitle.start_display_time, origin);
    if (!start) {
        return true;
    }
    std::optional<std::int64_t> end;
    if (subtitle.end_display_time != UINT32_MAX && subtitle.end_display_time > subtitle.start_display_time) {
        end = subtitleTime(subtitle.pts, subtitle.end_display_time, origin);
    }

    if (bitmapCodec && subtitle.num_rects == 0) {
        return sink.submit(
            {
                .playbackGeneration = playbackGeneration,
                .startMicroseconds = *start,
                .type = SubtitlePayloadType::Clear,
            },
            stopToken);
    }

    bool hasBitmap = false;
    for (unsigned int index = 0; index < subtitle.num_rects; ++index) {
        AVSubtitleRect const* rect = subtitle.rects[index];
        if (rect && rect->type == SUBTITLE_BITMAP) {
            hasBitmap = true;
            break;
        }
    }
    if (hasBitmap) {
        auto const composition = copyBitmapComposition(subtitle, codecContext, parameters, fallbackCanvas, error);
        if (!composition) {
            return false;
        }
        return sink.submit(
            {
                .playbackGeneration = playbackGeneration,
                .startMicroseconds = *start,
                .endMicroseconds = end,
                .type = SubtitlePayloadType::Bitmap,
                .bitmap = composition,
            },
            stopToken);
    }

    for (unsigned int index = 0; index < subtitle.num_rects; ++index) {
        AVSubtitleRect const* rect = subtitle.rects[index];
        if (!rect) {
            continue;
        }
        QByteArray ass;
        if (rect->type == SUBTITLE_ASS && rect->ass) {
            ass = rect->ass;
        } else if (rect->type == SUBTITLE_TEXT && rect->text) {
            ass = escapedAssText(rect->text);
        } else {
            continue;
        }
        if (!sink.submit(
                {
                    .playbackGeneration = playbackGeneration,
                    .startMicroseconds = *start,
                    .endMicroseconds = end,
                    .type = SubtitlePayloadType::AssText,
                    .ass = std::move(ass),
                },
                stopToken)) {
            return false;
        }
    }
    return true;
}

SubtitleWorkerStatus decodeSubtitlePackets(CodecContextPtr codecContext, AVCodecParameters const& parameters,
                                           bool bitmapCodec, QSize fallbackCanvas,
                                           std::optional<VideoTimelineOrigin> const& origin,
                                           std::uint64_t playbackGeneration, FfmpegPacketRouter& router,
                                           FfmpegSubtitleOutputSink const& sink, std::stop_token stopToken) {
    SubtitleWorkerStatus result;
    bool decoderFailed = false;
    std::uint64_t packetCount = 0;
    std::uint64_t outputCount = 0;
    while (!stopToken.stop_requested()) {
        FfmpegRoutedPacket input = router.pop(FfmpegPacketStream::Subtitle, stopToken);
        if (!input.packet) {
            result.cancelled = input.terminal == FfmpegPacketRouterTerminal::Cancelled;
            result.endOfStream = input.terminal == FfmpegPacketRouterTerminal::EndOfStream;
            if (input.terminal == FfmpegPacketRouterTerminal::Failed) {
                result.error = std::move(input.error);
            }
            break;
        }
        if (decoderFailed) {
            continue;
        }

        AVSubtitle subtitle{};
        int gotSubtitle = 0;
        int const decodeResult =
            avcodec_decode_subtitle2(codecContext.get(), &subtitle, &gotSubtitle, input.packet.get());
        ++packetCount;
        qCDebug(sunroomLogMediaDecode).noquote()
            << "event=subtitle.packet_decoded"
            << "generation=" + QString::number(playbackGeneration) << "packet=" + QString::number(packetCount)
            << "pts=" + QString::number(input.packet->pts) << "bytes=" + QString::number(input.packet->size)
            << "consumed=" + QString::number(decodeResult)
            << "output=" + QString(gotSubtitle ? QStringLiteral("true") : QStringLiteral("false"));
        if (decodeResult < 0) {
            result.error = QStringLiteral("Subtitle decode failed: %1").arg(ffmpegError(decodeResult));
            if (sink.failed) {
                sink.failed(result.error);
            }
            decoderFailed = true;
            continue;
        }
        if (!gotSubtitle) {
            continue;
        }
        ++outputCount;
        if (outputCount == 1) {
            qCInfo(sunroomLogMediaDecode).noquote()
                << "event=subtitle.first_output"
                << "generation=" + QString::number(playbackGeneration) << "packet=" + QString::number(packetCount)
                << "pts=" + QString::number(subtitle.pts) << "rects=" + QString::number(subtitle.num_rects);
        }
        bool const published = publishSubtitle(subtitle, *codecContext, parameters, bitmapCodec, fallbackCanvas, origin,
                                               playbackGeneration, sink, stopToken, result.error);
        avsubtitle_free(&subtitle);
        if (!published) {
            if (result.error.isEmpty() && !stopToken.stop_requested()) {
                result.error = QStringLiteral("Subtitle output rejected a decoded event");
            }
            if (!result.error.isEmpty() && sink.failed) {
                sink.failed(result.error);
            }
            decoderFailed = true;
        }
    }
    result.stopped = stopToken.stop_requested() && !result.cancelled;
    return result;
}

void stopSibling(FfmpegPacketRouter& router, std::stop_source& operationStop) {
    operationStop.request_stop();
    router.finish(FfmpegPacketRouterTerminal::Cancelled);
}

FfmpegVideoDecodeResult decodeVideoPackets(FfmpegMediaDecodeRequest const& request, AVCodecParameters const& parameters,
                                           AVRational streamTimeBase, AVRational streamAspectRatio,
                                           AVCodec const& decoder, FfmpegVideoStreamDiagnostics diagnostics,
                                           FfmpegPacketRouter& router, FfmpegVideoFrameSink const& sink,
                                           std::stop_source& operationStop) {
    std::stop_token const stopToken = operationStop.get_token();
    bool hardwareSelected = false;
    FfmpegVideoDecodeResult result = decodeFfmpegVideoPackets(
        request.video, decoder, parameters, {streamTimeBase.num, streamTimeBase.den},
        {streamAspectRatio.num, streamAspectRatio.den}, std::move(diagnostics),
        [&router](std::stop_token packetStopToken) {
            FfmpegRoutedPacket input = router.pop(FfmpegPacketStream::Video, packetStopToken);
            switch (input.terminal) {
            case FfmpegPacketRouterTerminal::Open:
                return FfmpegVideoPacketRead{
                    .packet = std::move(input.packet),
                };
            case FfmpegPacketRouterTerminal::EndOfStream:
                return FfmpegVideoPacketRead{
                    .terminal = FfmpegVideoPacketTerminal::EndOfStream,
                };
            case FfmpegPacketRouterTerminal::Failed:
                return FfmpegVideoPacketRead{
                    .terminal = FfmpegVideoPacketTerminal::Failed,
                    .error = std::move(input.error),
                };
            case FfmpegPacketRouterTerminal::Cancelled:
                return FfmpegVideoPacketRead{
                    .terminal = FfmpegVideoPacketTerminal::Cancelled,
                };
            }
            Q_UNREACHABLE_RETURN(FfmpegVideoPacketRead{});
        },
        sink, stopToken, &hardwareSelected);
    if (!result.error.isEmpty() || result.stopped) {
        stopSibling(router, operationStop);
    }
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
    AudioConverter(FfmpegMediaDecodeRequest const& request, AVCodecParameters const& parameters,
                   AVRational streamTimeBase, int streamIndex, AVCodec const& decoder,
                   std::optional<VideoTimelineOrigin> origin, FfmpegPcmAudioSink const& sink, std::stop_token stopToken)
        : m_request(request), m_streamTimeBase(streamTimeBase), m_origin(std::move(origin)), m_sink(sink),
          m_stopToken(stopToken) {
        m_diagnostics = {
            .decoderName = decoder.name ? QString::fromLatin1(decoder.name) : QStringLiteral("unknown"),
            .audioStreamIndex = streamIndex,
            .sourceSampleRate = parameters.sample_rate,
            .sourceChannelCount = parameters.ch_layout.nb_channels,
            .outputFormat = request.audioOutput,
            .timelineOrigin = m_origin,
        };
    }

    AudioConvertResult primeLeadingGap(std::optional<std::int64_t> expectedAudioStart, QString& error) {
        if (!expectedAudioStart) {
            return AudioConvertResult::Converted;
        }
        std::int64_t const playbackStart = m_request.video.start.targetPositionMicroseconds.value_or(0);
        // AVStream::start_time is the selected stream's declared first media
        // timestamp. Publishing that complete interval as source silence lets
        // bounded video and packet queues make progress without guessing a
        // fixed startup horizon. Decoded timestamps still validate the
        // boundary when the first real frame arrives.
        return *expectedAudioStart > playbackStart ? publishSilence(playbackStart, *expectedAudioStart, error)
                                                   : AudioConvertResult::Converted;
    }

    AudioConvertResult convert(AVFrame const& frame, QString& error) {
        if (!ensureContext(frame, error)) {
            return AudioConvertResult::Failed;
        }
        AVRational const sampleTimeBase{1, frame.sample_rate};
        std::int64_t const expectedInputSample = m_nextInputSample;
        std::optional<std::int64_t> const expectedMediaTime =
            normalizedTimestampMicroseconds(expectedInputSample, sampleTimeBase, m_origin);
        if (expectedInputSample != AV_NOPTS_VALUE && !expectedMediaTime) {
            error = QStringLiteral("Decoded audio timestamp range is not representable");
            return AudioConvertResult::Failed;
        }
        std::int64_t nextInputSample = m_nextInputSample;
        std::optional<std::int64_t> const frameStartSample = reconcileFrameStartSample(frame, nextInputSample, error);
        if (!error.isEmpty()) {
            return AudioConvertResult::Failed;
        }
        if (!frameStartSample) {
            error = QStringLiteral("The first decoded audio frame has no timestamp");
            return AudioConvertResult::Failed;
        }
        std::optional<std::int64_t> const frameTime =
            normalizedTimestampMicroseconds(*frameStartSample, sampleTimeBase, m_origin);
        if (!frameTime) {
            error = QStringLiteral("Decoded audio timestamp range is not representable");
            return AudioConvertResult::Failed;
        }
        if (!m_anchorMediaMicroseconds) {
            m_anchorMediaMicroseconds = *frameTime;
        }
        if (expectedMediaTime) {
            std::int64_t const toleranceSamples =
                std::max<std::int64_t>(1, av_rescale_q_rnd(1, m_streamTimeBase, sampleTimeBase, AV_ROUND_UP)) + 1;
            std::optional<std::int64_t> const differenceSamples =
                checkedTimestampSubtract(*frameStartSample, expectedInputSample);
            if (!differenceSamples || *differenceSamples < -toleranceSamples) {
                error = QStringLiteral("Decoded audio overlaps the previous timeline "
                                       "region (expected %1 us, got %2 us)")
                            .arg(*expectedMediaTime)
                            .arg(*frameTime);
                return AudioConvertResult::Failed;
            }
            if (*differenceSamples > toleranceSamples) {
                AudioConvertResult const gap = beginForwardGap(*frameTime, *expectedMediaTime, error);
                if (gap != AudioConvertResult::Converted) {
                    return gap;
                }
            }
        }
        m_nextInputSample = nextInputSample;

        std::int64_t const delay = swr_get_delay(m_swr.get(), frame.sample_rate);
        std::int64_t const capacity =
            av_rescale_rnd(delay + frame.nb_samples, m_request.audioOutput.sampleRate, frame.sample_rate, AV_ROUND_UP);
        if (capacity <= 0) {
            return AudioConvertResult::Converted;
        }
        std::vector<float> samples(static_cast<std::size_t>(capacity) * m_request.audioOutput.channelCount);
        std::uint8_t* output[] = {
            reinterpret_cast<std::uint8_t*>(samples.data()),
        };
        int const produced = swr_convert(m_swr.get(), output, static_cast<int>(capacity),
                                         const_cast<std::uint8_t const**>(frame.extended_data), frame.nb_samples);
        if (produced < 0) {
            error = QStringLiteral("Audio conversion failed: %1").arg(ffmpegError(produced));
            return AudioConvertResult::Failed;
        }
        samples.resize(static_cast<std::size_t>(produced) * m_request.audioOutput.channelCount);
        return publish(std::move(samples), produced, error);
    }

    AudioConvertResult flush(QString& error) {
        if (!m_swr) {
            return AudioConvertResult::Converted;
        }
        while (true) {
            std::int64_t const delay = swr_get_delay(m_swr.get(), m_inputSampleRate);
            if (delay <= 0) {
                return AudioConvertResult::Converted;
            }
            std::int64_t const capacity =
                av_rescale_rnd(delay, m_request.audioOutput.sampleRate, m_inputSampleRate, AV_ROUND_UP);
            if (capacity <= 0) {
                return AudioConvertResult::Converted;
            }
            std::vector<float> samples(static_cast<std::size_t>(capacity) * m_request.audioOutput.channelCount);
            std::uint8_t* output[] = {
                reinterpret_cast<std::uint8_t*>(samples.data()),
            };
            int const produced = swr_convert(m_swr.get(), output, static_cast<int>(capacity), nullptr, 0);
            if (produced < 0) {
                error = QStringLiteral("Could not flush audio converter: %1").arg(ffmpegError(produced));
                return AudioConvertResult::Failed;
            }
            if (produced == 0) {
                return AudioConvertResult::Converted;
            }
            samples.resize(static_cast<std::size_t>(produced) * m_request.audioOutput.channelCount);
            AudioConvertResult const published = publish(std::move(samples), produced, error);
            if (published != AudioConvertResult::Converted) {
                return published;
            }
        }
    }

    FfmpegAudioStreamDiagnostics const& diagnostics() const { return m_diagnostics; }

    std::uint64_t outputFrames() const { return m_streamFrameIndex; }

    std::optional<std::int64_t> observedEndMicroseconds() const { return m_observedEndMicroseconds; }

  private:
    std::optional<std::int64_t> reconcileFrameStartSample(AVFrame const& frame, std::int64_t& nextInputSample,
                                                          QString& error) const {
        if (frame.nb_samples <= 0) {
            error = QStringLiteral("The decoded audio frame has no samples");
            return std::nullopt;
        }
        if (frame.best_effort_timestamp != AV_NOPTS_VALUE) {
            AVRational const sampleTimeBase{1, frame.sample_rate};
            return av_rescale_delta(m_streamTimeBase, frame.best_effort_timestamp, sampleTimeBase, frame.nb_samples,
                                    &nextInputSample, sampleTimeBase);
        }
        if (nextInputSample == AV_NOPTS_VALUE) {
            return std::nullopt;
        }
        std::int64_t const frameStartSample = nextInputSample;
        std::optional<std::int64_t> const next = checkedTimestampAdd(frameStartSample, frame.nb_samples);
        if (!next) {
            error = QStringLiteral("Decoded audio sample range is not representable");
            return std::nullopt;
        }
        nextInputSample = *next;
        return frameStartSample;
    }

    AudioConvertResult beginForwardGap(std::int64_t nextFrameTimeMicroseconds,
                                       std::int64_t expectedFrameTimeMicroseconds, QString& error) {
        AudioConvertResult const flushed = flush(error);
        if (flushed != AudioConvertResult::Converted) {
            return flushed;
        }

        std::int64_t const playbackStart = m_request.video.start.targetPositionMicroseconds.value_or(0);
        std::int64_t const silenceStart =
            std::max(playbackStart, m_observedEndMicroseconds.value_or(expectedFrameTimeMicroseconds));
        AudioConvertResult const silence = publishSilence(silenceStart, nextFrameTimeMicroseconds, error);
        if (silence != AudioConvertResult::Converted) {
            return silence;
        }

        swr_close(m_swr.get());
        if (swr_init(m_swr.get()) < 0) {
            error = QStringLiteral("Could not reset audio conversion after a timestamp gap");
            return AudioConvertResult::Failed;
        }
        m_anchorMediaMicroseconds = nextFrameTimeMicroseconds;
        m_convertedFrames = 0;
        return AudioConvertResult::Converted;
    }

    bool ensureContext(AVFrame const& frame, QString& error) {
        if (m_swr) {
            if (frame.sample_rate != m_inputSampleRate || frame.format != m_inputSampleFormat ||
                av_channel_layout_compare(&frame.ch_layout, &m_inputLayout) != 0) {
                error = QStringLiteral("The selected audio stream changed format");
                return false;
            }
            return true;
        }
        if (frame.sample_rate <= 0 || frame.ch_layout.nb_channels <= 0 || frame.format == AV_SAMPLE_FMT_NONE) {
            error = QStringLiteral("Decoded audio format is incomplete");
            return false;
        }
        AVChannelLayout outputLayout = AV_CHANNEL_LAYOUT_STEREO;
        SwrContext* raw = nullptr;
        int const status = swr_alloc_set_opts2(&raw, &outputLayout, AV_SAMPLE_FMT_FLT, m_request.audioOutput.sampleRate,
                                               &frame.ch_layout, static_cast<AVSampleFormat>(frame.format),
                                               frame.sample_rate, 0, nullptr);
        if (status < 0 || !raw) {
            if (raw) {
                swr_free(&raw);
            }
            error = QStringLiteral("Could not configure audio conversion: %1").arg(ffmpegError(status));
            return false;
        }
        m_swr.reset(raw);
        if (swr_init(m_swr.get()) < 0) {
            error = QStringLiteral("Could not initialize audio conversion");
            m_swr.reset();
            return false;
        }
        m_inputSampleRate = frame.sample_rate;
        m_inputSampleFormat = frame.format;
        if (av_channel_layout_copy(&m_inputLayout, &frame.ch_layout) < 0) {
            error = QStringLiteral("Could not retain the decoded channel layout");
            m_swr.reset();
            return false;
        }
        m_inputLayoutInitialized = true;
        m_diagnostics.sourceSampleRate = frame.sample_rate;
        m_diagnostics.sourceChannelCount = frame.ch_layout.nb_channels;
        return true;
    }

    AudioConvertResult publish(std::vector<float> samples, int producedFrames, QString& error) {
        if (producedFrames == 0) {
            return AudioConvertResult::Converted;
        }
        Q_ASSERT(m_anchorMediaMicroseconds);
        if (m_convertedFrames > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            error = QStringLiteral("Converted audio frame index is not representable");
            return AudioConvertResult::Failed;
        }
        std::optional<std::int64_t> const blockStart = checkedTimestampAdd(
            *m_anchorMediaMicroseconds, av_rescale_q(static_cast<std::int64_t>(m_convertedFrames),
                                                     {1, m_request.audioOutput.sampleRate}, AV_TIME_BASE_Q));
        if (!blockStart) {
            error = QStringLiteral("Converted audio timestamp range is not representable");
            return AudioConvertResult::Failed;
        }
        m_convertedFrames += static_cast<std::uint64_t>(producedFrames);

        std::size_t trimFrames = 0;
        if (m_request.video.start.targetPositionMicroseconds) {
            std::int64_t const target = *m_request.video.start.targetPositionMicroseconds;
            if (*blockStart < target) {
                std::optional<std::int64_t> const difference = checkedTimestampSubtract(target, *blockStart);
                if (!difference) {
                    error = QStringLiteral("Audio seek trim range is not representable");
                    return AudioConvertResult::Failed;
                }
                std::int64_t const required =
                    av_rescale_q_rnd(*difference, AV_TIME_BASE_Q, {1, m_request.audioOutput.sampleRate}, AV_ROUND_UP);
                trimFrames = std::min<std::size_t>(producedFrames,
                                                   static_cast<std::size_t>(std::max<std::int64_t>(0, required)));
            }
        }
        std::size_t const keptFrames = static_cast<std::size_t>(producedFrames) - trimFrames;
        if (keptFrames == 0) {
            return AudioConvertResult::Converted;
        }
        std::size_t const channels = static_cast<std::size_t>(m_request.audioOutput.channelCount);
        if (trimFrames != 0) {
            samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(trimFrames * channels));
        }
        std::optional<std::int64_t> const mediaStart =
            checkedTimestampAdd(*blockStart, av_rescale_q(static_cast<std::int64_t>(trimFrames),
                                                          {1, m_request.audioOutput.sampleRate}, AV_TIME_BASE_Q));
        if (!mediaStart) {
            error = QStringLiteral("Trimmed audio timestamp range is not representable");
            return AudioConvertResult::Failed;
        }
        std::int64_t const playbackStart = m_request.video.start.targetPositionMicroseconds.value_or(0);
        if (!m_outputAnchorMediaMicroseconds && *mediaStart > playbackStart) {
            AudioConvertResult const gap = publishSilence(playbackStart, *mediaStart, error);
            if (gap != AudioConvertResult::Converted) {
                return gap;
            }
        }
        return submitSamples(*mediaStart, std::move(samples), keptFrames, error);
    }

    AudioConvertResult publishSilence(std::int64_t startMicroseconds, std::int64_t endMicroseconds, QString& error) {
        startMicroseconds = std::max(startMicroseconds, m_request.video.start.targetPositionMicroseconds.value_or(0));
        if (endMicroseconds <= startMicroseconds) {
            return AudioConvertResult::Converted;
        }
        std::int64_t const frameCount = av_rescale_q_rnd(endMicroseconds - startMicroseconds, AV_TIME_BASE_Q,
                                                         {1, m_request.audioOutput.sampleRate}, AV_ROUND_DOWN);
        if (frameCount <= 0) {
            return AudioConvertResult::Converted;
        }
        constexpr std::size_t maximumBlockFrames = 4'096;
        std::uint64_t remaining = static_cast<std::uint64_t>(frameCount);
        while (remaining != 0) {
            if (m_stopToken.stop_requested()) {
                return AudioConvertResult::Cancelled;
            }
            std::size_t const frames = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, maximumBlockFrames));
            std::optional<std::int64_t> const blockStart = checkedTimestampAdd(
                startMicroseconds,
                av_rescale_q(static_cast<std::int64_t>(static_cast<std::uint64_t>(frameCount) - remaining),
                             {1, m_request.audioOutput.sampleRate}, AV_TIME_BASE_Q));
            if (!blockStart) {
                error = QStringLiteral("Leading audio gap is not representable");
                return AudioConvertResult::Failed;
            }
            std::vector<float> silence(frames * static_cast<std::size_t>(m_request.audioOutput.channelCount), 0.0F);
            AudioConvertResult const submitted = submitSamples(*blockStart, std::move(silence), frames, error);
            if (submitted != AudioConvertResult::Converted) {
                return submitted;
            }
            remaining -= frames;
        }
        return AudioConvertResult::Converted;
    }

    AudioConvertResult submitSamples(std::int64_t mediaStartMicroseconds, std::vector<float> samples,
                                     std::size_t frameCount, QString& error) {
        if (!m_outputAnchorMediaMicroseconds) {
            m_outputAnchorMediaMicroseconds = mediaStartMicroseconds;
        } else {
            std::optional<std::int64_t> const expected = checkedTimestampAdd(
                *m_outputAnchorMediaMicroseconds, av_rescale_q(static_cast<std::int64_t>(m_streamFrameIndex),
                                                               {1, m_request.audioOutput.sampleRate}, AV_TIME_BASE_Q));
            if (!expected) {
                error = QStringLiteral("Audio output timeline is not representable");
                return AudioConvertResult::Failed;
            }
            std::int64_t const oneFrameMicroseconds =
                std::max<std::int64_t>(1, av_rescale_q(1, {1, m_request.audioOutput.sampleRate}, AV_TIME_BASE_Q));
            std::optional<std::int64_t> const difference = checkedTimestampSubtract(mediaStartMicroseconds, *expected);
            if (!difference || *difference < -oneFrameMicroseconds) {
                error = QStringLiteral("Decoded audio overlaps the published output timeline");
                return AudioConvertResult::Failed;
            }
            if (*difference > oneFrameMicroseconds) {
                AudioConvertResult const gap = publishSilence(*expected, mediaStartMicroseconds, error);
                if (gap != AudioConvertResult::Converted) {
                    return gap;
                }
                return submitSamples(mediaStartMicroseconds, std::move(samples), frameCount, error);
            }
            mediaStartMicroseconds = *expected;
        }
        m_observedEndMicroseconds = checkedTimestampAdd(
            mediaStartMicroseconds,
            av_rescale_q(static_cast<std::int64_t>(frameCount), {1, m_request.audioOutput.sampleRate}, AV_TIME_BASE_Q));
        if (!m_observedEndMicroseconds) {
            error = QStringLiteral("Audio endpoint is not representable");
            return AudioConvertResult::Failed;
        }
        PcmAudioBlock block{
            .playbackGeneration = requestGeneration(),
            .streamFrameIndex = m_streamFrameIndex,
            .mediaStartMicroseconds = mediaStartMicroseconds,
            .format = m_request.audioOutput,
            .samples = std::move(samples),
        };
        if (!m_sink(std::move(block), m_diagnostics, m_stopToken)) {
            return m_stopToken.stop_requested() ? AudioConvertResult::Cancelled : AudioConvertResult::Stopped;
        }
        m_streamFrameIndex += frameCount;
        return AudioConvertResult::Converted;
    }

    std::uint64_t requestGeneration() const { return m_request.video.firstFrameIdentity.playbackGeneration; }

  public:
    ~AudioConverter() {
        if (m_inputLayoutInitialized) {
            av_channel_layout_uninit(&m_inputLayout);
        }
    }

  private:
    FfmpegMediaDecodeRequest const& m_request;
    AVRational m_streamTimeBase;
    std::optional<VideoTimelineOrigin> m_origin;
    FfmpegPcmAudioSink const& m_sink;
    std::stop_token m_stopToken;
    FfmpegAudioStreamDiagnostics m_diagnostics;
    SwrContextPtr m_swr;
    AVChannelLayout m_inputLayout{};
    bool m_inputLayoutInitialized = false;
    int m_inputSampleRate = 0;
    int m_inputSampleFormat = AV_SAMPLE_FMT_NONE;
    std::int64_t m_nextInputSample = AV_NOPTS_VALUE;
    std::optional<std::int64_t> m_anchorMediaMicroseconds;
    std::uint64_t m_convertedFrames = 0;
    std::uint64_t m_streamFrameIndex = 0;
    std::optional<std::int64_t> m_outputAnchorMediaMicroseconds;
    std::optional<std::int64_t> m_observedEndMicroseconds;
};

AudioWorkerStatus decodeAudioPackets(FfmpegMediaDecodeRequest const& request, AVCodecParameters const& parameters,
                                     AVRational streamTimeBase, int streamIndex, AVCodec const& decoder,
                                     std::optional<VideoTimelineOrigin> origin,
                                     std::optional<std::int64_t> expectedAudioStartMicroseconds,
                                     FfmpegPacketRouter& router, FfmpegPcmAudioSink const& sink,
                                     std::stop_source& operationStop) {
    AudioWorkerStatus result;
    std::stop_token const stopToken = operationStop.get_token();
    auto const fail = [&](QString error) {
        result.error = std::move(error);
        stopSibling(router, operationStop);
        return result;
    };

    CodecContextPtr codecContext(avcodec_alloc_context3(&decoder));
    if (!codecContext) {
        return fail(QStringLiteral("Could not allocate audio decoder"));
    }
    int status = avcodec_parameters_to_context(codecContext.get(), &parameters);
    if (status < 0) {
        return fail(QStringLiteral("Could not configure audio decoder: %1").arg(ffmpegError(status)));
    }
    codecContext->pkt_timebase = streamTimeBase;
    status = avcodec_open2(codecContext.get(), &decoder, nullptr);
    if (status < 0) {
        return fail(QStringLiteral("Could not open audio decoder %1: %2")
                        .arg(QString::fromLatin1(decoder.name), ffmpegError(status)));
    }
    FramePtr frame(av_frame_alloc());
    if (!frame) {
        return fail(QStringLiteral("Could not allocate audio frame"));
    }
    AudioConverter converter(request, parameters, streamTimeBase, streamIndex, decoder, std::move(origin), sink,
                             stopToken);
    QString primeError;
    AudioConvertResult const primed = converter.primeLeadingGap(expectedAudioStartMicroseconds, primeError);
    if (primed != AudioConvertResult::Converted) {
        if (primed == AudioConvertResult::Cancelled) {
            result.cancelled = true;
            return result;
        }
        if (primed == AudioConvertResult::Stopped) {
            result.stopped = true;
            result.diagnostics = converter.diagnostics();
            result.outputFrames = converter.outputFrames();
            result.observedEndMicroseconds = converter.observedEndMicroseconds();
            stopSibling(router, operationStop);
            return result;
        }
        return fail(primeError);
    }
    enum class Drain {
        NeedInput,
        Drained,
        Failed,
        Cancelled,
    };
    auto const drain = [&](bool flushing) -> Drain {
        while (true) {
            if (stopToken.stop_requested()) {
                return Drain::Cancelled;
            }
            int const receive = avcodec_receive_frame(codecContext.get(), frame.get());
            if (receive == AVERROR(EAGAIN)) {
                return flushing ? Drain::Failed : Drain::NeedInput;
            }
            if (receive == AVERROR_EOF) {
                return Drain::Drained;
            }
            if (receive < 0) {
                result.error = QStringLiteral("Audio decoding failed: %1").arg(ffmpegError(receive));
                return Drain::Failed;
            }
            ++result.decodedFrames;
            QString conversionError;
            AudioConvertResult const converted = converter.convert(*frame, conversionError);
            av_frame_unref(frame.get());
            if (converted != AudioConvertResult::Converted) {
                if (converted == AudioConvertResult::Stopped) {
                    result.stopped = true;
                } else if (converted == AudioConvertResult::Cancelled) {
                    return Drain::Cancelled;
                } else {
                    result.error = conversionError;
                }
                return Drain::Failed;
            }
        }
    };

    while (!stopToken.stop_requested()) {
        FfmpegRoutedPacket input = router.pop(FfmpegPacketStream::Audio, stopToken);
        if (input.terminal == FfmpegPacketRouterTerminal::Cancelled) {
            result.cancelled = true;
            return result;
        }
        if (input.terminal == FfmpegPacketRouterTerminal::Failed) {
            return fail(input.error);
        }
        bool const flushing = input.terminal == FfmpegPacketRouterTerminal::EndOfStream;
        while (true) {
            int const sent = avcodec_send_packet(codecContext.get(), flushing ? nullptr : input.packet.get());
            if (sent != AVERROR(EAGAIN)) {
                if (sent < 0 && sent != AVERROR_EOF) {
                    return fail(QStringLiteral("Could not submit audio packet: %1").arg(ffmpegError(sent)));
                }
                break;
            }
            std::uint64_t const before = result.decodedFrames;
            Drain const progress = drain(false);
            if (progress == Drain::Cancelled) {
                result.cancelled = true;
                return result;
            }
            if (progress == Drain::Failed) {
                if (result.stopped) {
                    result.diagnostics = converter.diagnostics();
                    result.outputFrames = converter.outputFrames();
                    result.observedEndMicroseconds = converter.observedEndMicroseconds();
                    stopSibling(router, operationStop);
                    return result;
                }
                return fail(result.error);
            }
            if (progress == Drain::NeedInput && result.decodedFrames == before) {
                return fail(QStringLiteral("Audio decoder returned EAGAIN from both "
                                           "send and receive without progress"));
            }
        }
        Drain const progress = drain(flushing);
        if (progress == Drain::Cancelled) {
            result.cancelled = true;
            return result;
        }
        if (progress == Drain::Failed) {
            if (result.stopped) {
                result.diagnostics = converter.diagnostics();
                result.outputFrames = converter.outputFrames();
                result.observedEndMicroseconds = converter.observedEndMicroseconds();
                stopSibling(router, operationStop);
                return result;
            }
            return fail(result.error.isEmpty() ? QStringLiteral("Audio decoder did not drain at end of stream")
                                               : result.error);
        }
        if (flushing || progress == Drain::Drained) {
            QString flushError;
            AudioConvertResult const flushed = converter.flush(flushError);
            if (flushed != AudioConvertResult::Converted) {
                if (flushed == AudioConvertResult::Stopped) {
                    result.stopped = true;
                    result.diagnostics = converter.diagnostics();
                    result.outputFrames = converter.outputFrames();
                    result.observedEndMicroseconds = converter.observedEndMicroseconds();
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
            result.observedEndMicroseconds = converter.observedEndMicroseconds();
            result.endOfStream = true;
            return result;
        }
    }
    result.cancelled = true;
    return result;
}
} // namespace

bool FfmpegAudioStreamDiagnostics::isValid() const {
    return !decoderName.isEmpty() && audioStreamIndex >= 0 && sourceSampleRate > 0 && sourceChannelCount > 0 &&
           outputFormat.isValid() && (!timelineOrigin || timelineOrigin->isValid());
}

bool FfmpegAudioOutputSink::isValid() const { return static_cast<bool>(submit) && static_cast<bool>(endOfStream); }

bool FfmpegMediaDecodeRequest::isValid() const {
    return video.isValid() && (!decodeSelectedAudio || audioOutput == AudioStreamFormat{48'000, 2}) &&
           selectedSubtitleStreamIndex >= -1;
}

bool FfmpegSubtitleOutputSink::isValid() const { return static_cast<bool>(submit); }

bool FfmpegMediaDecodeResult::isSuccess() const {
    return video.isSuccess() && error.isEmpty() && !cancelled && !video.stopped && !audioStopped &&
           (!audioStreamPresent || (audio && audio->isValid() && audioEndOfStream));
}

bool FfmpegMediaDecodeResult::isCancelled() const { return cancelled && error.isEmpty() && video.isCancelled(); }

bool FfmpegMediaDecodeResult::isStopped() const {
    return error.isEmpty() && !cancelled && (video.stopped || audioStopped);
}

FfmpegMediaDecodeResult decodeMediaFrames(FfmpegMediaDecodeRequest const& request,
                                          FfmpegVideoFrameSink const& videoSink, FfmpegPcmAudioSink const& audioSink,
                                          std::stop_token stopToken) {
    return decodeMediaFrames(request, videoSink,
                             FfmpegAudioOutputSink{
                                 .submit = audioSink,
                                 .endOfStream = [](std::uint64_t) {},
                             },
                             {}, stopToken);
}

FfmpegMediaDecodeResult decodeMediaFrames(FfmpegMediaDecodeRequest const& request,
                                          FfmpegVideoFrameSink const& videoSink, FfmpegAudioOutputSink const& audioSink,
                                          FfmpegMediaStreamSink const& streamSink, std::stop_token stopToken) {
    return decodeMediaFrames(request, videoSink, audioSink, streamSink, {}, stopToken);
}

FfmpegMediaDecodeResult decodeMediaFrames(FfmpegMediaDecodeRequest const& request,
                                          FfmpegVideoFrameSink const& videoSink, FfmpegAudioOutputSink const& audioSink,
                                          FfmpegMediaStreamSink const& streamSink,
                                          FfmpegSubtitleOutputSink const& subtitleSink, std::stop_token stopToken) {
    FfmpegMediaDecodeResult result;
    auto const fail = [&](QString error) {
        result.error = std::move(error);
        result.video.error = result.error;
        return result;
    };
    if (!request.isValid() || !videoSink || (request.decodeSelectedAudio && !audioSink.isValid()) ||
        (request.selectedSubtitleStreamIndex >= 0 && !subtitleSink.isValid())) {
        return fail(QStringLiteral("Media decode request is invalid"));
    }

    std::stop_source operationStop;
    std::stop_callback externalStop(stopToken, [&] { operationStop.request_stop(); });
    InterruptState interrupt{operationStop.get_token()};
    auto const cancel = [&] {
        result.error.clear();
        result.cancelled = true;
        result.video.error.clear();
        result.video.cancelled = true;
        return result;
    };
    if (stopToken.stop_requested()) {
        return cancel();
    }

    AVFormatContext* rawFormat = avformat_alloc_context();
    if (!rawFormat) {
        return fail(QStringLiteral("Could not allocate media context"));
    }
    rawFormat->interrupt_callback = {
        interruptFfmpeg,
        &interrupt,
    };
    QByteArray const path = request.video.path.toUtf8();
    int status = avformat_open_input(&rawFormat, path.constData(), nullptr, nullptr);
    if (status < 0) {
        if (rawFormat) {
            avformat_close_input(&rawFormat);
        }
        if (stopToken.stop_requested()) {
            return cancel();
        }
        return fail(QStringLiteral("Could not open media: %1").arg(ffmpegError(status)));
    }
    FormatContextPtr formatContext(rawFormat);
    status = avformat_find_stream_info(formatContext.get(), nullptr);
    if (status < 0) {
        if (stopToken.stop_requested()) {
            return cancel();
        }
        return fail(QStringLiteral("Could not discover media streams: %1").arg(ffmpegError(status)));
    }

    AVCodec const* videoDecoder = nullptr;
    int const videoIndex = av_find_best_stream(formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &videoDecoder, 0);
    if (videoIndex < 0 || !videoDecoder) {
        return fail(QStringLiteral("Could not select video stream: %1").arg(ffmpegError(videoIndex)));
    }
    AVStream& videoStream = *formatContext->streams[videoIndex];

    AVCodec const* audioDecoder = nullptr;
    int audioIndex = -1;
    AVStream* audioStream = nullptr;
    if (request.decodeSelectedAudio) {
        audioIndex = av_find_best_stream(formatContext.get(), AVMEDIA_TYPE_AUDIO, -1, videoIndex, &audioDecoder, 0);
        if (audioIndex >= 0 && audioDecoder) {
            audioStream = formatContext->streams[audioIndex];
            result.audioStreamPresent = true;
        } else if (audioIndex != AVERROR_STREAM_NOT_FOUND) {
            return fail(QStringLiteral("Could not select audio stream: %1").arg(ffmpegError(audioIndex)));
        }
    }

    std::vector<SubtitleTrackDescriptor> const subtitleTracks = discoverSubtitleTracks(*formatContext);
    AVCodec const* subtitleDecoder = nullptr;
    AVStream* subtitleStream = nullptr;
    int subtitleIndex = -1;
    if (request.selectedSubtitleStreamIndex >= 0) {
        auto const selected =
            std::find_if(subtitleTracks.begin(), subtitleTracks.end(), [&](SubtitleTrackDescriptor const& track) {
                return track.streamIndex == request.selectedSubtitleStreamIndex;
            });
        if (selected == subtitleTracks.end()) {
            result.subtitleError = QStringLiteral("The selected subtitle track is unavailable");
        } else if (!selected->supported) {
            result.subtitleError = QStringLiteral("The selected subtitle codec is unsupported");
        } else {
            subtitleIndex = selected->streamIndex;
            subtitleStream = formatContext->streams[subtitleIndex];
            subtitleDecoder = avcodec_find_decoder(subtitleStream->codecpar->codec_id);
        }
    }

    std::optional<VideoTimelineOrigin> const origin =
        ffmpegSharedTimelineOrigin(*formatContext, videoStream, audioStream, request.video.start.timelineOrigin);
    if ((request.video.start.targetPositionMicroseconds || audioStream) && !origin) {
        return fail(QStringLiteral("The synchronized media timeline has no stable origin"));
    }
    std::optional<std::int64_t> const audioStartMicroseconds =
        audioStream ? normalizedTimestampMicroseconds(audioStream->start_time, audioStream->time_base, origin)
                    : std::nullopt;
    std::optional<std::int64_t> const audioEndMicroseconds =
        audioStream ? ffmpegDeclaredStreamEndMicroseconds(*formatContext, *audioStream, origin) : std::nullopt;
    std::int64_t const requestedStartMicroseconds = request.video.start.targetPositionMicroseconds.value_or(0);
    bool const audioOutputExpected =
        audioStream && (!audioEndMicroseconds || requestedStartMicroseconds < *audioEndMicroseconds);

    if (request.video.start.performDemuxSeek) {
        if (!formatContext->pb || !(formatContext->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
            return fail(QStringLiteral("The selected media source is not seekable"));
        }
        auto const targetTimestamp =
            videoStreamTimestampForPosition(*origin, {videoStream.time_base.num, videoStream.time_base.den},
                                            *request.video.start.targetPositionMicroseconds);
        if (!targetTimestamp) {
            return fail(QStringLiteral("The requested media seek cannot be represented"));
        }
        std::int64_t const seekTimestamp =
            *targetTimestamp > std::numeric_limits<std::int64_t>::min() ? *targetTimestamp - 1 : *targetTimestamp;
        status = avformat_seek_file(formatContext.get(), videoIndex, std::numeric_limits<std::int64_t>::min(),
                                    seekTimestamp, seekTimestamp, 0);
        if (status < 0) {
            if (stopToken.stop_requested()) {
                return cancel();
            }
            return fail(QStringLiteral("Could not seek media: %1").arg(ffmpegError(status)));
        }
    }

    for (unsigned int index = 0; index < formatContext->nb_streams; ++index) {
        formatContext->streams[index]->discard = static_cast<int>(index) == videoIndex ||
                                                         static_cast<int>(index) == audioIndex ||
                                                         static_cast<int>(index) == subtitleIndex
                                                     ? AVDISCARD_DEFAULT
                                                     : AVDISCARD_ALL;
    }

    CodecParametersPtr videoParameters(avcodec_parameters_alloc());
    if (!videoParameters || avcodec_parameters_copy(videoParameters.get(), videoStream.codecpar) < 0) {
        return fail(QStringLiteral("Could not retain video stream parameters"));
    }
    AVRational const videoTimeBase = videoStream.time_base;
    AVRational videoAspectRatio = videoStream.sample_aspect_ratio;
    if ((videoAspectRatio.num <= 0 || videoAspectRatio.den <= 0) && videoStream.codecpar->sample_aspect_ratio.num > 0 &&
        videoStream.codecpar->sample_aspect_ratio.den > 0) {
        videoAspectRatio = videoStream.codecpar->sample_aspect_ratio;
    }
    FfmpegVideoStreamDiagnostics initialVideoDiagnostics =
        videoDiagnostics(*formatContext, videoStream, *videoDecoder, videoIndex, request.video, origin);

    CodecParametersPtr audioParameters;
    AVRational audioTimeBase{};
    if (audioStream) {
        audioParameters.reset(avcodec_parameters_alloc());
        if (!audioParameters || avcodec_parameters_copy(audioParameters.get(), audioStream->codecpar) < 0) {
            return fail(QStringLiteral("Could not retain audio stream parameters"));
        }
        audioTimeBase = audioStream->time_base;
    }

    CodecParametersPtr subtitleParameters;
    CodecContextPtr subtitleCodecContext;
    std::optional<SubtitleStreamConfiguration> subtitleConfiguration;
    bool bitmapSubtitleCodec = false;
    QSize const subtitleFallbackCanvas{
        videoParameters->width,
        videoParameters->height,
    };
    if (subtitleStream && subtitleDecoder) {
        constexpr int maximumCodecPrivateBytes = 4 * 1024 * 1024;
        subtitleParameters.reset(avcodec_parameters_alloc());
        if (!subtitleParameters || avcodec_parameters_copy(subtitleParameters.get(), subtitleStream->codecpar) < 0) {
            result.subtitleError = QStringLiteral("Could not retain subtitle stream parameters");
        } else {
            subtitleCodecContext.reset(avcodec_alloc_context3(subtitleDecoder));
            status = subtitleCodecContext
                         ? avcodec_parameters_to_context(subtitleCodecContext.get(), subtitleParameters.get())
                         : AVERROR(ENOMEM);
            if (status >= 0) {
                subtitleCodecContext->pkt_timebase = subtitleStream->time_base;
                status = avcodec_open2(subtitleCodecContext.get(), subtitleDecoder, nullptr);
            }
            if (status < 0) {
                result.subtitleError = QStringLiteral("Could not open subtitle decoder: %1").arg(ffmpegError(status));
                subtitleCodecContext.reset();
            } else if (subtitleCodecContext->subtitle_header_size > maximumCodecPrivateBytes) {
                result.subtitleError = QStringLiteral("Subtitle codec private data exceeds its budget");
                subtitleCodecContext.reset();
            } else {
                AVCodecDescriptor const* descriptor = avcodec_descriptor_get(subtitleParameters->codec_id);
                bitmapSubtitleCodec = descriptor && (descriptor->props & AV_CODEC_PROP_BITMAP_SUB);
                subtitleConfiguration = SubtitleStreamConfiguration{
                    .playbackGeneration = request.video.firstFrameIdentity.playbackGeneration,
                    .streamIndex = subtitleIndex,
                    .codec = QString::fromLatin1(avcodec_get_name(subtitleParameters->codec_id)),
                    .codecPrivate = QByteArray(reinterpret_cast<char const*>(subtitleCodecContext->subtitle_header),
                                               subtitleCodecContext->subtitle_header_size),
                    .canvasSize =
                        {
                            subtitleParameters->width > 0 ? subtitleParameters->width : subtitleFallbackCanvas.width(),
                            subtitleParameters->height > 0 ? subtitleParameters->height
                                                           : subtitleFallbackCanvas.height(),
                        },
                    .fonts = collectSubtitleFonts(*formatContext),
                };
            }
        }
        if (!result.subtitleError.isEmpty()) {
            subtitleIndex = -1;
            subtitleStream = nullptr;
            if (subtitleSink.failed) {
                subtitleSink.failed(result.subtitleError);
            }
        }
    }

    if (streamSink) {
        streamSink({
            .audioStreamPresent = audioStream != nullptr,
            .audioOutputExpected = audioOutputExpected,
            .videoDiagnostics = initialVideoDiagnostics,
            .subtitleTracks = subtitleTracks,
            .subtitleConfiguration = subtitleConfiguration,
        });
    }
    if (operationStop.stop_requested()) {
        return cancel();
    }

    FfmpegPacketRouter router;
    FfmpegVideoDecodeResult videoResult;
    AudioWorkerStatus audioResult;
    SubtitleWorkerStatus subtitleResult;
    std::jthread videoWorker([&] {
        videoResult = decodeVideoPackets(request, *videoParameters, videoTimeBase, videoAspectRatio, *videoDecoder,
                                         initialVideoDiagnostics, router, videoSink, operationStop);
    });
    std::optional<std::jthread> audioWorker;
    if (audioStream) {
        audioWorker.emplace([&] {
            audioResult = decodeAudioPackets(request, *audioParameters, audioTimeBase, audioIndex, *audioDecoder,
                                             origin, audioStartMicroseconds, router, audioSink.submit, operationStop);
            if (audioResult.endOfStream && audioSink.endOfStream) {
                audioSink.endOfStream(request.video.firstFrameIdentity.playbackGeneration);
            }
        });
    }
    std::optional<std::jthread> subtitleWorker;
    if (subtitleStream && subtitleCodecContext && subtitleParameters) {
        subtitleWorker.emplace([&, codecContext = std::move(subtitleCodecContext)]() mutable {
            subtitleResult = decodeSubtitlePackets(
                std::move(codecContext), *subtitleParameters, bitmapSubtitleCodec, subtitleFallbackCanvas, origin,
                request.video.firstFrameIdentity.playbackGeneration, router, subtitleSink, operationStop.get_token());
        });
    }

    while (!operationStop.stop_requested()) {
        PacketPtr packet(av_packet_alloc());
        if (!packet) {
            router.finish(FfmpegPacketRouterTerminal::Failed, QStringLiteral("Could not allocate media packet"));
            break;
        }
        int const read = av_read_frame(formatContext.get(), packet.get());
        if (read < 0) {
            if (operationStop.stop_requested()) {
                router.finish(FfmpegPacketRouterTerminal::Cancelled);
            } else if (read == AVERROR_EOF) {
                router.finish(FfmpegPacketRouterTerminal::EndOfStream);
            } else {
                router.finish(FfmpegPacketRouterTerminal::Failed,
                              QStringLiteral("Media read failed: %1").arg(ffmpegError(read)));
            }
            break;
        }
        std::optional<FfmpegPacketStream> destination;
        if (packet->stream_index == videoIndex) {
            destination = FfmpegPacketStream::Video;
        } else if (packet->stream_index == audioIndex) {
            destination = FfmpegPacketStream::Audio;
        } else if (packet->stream_index == subtitleIndex) {
            destination = FfmpegPacketStream::Subtitle;
        }
        if (destination && !router.push(*destination, std::move(packet), operationStop.get_token())) {
            router.finish(FfmpegPacketRouterTerminal::Cancelled);
            break;
        }
    }

    videoWorker.join();
    if (audioWorker) {
        audioWorker->join();
    }
    if (subtitleWorker) {
        subtitleWorker->join();
    }

    FfmpegPacketRouterStatistics const routerStatistics = router.statistics();
    result.packetCountLimit = routerStatistics.packetCountLimit;
    result.packetByteLimit = routerStatistics.packetByteLimit;
    result.maximumQueuedPacketCount = routerStatistics.maximumQueuedPacketCount;
    result.maximumQueuedPacketBytes = routerStatistics.maximumQueuedPacketBytes;
    result.largestQueuedPacketBytes = routerStatistics.largestQueuedPacketBytes;

    result.video = std::move(videoResult);
    if (audioStream) {
        result.audio = std::move(audioResult.diagnostics);
        result.decodedAudioFrames = audioResult.decodedFrames;
        result.outputAudioFrames = audioResult.outputFrames;
        result.observedAudioEndMicroseconds = audioResult.observedEndMicroseconds;
        result.audioEndOfStream = audioResult.endOfStream;
        result.audioStopped = audioResult.stopped;
    }
    if (!subtitleResult.error.isEmpty()) {
        result.subtitleError = subtitleResult.error;
    }
    result.subtitleEndOfStream = subtitleResult.endOfStream;

    if (!result.video.error.isEmpty()) {
        result.error = result.video.error;
        return result;
    }
    if (audioStream && !audioResult.error.isEmpty()) {
        result.error = audioResult.error;
        return result;
    }
    if (result.video.stopped || audioResult.stopped) {
        return result;
    }
    if (stopToken.stop_requested() || result.video.isCancelled() || audioResult.cancelled) {
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
    if (audioStream && !audioResult.endOfStream && !audioResult.stopped) {
        result.error = QStringLiteral("Audio decoding failed");
        return result;
    }
    std::optional<std::int64_t> const observedEnd =
        observedPlaybackDurationMicroseconds(result.video.observedEndMicroseconds, result.observedAudioEndMicroseconds);
    if (observedEnd) {
        result.video.diagnostics.durationMicroseconds = observedEnd;
        result.video.diagnostics.durationFinal = true;
        qCInfo(sunroomLogMediaDecode).noquote() << "event=media.duration_finalized"
                                                << "durationUs=" + QString::number(*observedEnd);
    } else if (audioStream) {
        result.video.diagnostics.durationMicroseconds = initialVideoDiagnostics.durationMicroseconds;
        result.video.diagnostics.durationFinal = false;
    }
    qCInfo(sunroomLogMediaDecode).noquote()
        << "event=decode.synchronized_complete"
        << "generation=" + QString::number(request.video.firstFrameIdentity.playbackGeneration)
        << "videoFrames=" + QString::number(result.video.framesDecoded)
        << "audioFrames=" + QString::number(result.outputAudioFrames)
        << "maxPacketCount=" + QString::number(result.maximumQueuedPacketCount)
        << "maxPacketBytes=" + QString::number(result.maximumQueuedPacketBytes)
        << "audioPresent=" + QString(result.audioStreamPresent ? QStringLiteral("true") : QStringLiteral("false"));
    return result;
}
