#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>

#include "media/FfmpegVideoDecoder.h"

class DecodedVideoFrame;

struct QueuedVideoFrame {
    std::shared_ptr<const DecodedVideoFrame> frame;
    FfmpegVideoStreamDiagnostics diagnostics;
    std::int64_t timelineTimeMicroseconds = 0;
    std::int64_t presentationTimeMicroseconds = 0;
    std::int64_t durationMicroseconds = 0;
    bool durationAuthoritative = false;

    bool isValid() const;
};

// Resolves missing timestamps without changing DecodedVideoFrame's exact
// FFmpeg timing snapshot. One instance belongs to one decoder revision.
class VideoFrameTimeline final {
public:
    explicit VideoFrameTimeline(
        std::optional<VideoTimelineOrigin> stableOrigin = {});

    QueuedVideoFrame schedule(
        std::shared_ptr<const DecodedVideoFrame> frame,
        const FfmpegVideoStreamDiagnostics &diagnostics);

private:
    std::optional<VideoTimelineOrigin> m_timelineOrigin;
    std::optional<std::int64_t> m_lastTimelineTimeMicroseconds;
    std::int64_t m_lastDurationMicroseconds = 33'333;
};

struct VideoSeekPrerollAdmission {
    std::optional<QueuedVideoFrame> first;
    std::optional<QueuedVideoFrame> second;
};

// Decoder restarts begin at a preceding keyframe. This gate consumes decoded
// preroll before the bounded frame mailbox, admitting the frame active at the
// target position or the first frame after it.
class VideoSeekPrerollGate final {
public:
    explicit VideoSeekPrerollGate(
        std::optional<std::int64_t> targetPositionMicroseconds);

    VideoSeekPrerollAdmission admit(
        QueuedVideoFrame frame);
    std::optional<QueuedVideoFrame> finish();

private:
    std::optional<std::int64_t> m_targetPositionMicroseconds;
    bool m_open = false;
    std::optional<QueuedVideoFrame> m_candidate;
    bool m_candidateMayCoverTarget = false;
};

// Stop-aware, generation-scoped mailbox. Retained hardware frames reserve
// decoder surfaces, so capacity is deliberately small and hard.
class VideoFrameQueue final {
public:
    static constexpr std::size_t capacity = 3;

    void reset(std::uint64_t playbackGeneration);
    bool push(
        std::uint64_t playbackGeneration,
        QueuedVideoFrame frame,
        std::stop_token stopToken);
    std::optional<QueuedVideoFrame> front(
        std::uint64_t playbackGeneration) const;
    std::optional<QueuedVideoFrame> pop(
        std::uint64_t playbackGeneration);
    std::size_t size(
        std::uint64_t playbackGeneration) const;
    std::uint64_t totalPushed(
        std::uint64_t playbackGeneration) const;
    std::size_t maximumObservedSize() const;

private:
    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<QueuedVideoFrame> m_frames;
    std::uint64_t m_playbackGeneration = 0;
    std::uint64_t m_totalPushed = 0;
    std::size_t m_maximumObservedSize = 0;
};
