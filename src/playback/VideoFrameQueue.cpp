#include "playback/VideoFrameQueue.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "media/DecodedVideoFrame.h"

namespace {
constexpr std::int64_t defaultFrameDurationMicroseconds = 33'333;
}

bool QueuedVideoFrame::isValid() const {
    return frame && diagnostics.isValid() && presentationTimeMicroseconds >= 0 && durationMicroseconds > 0;
}

VideoFrameTimeline::VideoFrameTimeline(std::optional<VideoTimelineOrigin> stableOrigin)
    : m_timelineOrigin(std::move(stableOrigin)) {
    Q_ASSERT(!m_timelineOrigin || m_timelineOrigin->isValid());
}

QueuedVideoFrame VideoFrameTimeline::schedule(std::shared_ptr<DecodedVideoFrame const> frame,
                                              FfmpegVideoStreamDiagnostics const& diagnostics) {
    Q_ASSERT(frame);
    Q_ASSERT(diagnostics.isValid());

    VideoFrameTiming const& timing = frame->timing();
    std::optional<std::int64_t> const timestamp = timing.ptsMicroseconds();
    FfmpegVideoStreamDiagnostics effectiveDiagnostics = diagnostics;
    if (!m_timelineOrigin && diagnostics.timelineOrigin) {
        m_timelineOrigin = diagnostics.timelineOrigin;
    }
    if (!m_timelineOrigin && timing.pts) {
        m_timelineOrigin = VideoTimelineOrigin{
            .timestamp = *timing.pts,
            .timeBase = timing.timeBase,
        };
    }
    effectiveDiagnostics.timelineOrigin = m_timelineOrigin;

    std::int64_t timelineTime = 0;
    if (timestamp) {
        std::optional<std::int64_t> const origin = m_timelineOrigin ? m_timelineOrigin->microseconds() : std::nullopt;
        if (origin) {
            timelineTime = *timestamp - *origin;
        }
    } else if (m_lastTimelineTimeMicroseconds) {
        timelineTime = *m_lastTimelineTimeMicroseconds + m_lastDurationMicroseconds;
    }
    if (m_lastTimelineTimeMicroseconds) {
        timelineTime = std::max(timelineTime, *m_lastTimelineTimeMicroseconds);
    }

    std::optional<std::int64_t> const decodedDuration = timing.durationMicroseconds();
    std::int64_t duration =
        decodedDuration.value_or(diagnostics.nominalFrameDurationMicroseconds.value_or(m_lastDurationMicroseconds));
    if (duration <= 0) {
        duration = defaultFrameDurationMicroseconds;
    }

    m_lastTimelineTimeMicroseconds = timelineTime;
    m_lastDurationMicroseconds = duration;
    return {
        .frame = std::move(frame),
        .diagnostics = std::move(effectiveDiagnostics),
        .timelineTimeMicroseconds = timelineTime,
        .presentationTimeMicroseconds = std::max<std::int64_t>(0, timelineTime),
        .durationMicroseconds = duration,
        .durationAuthoritative = decodedDuration.has_value(),
    };
}

VideoSeekPrerollGate::VideoSeekPrerollGate(std::optional<std::int64_t> targetPositionMicroseconds)
    : m_targetPositionMicroseconds(targetPositionMicroseconds) {
    Q_ASSERT(!m_targetPositionMicroseconds || *m_targetPositionMicroseconds >= 0);
}

VideoSeekPrerollAdmission VideoSeekPrerollGate::admit(QueuedVideoFrame frame) {
    Q_ASSERT(frame.isValid());
    if (m_open || !m_targetPositionMicroseconds) {
        return {.first = std::move(frame)};
    }

    std::int64_t const target = *m_targetPositionMicroseconds;
    if (frame.timelineTimeMicroseconds == target) {
        m_open = true;
        m_candidate.reset();
        return {.first = std::move(frame)};
    }
    if (frame.timelineTimeMicroseconds > target) {
        m_open = true;
        if (m_candidateMayCoverTarget) {
            VideoSeekPrerollAdmission admitted{
                .first = std::move(m_candidate),
                .second = std::move(frame),
            };
            m_candidate.reset();
            return admitted;
        }
        m_candidate.reset();
        return {.first = std::move(frame)};
    }

    m_candidateMayCoverTarget = !frame.durationAuthoritative;
    bool const durationCrossesTarget =
        frame.timelineTimeMicroseconds > std::numeric_limits<std::int64_t>::max() - frame.durationMicroseconds ||
        frame.timelineTimeMicroseconds + frame.durationMicroseconds > target;
    if (frame.durationAuthoritative && durationCrossesTarget) {
        m_open = true;
        m_candidate.reset();
        return {.first = std::move(frame)};
    }

    m_candidate = std::move(frame);
    return {};
}

std::optional<QueuedVideoFrame> VideoSeekPrerollGate::finish() {
    if (m_open || !m_candidate) {
        return std::nullopt;
    }
    m_open = true;
    return std::move(m_candidate);
}

void VideoFrameQueue::reset(std::uint64_t playbackGeneration) {
    {
        std::lock_guard lock(m_mutex);
        m_frames.clear();
        m_playbackGeneration = playbackGeneration;
        m_totalPushed = 0;
        m_maximumObservedSize = 0;
    }
    m_wake.notify_all();
}

bool VideoFrameQueue::push(std::uint64_t playbackGeneration, QueuedVideoFrame frame, std::stop_token stopToken) {
    Q_ASSERT(frame.isValid());
    Q_ASSERT(frame.frame->identity().playbackGeneration == playbackGeneration);

    std::unique_lock lock(m_mutex);
    bool const ready = m_wake.wait(lock, stopToken, [this, playbackGeneration] {
        return playbackGeneration != m_playbackGeneration || m_frames.size() < capacity;
    });
    if (!ready || playbackGeneration != m_playbackGeneration) {
        return false;
    }
    m_frames.push_back(std::move(frame));
    ++m_totalPushed;
    m_maximumObservedSize = std::max(m_maximumObservedSize, m_frames.size());
    lock.unlock();
    m_wake.notify_all();
    return true;
}

std::optional<QueuedVideoFrame> VideoFrameQueue::front(std::uint64_t playbackGeneration) const {
    std::lock_guard lock(m_mutex);
    if (playbackGeneration != m_playbackGeneration || m_frames.empty()) {
        return std::nullopt;
    }
    return m_frames.front();
}

std::optional<QueuedVideoFrame> VideoFrameQueue::pop(std::uint64_t playbackGeneration) {
    std::unique_lock lock(m_mutex);
    if (playbackGeneration != m_playbackGeneration || m_frames.empty()) {
        return std::nullopt;
    }
    QueuedVideoFrame frame = std::move(m_frames.front());
    m_frames.pop_front();
    lock.unlock();
    m_wake.notify_all();
    return frame;
}

std::size_t VideoFrameQueue::size(std::uint64_t playbackGeneration) const {
    std::lock_guard lock(m_mutex);
    return playbackGeneration == m_playbackGeneration ? m_frames.size() : 0;
}

std::uint64_t VideoFrameQueue::totalPushed(std::uint64_t playbackGeneration) const {
    std::lock_guard lock(m_mutex);
    return playbackGeneration == m_playbackGeneration ? m_totalPushed : 0;
}

std::size_t VideoFrameQueue::maximumObservedSize() const {
    std::lock_guard lock(m_mutex);
    return m_maximumObservedSize;
}
