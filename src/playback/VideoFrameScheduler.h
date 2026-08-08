#pragma once

#include <cstdint>
#include <optional>

#include "playback/VideoFrameQueue.h"

// Shared input to playback scheduling policy. The provisional monotonic clock
// and a future audio-master clock both publish this value contract.
struct MediaClockSnapshot {
    std::int64_t positionMicroseconds = 0;
    bool advancing = false;
    // A terminal clock may select the last due frame and complete playback
    // even though it is no longer advancing.
    bool terminal = false;
};

struct VideoFrameSelection {
    std::optional<QueuedVideoFrame> frame;
    std::uint64_t droppedFrames = 0;
    bool reachedEnd = false;
    std::optional<std::int64_t> mediaEndMicroseconds;
};

// Deterministic presentation policy over a generation-scoped queue and a
// media-clock observation. It owns selected-frame timing, not the clock or
// frame storage.
class VideoFrameScheduler final {
  public:
    void reset();
    VideoFrameSelection selectFirst(VideoFrameQueue& queue, std::uint64_t playbackGeneration);
    VideoFrameSelection selectForPresentation(VideoFrameQueue& queue, std::uint64_t playbackGeneration,
                                              MediaClockSnapshot clock, bool decoderDrained,
                                              std::optional<std::int64_t> declaredDurationMicroseconds = std::nullopt);

  private:
    void remember(QueuedVideoFrame const& frame);

    std::optional<std::int64_t> m_currentFrameTimeMicroseconds;
    std::int64_t m_currentFrameDurationMicroseconds = 0;
};
