#include "playback/VideoFrameQueue.h"

#include <algorithm>
#include <utility>

#include "media/DecodedVideoFrame.h"

namespace {
constexpr std::int64_t defaultFrameDurationMicroseconds =
    33'333;
}

bool QueuedVideoFrame::isValid() const {
    return frame
        && diagnostics.isValid()
        && presentationTimeMicroseconds >= 0
        && durationMicroseconds > 0;
}

QueuedVideoFrame VideoFrameTimeline::schedule(
        std::shared_ptr<const DecodedVideoFrame> frame,
        const FfmpegVideoStreamDiagnostics &diagnostics) {
    Q_ASSERT(frame);
    Q_ASSERT(diagnostics.isValid());

    const VideoFrameTiming &timing = frame->timing();
    const std::optional<std::int64_t> timestamp =
        timing.ptsMicroseconds();
    std::int64_t presentationTime = 0;
    if (timestamp) {
        if (diagnostics.timelineOriginMicroseconds) {
            presentationTime =
                *timestamp
                - *diagnostics.timelineOriginMicroseconds;
        } else {
            if (!m_firstTimestampMicroseconds)
                m_firstTimestampMicroseconds = *timestamp;
            presentationTime =
                *timestamp - *m_firstTimestampMicroseconds;
        }
    } else if (m_lastPresentationTimeMicroseconds) {
        presentationTime =
            *m_lastPresentationTimeMicroseconds
            + m_lastDurationMicroseconds;
    }
    presentationTime = std::max<std::int64_t>(
        0, presentationTime);
    if (m_lastPresentationTimeMicroseconds) {
        presentationTime = std::max(
            presentationTime,
            *m_lastPresentationTimeMicroseconds);
    }

    std::int64_t duration =
        timing.durationMicroseconds().value_or(
            diagnostics
                .nominalFrameDurationMicroseconds
                .value_or(m_lastDurationMicroseconds));
    if (duration <= 0)
        duration = defaultFrameDurationMicroseconds;

    m_lastPresentationTimeMicroseconds =
        presentationTime;
    m_lastDurationMicroseconds = duration;
    return {
        .frame = std::move(frame),
        .diagnostics = diagnostics,
        .presentationTimeMicroseconds = presentationTime,
        .durationMicroseconds = duration,
    };
}

void VideoFrameQueue::reset(
        std::uint64_t playbackGeneration) {
    {
        std::lock_guard lock(m_mutex);
        m_frames.clear();
        m_playbackGeneration = playbackGeneration;
        m_totalPushed = 0;
        m_maximumObservedSize = 0;
    }
    m_wake.notify_all();
}

bool VideoFrameQueue::push(
        std::uint64_t playbackGeneration,
        QueuedVideoFrame frame,
        std::stop_token stopToken) {
    Q_ASSERT(frame.isValid());
    Q_ASSERT(
        frame.frame->identity().playbackGeneration
        == playbackGeneration);

    std::unique_lock lock(m_mutex);
    const bool ready = m_wake.wait(
        lock,
        stopToken,
        [this, playbackGeneration] {
            return playbackGeneration
                    != m_playbackGeneration
                || m_frames.size() < capacity;
        });
    if (!ready
            || playbackGeneration
                != m_playbackGeneration) {
        return false;
    }
    m_frames.push_back(std::move(frame));
    ++m_totalPushed;
    m_maximumObservedSize = std::max(
        m_maximumObservedSize, m_frames.size());
    lock.unlock();
    m_wake.notify_all();
    return true;
}

std::optional<QueuedVideoFrame> VideoFrameQueue::front(
        std::uint64_t playbackGeneration) const {
    std::lock_guard lock(m_mutex);
    if (playbackGeneration != m_playbackGeneration
            || m_frames.empty()) {
        return std::nullopt;
    }
    return m_frames.front();
}

std::optional<QueuedVideoFrame> VideoFrameQueue::pop(
        std::uint64_t playbackGeneration) {
    std::unique_lock lock(m_mutex);
    if (playbackGeneration != m_playbackGeneration
            || m_frames.empty()) {
        return std::nullopt;
    }
    QueuedVideoFrame frame =
        std::move(m_frames.front());
    m_frames.pop_front();
    lock.unlock();
    m_wake.notify_all();
    return frame;
}

std::size_t VideoFrameQueue::size(
        std::uint64_t playbackGeneration) const {
    std::lock_guard lock(m_mutex);
    return playbackGeneration == m_playbackGeneration
        ? m_frames.size()
        : 0;
}

std::uint64_t VideoFrameQueue::totalPushed(
        std::uint64_t playbackGeneration) const {
    std::lock_guard lock(m_mutex);
    return playbackGeneration == m_playbackGeneration
        ? m_totalPushed
        : 0;
}

std::size_t VideoFrameQueue::maximumObservedSize() const {
    std::lock_guard lock(m_mutex);
    return m_maximumObservedSize;
}
