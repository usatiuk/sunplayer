#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct AudioStreamFormat {
    bool operator==(const AudioStreamFormat &) const = default;

    int sampleRate = 0;
    int channelCount = 0;

    bool isValid() const;
};

// Immutable-by-convention decoded PCM handoff. Samples are native-endian,
// interleaved IEEE float32. The stream-frame index is scoped to one playback
// generation; mediaStartMicroseconds remains on the shared media timeline.
struct PcmAudioBlock {
    std::uint64_t playbackGeneration = 0;
    std::uint64_t streamFrameIndex = 0;
    std::int64_t mediaStartMicroseconds = 0;
    AudioStreamFormat format;
    std::vector<float> samples;

    std::size_t frameCount() const;
    bool isValid() const;
};

struct AudioPresentationSnapshot {
    std::uint64_t playbackGeneration = 0;
    // Identifies one physical output-clock lifetime within a playback
    // generation. Device replacement must start a new epoch and re-anchor it.
    std::uint64_t audioOutputEpoch = 0;
    std::uint64_t submittedFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::int64_t mediaPositionMicroseconds = 0;
    bool producerFinished = false;
    bool drained = false;
    // The device is presenting output hold-silence rather than media PCM.
    // The device cursor may advance, but mediaPositionMicroseconds must stay
    // fixed until real media presentation resumes.
    bool holding = false;
    bool advancing = false;
    // A drained backend may stop exposing a live device position. In that
    // case mediaPositionMicroseconds still carries the final presented media
    // endpoint when terminalPositionValid is true.
    bool terminalPositionValid = false;
    // Terminal failure of this playback generation. The reason is obtained
    // from AudioSink::failureReason() outside the real-time boundary.
    bool failed = false;
    bool valid = false;
};

// Low-rate, immutable observation for diagnostics and support tooling. This is
// sampled outside the real-time callback; implementations publish callback
// counters through atomics or their existing synchronized control boundary.
struct AudioSinkDiagnostics {
    bool operator==(const AudioSinkDiagnostics &) const = default;

    std::string backendName;
    std::string errorMessage;
    AudioStreamFormat format;
    std::size_t queueCapacityFrames = 0;
    std::size_t maximumSubmitFrames = 0;
    std::size_t queuedFrames = 0;
    std::size_t maximumQueuedFrames = 0;
    std::uint32_t requestedLatencyFrames = 0;
    std::optional<std::uint32_t> reportedLatencyFrames;
    std::uint64_t mediaFramesSubmitted = 0;
    std::uint64_t mediaFramesPresented = 0;
    std::uint64_t deviceFramesWritten = 0;
    std::optional<std::uint64_t> deviceFramesPresented;
    std::uint64_t underrunFrames = 0;
    std::uint64_t audioOutputEpoch = 0;
    std::uint64_t deviceRevision = 0;
    bool streamOpen = false;
    bool followsSystemDefault = false;
    bool positionAvailable = false;
    bool deviceNotificationsAvailable = false;
    bool clockReliable = false;
};
