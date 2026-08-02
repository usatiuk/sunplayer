#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

#include "audio/AudioTypes.h"

struct RealtimePcmRead {
    std::size_t mediaFrames = 0;
    std::size_t silentFrames = 0;
};

enum class RealtimePcmSubmitResult {
    Accepted,
    Invalid,
    Stale,
    Reset,
    Cancelled,
};

// Single-producer/single-consumer PCM boundary. submit() may wait on the
// decode thread. consume() performs only bounded copies, zero-fill, atomics,
// and a non-blocking producer notification, making it suitable for a device
// callback after reset() has established a fixed format.
class RealtimePcmQueue final {
public:
    RealtimePcmQueue(
        std::size_t capacityFrames,
        int maximumChannelCount);

    void reset(
        std::uint64_t playbackGeneration,
        AudioStreamFormat format);
    void cancel();
    bool submit(
        const PcmAudioBlock &block,
        std::stop_token stopToken = {});
    RealtimePcmSubmitResult submitResult(
        const PcmAudioBlock &block,
        std::stop_token stopToken = {});
    RealtimePcmRead consume(
        std::span<float> output,
        std::size_t requestedFrames);

    std::size_t capacityFrames() const;
    std::size_t queuedFrames() const;
    std::size_t maximumObservedQueuedFrames() const;
    std::size_t waitingProducerCount() const;
    std::uint64_t producedMediaFrames() const;
    std::optional<std::int64_t>
        firstMediaTimestampMicroseconds() const;

private:
    std::size_t freeFrames() const;

    const std::size_t m_capacityFrames;
    const int m_maximumChannelCount;
    std::vector<float> m_samples;
    mutable std::mutex m_producerMutex;
    std::atomic<std::uint64_t> m_wakeRevision{0};
    std::atomic<std::uint64_t> m_readFrame{0};
    // The low bit is the sticky accepting state; the remaining bits are the
    // published write cursor. A single compare/exchange linearizes block
    // publication against callback-safe cancellation.
    std::atomic<std::uint64_t> m_writeState{0};
    std::atomic<std::uint64_t> m_epoch{0};
    std::atomic<std::uint64_t> m_playbackGeneration{0};
    std::atomic<int> m_channelCount{0};
    std::atomic<std::size_t> m_maximumObservedQueuedFrames{0};
    std::atomic<std::size_t> m_waitingProducerCount{0};
    std::atomic<std::uint64_t> m_producedMediaFrames{0};
    std::atomic<std::int64_t> m_firstMediaTimestampMicroseconds{0};
    std::atomic_bool m_hasFirstMediaTimestamp{false};
    AudioStreamFormat m_format;
};
