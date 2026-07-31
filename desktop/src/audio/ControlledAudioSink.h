#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <vector>

#include "audio/AudioSink.h"

struct ControlledAudioRender {
    std::vector<float> samples;
    std::size_t frames = 0;
};

// Deterministic device edge for tests and scenario runners. It models a
// bounded decode-to-device queue and keeps submitted and presented cursors
// separate. It intentionally uses locks and is not a physical callback.
class ControlledAudioSink final : public AudioSink {
public:
    explicit ControlledAudioSink(
        std::size_t maximumBufferedFrames);

    void reset(
        std::uint64_t playbackGeneration,
        AudioStreamFormat format) override;
    bool submit(
        PcmAudioBlock block,
        std::stop_token stopToken = {}) override;
    void finish() override;
    void start() override;
    void pause() override;
    AudioPresentationSnapshot snapshot() const override;

    ControlledAudioRender render(std::size_t requestedFrames);
    void advancePresentedFrames(std::size_t frames);

    std::size_t bufferedFrames() const;
    std::size_t maximumObservedBufferedFrames() const;
    std::uint64_t submittedFrames() const;
    std::uint64_t presentedFrames() const;

private:
    struct QueuedBlock {
        PcmAudioBlock block;
        std::size_t consumedFrames = 0;
    };

    struct MappingSegment {
        std::uint64_t submittedStartFrame = 0;
        std::uint64_t frameCount = 0;
        std::int64_t mediaStartMicroseconds = 0;
    };

    std::int64_t mediaPositionForPresentedFrameLocked(
        std::uint64_t presentedFrame) const;
    std::uint64_t outstandingFramesLocked() const;

    const std::size_t m_maximumBufferedFrames;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<QueuedBlock> m_blocks;
    std::vector<MappingSegment> m_mapping;
    AudioStreamFormat m_format;
    std::uint64_t m_playbackGeneration = 0;
    std::uint64_t m_submittedFrames = 0;
    std::uint64_t m_presentedFrames = 0;
    std::size_t m_bufferedFrames = 0;
    std::size_t m_maximumObservedBufferedFrames = 0;
    bool m_running = false;
    bool m_finished = false;
};
