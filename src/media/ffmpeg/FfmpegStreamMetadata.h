#pragma once

#include <cstdint>
#include <optional>

#include "media/FfmpegVideoDecoder.h"

struct AVFormatContext;
struct AVStream;

std::optional<std::int64_t> checkedTimestampAdd(std::int64_t left, std::int64_t right);
std::optional<std::int64_t> checkedTimestampSubtract(std::int64_t left, std::int64_t right);

class ObservedVideoRange final {
  public:
    void observeFrame(std::optional<std::int64_t> normalizedPtsMicroseconds,
                      std::optional<std::int64_t> durationMicroseconds);
    std::optional<std::int64_t> endMicroseconds() const;

  private:
    std::optional<std::int64_t> m_latestPtsMicroseconds;
    std::optional<std::int64_t> m_maximumEndMicroseconds;
    bool m_latestFrameEndKnown = false;
};

std::optional<VideoTimelineOrigin> ffmpegSharedTimelineOrigin(AVFormatContext const& formatContext,
                                                              AVStream const& videoStream, AVStream const* audioStream,
                                                              std::optional<VideoTimelineOrigin> const& requested);

// Best declared end of one stream on the normalized playback timeline. Uses
// AVStream timing first and the Matroska/WebM DURATION tag fallback emitted by
// FFmpeg when the public duration field is unavailable.
std::optional<std::int64_t> ffmpegDeclaredStreamEndMicroseconds(AVFormatContext const& formatContext,
                                                                AVStream const& stream,
                                                                std::optional<VideoTimelineOrigin> const& origin);

// Returns FFmpeg's best duration estimate at open time. The public format and
// stream fields are durations, not endpoints, so start_time must not be
// subtracted. Some containers can nevertheless include a leading empty
// interval in this value. A decoder that reaches EOF should replace this
// provisional value with the maximum observed selected-stream endpoint.
std::optional<std::int64_t> ffmpegProvisionalDurationMicroseconds(AVFormatContext const& formatContext,
                                                                  AVStream const& stream);

// Finalizes the selected playback range from the required video endpoint and
// an audio endpoint when decoded audio exists in the requested interval. A
// clean selected-audio interval with no samples does not invalidate the video
// endpoint. This deliberately ignores the provisional header estimate once
// complete EOF evidence is available.
std::optional<std::int64_t> observedPlaybackDurationMicroseconds(std::optional<std::int64_t> videoEndMicroseconds,
                                                                 std::optional<std::int64_t> audioEndMicroseconds);
