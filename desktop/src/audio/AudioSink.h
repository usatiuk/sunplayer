#pragma once

#include <cstdint>
#include <stop_token>
#include <string>

#include "audio/AudioTypes.h"

// Decode/control-thread contract. A production device implementation owns a
// separate callback-safe ring behind this boundary; submit() is never called
// by the physical real-time callback.
class AudioSink {
public:
    virtual ~AudioSink() = default;

    virtual void reset(
        std::uint64_t playbackGeneration,
        AudioStreamFormat format) = 0;
    virtual bool submit(
        PcmAudioBlock block,
        std::stop_token stopToken = {}) = 0;
    // Invalidates one output epoch and promptly releases any blocked
    // producer. A stale generation must not affect its replacement.
    virtual void cancel(std::uint64_t playbackGeneration) = 0;
    // End-of-stream is generation-scoped so a superseded decoder cannot mark
    // a replacement output epoch finished.
    virtual void finish(std::uint64_t playbackGeneration) = 0;
    virtual void start() = 0;
    virtual void pause() = 0;
    // snapshot().failed is generation-scoped; read the human-readable reason
    // only after observing that flag for the current generation.
    virtual AudioPresentationSnapshot snapshot() const = 0;
    virtual std::string failureReason() const = 0;
};
