#pragma once

#include <cstddef>
#include <cstdint>
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
    std::uint64_t submittedFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::int64_t mediaPositionMicroseconds = 0;
    bool producerFinished = false;
    bool drained = false;
    bool advancing = false;
    bool valid = false;
};
