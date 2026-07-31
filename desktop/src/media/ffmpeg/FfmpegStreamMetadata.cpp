#include "media/ffmpeg/FfmpegStreamMetadata.h"

#include <algorithm>
#include <limits>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include "diagnostics/LogCategories.h"

namespace {
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

VideoTimelineOrigin makeOrigin(
        std::int64_t timestamp,
        AVRational timeBase) {
    return {
        .timestamp = timestamp,
        .timeBase = {timeBase.num, timeBase.den},
    };
}

}

std::optional<std::int64_t> checkedTimestampAdd(
        std::int64_t left,
        std::int64_t right) {
    if ((right > 0
            && left > std::numeric_limits<std::int64_t>::max()
                - right)
            || (right < 0
                && left < std::numeric_limits<std::int64_t>::min()
                    - right)) {
        return std::nullopt;
    }
    return left + right;
}

std::optional<std::int64_t> checkedTimestampSubtract(
        std::int64_t left,
        std::int64_t right) {
    if ((right > 0
            && left < std::numeric_limits<std::int64_t>::min()
                + right)
            || (right < 0
                && left > std::numeric_limits<std::int64_t>::max()
                    + right)) {
        return std::nullopt;
    }
    return left - right;
}

void ObservedVideoRange::observeFrame(
        std::optional<std::int64_t> normalizedPtsMicroseconds,
        std::optional<std::int64_t> durationMicroseconds) {
    if (!normalizedPtsMicroseconds) {
        m_latestFrameEndKnown = false;
        return;
    }
    const bool latest = !m_latestPtsMicroseconds
        || *normalizedPtsMicroseconds > *m_latestPtsMicroseconds;
    if (latest) {
        m_latestPtsMicroseconds = normalizedPtsMicroseconds;
        m_latestFrameEndKnown = false;
    }
    if (!durationMicroseconds || *durationMicroseconds <= 0)
        return;
    const std::optional<std::int64_t> end = checkedTimestampAdd(
        *normalizedPtsMicroseconds,
        *durationMicroseconds);
    if (end && *end > 0) {
        if (!m_maximumEndMicroseconds
                || *end > *m_maximumEndMicroseconds) {
            m_maximumEndMicroseconds = end;
        }
        if (latest || *normalizedPtsMicroseconds
                == *m_latestPtsMicroseconds) {
            m_latestFrameEndKnown = true;
        }
    }
}

std::optional<std::int64_t>
ObservedVideoRange::endMicroseconds() const {
    return m_latestFrameEndKnown
        ? m_maximumEndMicroseconds
        : std::nullopt;
}

std::optional<VideoTimelineOrigin> ffmpegSharedTimelineOrigin(
        const AVFormatContext &formatContext,
        const AVStream &videoStream,
        const AVStream *audioStream,
        const std::optional<VideoTimelineOrigin> &requested) {
    if (requested)
        return requested;
    if (formatContext.start_time != AV_NOPTS_VALUE) {
        return makeOrigin(
            formatContext.start_time, AV_TIME_BASE_Q);
    }

    std::optional<std::int64_t> earliestMicroseconds;
    const auto consider = [&earliestMicroseconds](
            const AVStream &stream) {
        if (stream.start_time == AV_NOPTS_VALUE)
            return;
        const std::int64_t converted = av_rescale_q(
            stream.start_time,
            stream.time_base,
            AV_TIME_BASE_Q);
        if (!earliestMicroseconds
                || converted < *earliestMicroseconds) {
            earliestMicroseconds = converted;
        }
    };
    consider(videoStream);
    if (audioStream)
        consider(*audioStream);
    return earliestMicroseconds
        ? std::optional<VideoTimelineOrigin>(
            makeOrigin(*earliestMicroseconds, AV_TIME_BASE_Q))
        : std::nullopt;
}

std::optional<std::int64_t> ffmpegProvisionalDurationMicroseconds(
        const AVFormatContext &formatContext,
        const AVStream &stream) {
    std::optional<std::int64_t> result;
    if (formatContext.duration > 0
            && formatContext.duration != AV_NOPTS_VALUE) {
        result = formatContext.duration;
    } else {
        result = positiveDurationMicroseconds(
            stream.duration, stream.time_base);
    }
    qCDebug(sunroomLogMediaDecode).noquote()
        << "event=media.duration_estimated"
        << "rawFormatUs=" + QString::number(
            formatContext.duration)
        << "formatStartUs=" + QString::number(
            formatContext.start_time)
        << "estimation=" + QString::number(
            formatContext.duration_estimation_method)
        << "streamDuration=" + QString::number(
            stream.duration)
        << "streamTimeBase="
            + QStringLiteral("%1/%2")
                .arg(stream.time_base.num)
                .arg(stream.time_base.den)
        << "provisionalUs=" + (result
            ? QString::number(*result)
            : QStringLiteral("unknown"));
    return result;
}

std::optional<std::int64_t> observedPlaybackDurationMicroseconds(
        std::optional<std::int64_t> videoEndMicroseconds,
        std::optional<std::int64_t> audioEndMicroseconds,
        bool audioSelected) {
    if (!videoEndMicroseconds
            || *videoEndMicroseconds <= 0
            || (audioSelected
                && (!audioEndMicroseconds
                    || *audioEndMicroseconds <= 0))) {
        return std::nullopt;
    }
    if (!audioSelected)
        return videoEndMicroseconds;
    return std::max(
        *videoEndMicroseconds,
        *audioEndMicroseconds);
}
