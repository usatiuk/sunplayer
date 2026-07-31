#include "audio/AudioOutputLedger.h"

#include <algorithm>
#include <limits>

#include <QtGlobal>

AudioOutputLedger::AudioOutputLedger(
        std::size_t spanCapacity)
    : m_spanCapacity(spanCapacity),
      m_slots(std::make_unique<Slot[]>(spanCapacity)) {
    Q_ASSERT(m_spanCapacity > 0);
    static_assert(
        std::atomic<std::uint64_t>::is_always_lock_free);
}

void AudioOutputLedger::reset() {
    m_publishedSequence.store(0, std::memory_order_relaxed);
    m_outputFrames.store(0, std::memory_order_relaxed);
    m_mediaFrames.store(0, std::memory_order_relaxed);
    for (std::size_t index = 0;
            index < m_spanCapacity;
            ++index) {
        m_slots[index].sequence.store(0);
    }
}

void AudioOutputLedger::record(
        std::size_t mediaFrames,
        std::size_t holdFrames) {
    const std::uint64_t total =
        static_cast<std::uint64_t>(mediaFrames)
        + static_cast<std::uint64_t>(holdFrames);
    if (total == 0)
        return;
    const std::uint64_t outputStart = m_outputFrames.load(
        std::memory_order_relaxed);
    const std::uint64_t mediaStart = m_mediaFrames.load(
        std::memory_order_relaxed);
    Q_ASSERT(outputStart
        <= std::numeric_limits<std::uint64_t>::max() - total);
    Q_ASSERT(mediaStart
        <= std::numeric_limits<std::uint64_t>::max()
            - mediaFrames);

    const std::uint64_t sequence =
        m_publishedSequence.load(std::memory_order_relaxed) + 1;
    Slot &slot = m_slots[
        static_cast<std::size_t>(
            (sequence - 1) % m_spanCapacity)];
    // These per-slot atomics deliberately use sequential consistency. The
    // callback publishes infrequent spans, and the stronger ordering makes
    // overwrite detection below a valid snapshot.
    slot.sequence.store(0);
    slot.outputStart.store(outputStart);
    slot.mediaStart.store(mediaStart);
    slot.mediaFrameCount.store(mediaFrames);
    slot.holdFrameCount.store(holdFrames);
    slot.sequence.store(sequence);
    m_outputFrames.store(
        outputStart + total, std::memory_order_release);
    m_mediaFrames.store(
        mediaStart + mediaFrames, std::memory_order_release);
    m_publishedSequence.store(
        sequence, std::memory_order_release);
}

std::optional<AudioOutputPosition>
AudioOutputLedger::positionForOutputFrame(
        std::uint64_t outputFrame) const {
    if (outputFrame == 0)
        return AudioOutputPosition{};
    const std::uint64_t latest = m_publishedSequence.load(
        std::memory_order_acquire);
    const std::uint64_t available = std::min<std::uint64_t>(
        latest, m_spanCapacity);
    for (std::uint64_t offset = 0;
            offset < available;
            ++offset) {
        const std::uint64_t sequence = latest - offset;
        const Slot &slot = m_slots[
            static_cast<std::size_t>(
                (sequence - 1) % m_spanCapacity)];
        if (slot.sequence.load() != sequence) {
            continue;
        }
        const std::uint64_t outputStart =
            slot.outputStart.load();
        const std::uint64_t mediaStart =
            slot.mediaStart.load();
        const std::uint64_t mediaCount =
            slot.mediaFrameCount.load();
        const std::uint64_t holdCount =
            slot.holdFrameCount.load();
        if (slot.sequence.load() != sequence) {
            continue;
        }
        if (outputFrame < outputStart)
            continue;
        const std::uint64_t spanFrames = mediaCount + holdCount;
        if (outputFrame > outputStart + spanFrames)
            continue;
        const std::uint64_t within = outputFrame - outputStart;
        return AudioOutputPosition{
            .mediaFrame =
                mediaStart + std::min(within, mediaCount),
            .holding = holdCount != 0 && within >= mediaCount,
        };
    }
    return std::nullopt;
}

std::uint64_t AudioOutputLedger::outputFrames() const {
    return m_outputFrames.load(std::memory_order_acquire);
}

std::uint64_t AudioOutputLedger::mediaFrames() const {
    return m_mediaFrames.load(std::memory_order_acquire);
}
