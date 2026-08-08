#include "playback/VideoFrameScheduler.h"

#include <utility>

void VideoFrameScheduler::reset() {
    m_currentFrameTimeMicroseconds.reset();
    m_currentFrameDurationMicroseconds = 0;
}

VideoFrameSelection VideoFrameScheduler::selectFirst(VideoFrameQueue& queue, std::uint64_t playbackGeneration) {
    VideoFrameSelection selection;
    selection.frame = queue.pop(playbackGeneration);
    if (selection.frame) {
        remember(*selection.frame);
    }
    return selection;
}

VideoFrameSelection
VideoFrameScheduler::selectForPresentation(VideoFrameQueue& queue, std::uint64_t playbackGeneration,
                                           MediaClockSnapshot clock, bool decoderDrained,
                                           std::optional<std::int64_t> declaredDurationMicroseconds) {
    VideoFrameSelection selection;
    std::uint64_t dueFrames = 0;
    while (true) {
        std::optional<QueuedVideoFrame> const next = queue.front(playbackGeneration);
        if (!next || next->presentationTimeMicroseconds > clock.positionMicroseconds) {
            break;
        }
        selection.frame = queue.pop(playbackGeneration);
        ++dueFrames;
    }

    if (selection.frame) {
        remember(*selection.frame);
        if (dueFrames > 1) {
            selection.droppedFrames = dueFrames - 1;
        }
    }

    if ((!clock.advancing && !clock.terminal) || !decoderDrained || queue.size(playbackGeneration) != 0 ||
        !m_currentFrameTimeMicroseconds) {
        return selection;
    }

    std::int64_t const finalFrameEnd = *m_currentFrameTimeMicroseconds + m_currentFrameDurationMicroseconds;
    std::int64_t const mediaEnd = declaredDurationMicroseconds ? *declaredDurationMicroseconds : finalFrameEnd;
    if (clock.positionMicroseconds >= mediaEnd) {
        selection.reachedEnd = true;
        selection.mediaEndMicroseconds = mediaEnd;
    }
    return selection;
}

void VideoFrameScheduler::remember(QueuedVideoFrame const& frame) {
    m_currentFrameTimeMicroseconds = frame.presentationTimeMicroseconds;
    m_currentFrameDurationMicroseconds = frame.durationMicroseconds;
}
