#include "audio/RealtimePcmQueue.h"

#include <algorithm>
#include <limits>

#include <QtGlobal>

namespace {
std::optional<std::int64_t> timestampForFrame(std::int64_t firstTimestamp, std::uint64_t frame, int sampleRate) {
    std::uint64_t const rate = static_cast<std::uint64_t>(sampleRate);
    std::uint64_t const wholeSeconds = frame / rate;
    std::uint64_t const remainingFrames = frame % rate;
    if (wholeSeconds > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 1'000'000)) {
        return std::nullopt;
    }
    std::uint64_t const offset = wholeSeconds * 1'000'000 + remainingFrames * 1'000'000 / rate;
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    auto const signedOffset = static_cast<std::int64_t>(offset);
    if (firstTimestamp > std::numeric_limits<std::int64_t>::max() - signedOffset) {
        return std::nullopt;
    }
    return firstTimestamp + signedOffset;
}
} // namespace

RealtimePcmQueue::RealtimePcmQueue(std::size_t capacityFrames, int maximumChannelCount)
    : m_capacityFrames(capacityFrames), m_maximumChannelCount(maximumChannelCount),
      m_samples(capacityFrames * static_cast<std::size_t>(maximumChannelCount)) {
    Q_ASSERT(m_capacityFrames > 0);
    Q_ASSERT(m_maximumChannelCount > 0);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
}

void RealtimePcmQueue::reset(std::uint64_t playbackGeneration, AudioStreamFormat format) {
    Q_ASSERT(playbackGeneration != 0);
    Q_ASSERT(format.isValid());
    Q_ASSERT(format.channelCount <= m_maximumChannelCount);
    {
        std::lock_guard lock(m_producerMutex);
        m_writeState.store(0, std::memory_order_release);
        m_epoch.fetch_add(1, std::memory_order_acq_rel);
        m_readFrame.store(0, std::memory_order_relaxed);
        m_playbackGeneration.store(playbackGeneration, std::memory_order_relaxed);
        m_channelCount.store(format.channelCount, std::memory_order_relaxed);
        m_maximumObservedQueuedFrames.store(0, std::memory_order_relaxed);
        m_producedMediaFrames.store(0, std::memory_order_relaxed);
        m_hasFirstMediaTimestamp.store(false, std::memory_order_relaxed);
        m_format = format;
        m_writeState.store(1, std::memory_order_release);
    }
    m_wakeRevision.fetch_add(1, std::memory_order_release);
    m_wakeRevision.notify_all();
}

void RealtimePcmQueue::cancel() {
    m_writeState.fetch_and(~std::uint64_t{1}, std::memory_order_acq_rel);
    m_epoch.fetch_add(1, std::memory_order_acq_rel);
    m_wakeRevision.fetch_add(1, std::memory_order_release);
    m_wakeRevision.notify_all();
}

bool RealtimePcmQueue::submit(PcmAudioBlock const& block, std::stop_token stopToken) {
    return submitResult(block, stopToken) == RealtimePcmSubmitResult::Accepted;
}

RealtimePcmSubmitResult RealtimePcmQueue::submitResult(PcmAudioBlock const& block, std::stop_token stopToken) {
    if (!block.isValid()) {
        return RealtimePcmSubmitResult::Invalid;
    }

    std::unique_lock lock(m_producerMutex);
    std::stop_callback stopWake(stopToken, [this] {
        m_wakeRevision.fetch_add(1, std::memory_order_release);
        m_wakeRevision.notify_all();
    });
    if (stopToken.stop_requested()) {
        return RealtimePcmSubmitResult::Cancelled;
    }
    std::uint64_t const writeState = m_writeState.load(std::memory_order_acquire);
    if ((writeState & 1U) == 0) {
        return RealtimePcmSubmitResult::Reset;
    }
    std::uint64_t const epoch = m_epoch.load(std::memory_order_acquire);
    if (block.playbackGeneration != m_playbackGeneration.load(std::memory_order_relaxed)) {
        return RealtimePcmSubmitResult::Stale;
    }
    if (block.format != m_format || block.streamFrameIndex != m_producedMediaFrames.load(std::memory_order_relaxed)) {
        return RealtimePcmSubmitResult::Invalid;
    }

    bool const hasFirstTimestamp = m_hasFirstMediaTimestamp.load(std::memory_order_acquire);
    if (hasFirstTimestamp) {
        std::int64_t const firstTimestamp = m_firstMediaTimestampMicroseconds.load(std::memory_order_relaxed);
        std::uint64_t const produced = m_producedMediaFrames.load(std::memory_order_relaxed);
        std::optional<std::int64_t> const expected = timestampForFrame(firstTimestamp, produced, m_format.sampleRate);
        if (!expected ||
            (*expected > std::numeric_limits<std::int64_t>::min() && block.mediaStartMicroseconds < *expected - 1) ||
            (*expected < std::numeric_limits<std::int64_t>::max() && block.mediaStartMicroseconds > *expected + 1)) {
            return RealtimePcmSubmitResult::Invalid;
        }
    }

    std::size_t const channels = static_cast<std::size_t>(m_format.channelCount);
    std::size_t const blockFrames = block.frameCount();
    if (blockFrames > m_capacityFrames) {
        return RealtimePcmSubmitResult::Invalid;
    }
    while (freeFrames() < blockFrames) {
        if (stopToken.stop_requested()) {
            return RealtimePcmSubmitResult::Cancelled;
        }
        if (m_epoch.load(std::memory_order_acquire) != epoch ||
            (m_writeState.load(std::memory_order_acquire) & 1U) == 0) {
            return RealtimePcmSubmitResult::Reset;
        }
        std::uint64_t const revision = m_wakeRevision.load(std::memory_order_acquire);
        m_waitingProducerCount.fetch_add(1, std::memory_order_release);
        lock.unlock();
        if (!stopToken.stop_requested() && m_epoch.load(std::memory_order_acquire) == epoch &&
            (m_writeState.load(std::memory_order_acquire) & 1U) != 0 && freeFrames() < blockFrames) {
            m_wakeRevision.wait(revision, std::memory_order_acquire);
        }
        lock.lock();
        m_waitingProducerCount.fetch_sub(1, std::memory_order_release);
    }
    if (stopToken.stop_requested()) {
        return RealtimePcmSubmitResult::Cancelled;
    }
    if (m_epoch.load(std::memory_order_acquire) != epoch || (m_writeState.load(std::memory_order_acquire) & 1U) == 0) {
        return RealtimePcmSubmitResult::Reset;
    }

    if (!hasFirstTimestamp) {
        m_firstMediaTimestampMicroseconds.store(block.mediaStartMicroseconds, std::memory_order_relaxed);
        m_hasFirstMediaTimestamp.store(true, std::memory_order_release);
    }

    std::uint64_t const write = writeState >> 1U;
    std::size_t const writeIndex = static_cast<std::size_t>(write % m_capacityFrames);
    std::size_t const firstFrames = std::min(blockFrames, m_capacityFrames - writeIndex);
    std::copy_n(block.samples.data(), firstFrames * channels, m_samples.data() + writeIndex * channels);
    if (firstFrames != blockFrames) {
        std::copy_n(block.samples.data() + firstFrames * channels, (blockFrames - firstFrames) * channels,
                    m_samples.data());
    }
    std::uint64_t const nextWrite = write + blockFrames;
    if (nextWrite > (std::numeric_limits<std::uint64_t>::max() >> 1U)) {
        return RealtimePcmSubmitResult::Invalid;
    }
    std::uint64_t expectedWriteState = writeState;
    if (stopToken.stop_requested()) {
        if (!hasFirstTimestamp) {
            m_hasFirstMediaTimestamp.store(false, std::memory_order_release);
        }
        return RealtimePcmSubmitResult::Cancelled;
    }
    if (m_epoch.load(std::memory_order_acquire) != epoch ||
        !m_writeState.compare_exchange_strong(expectedWriteState, (nextWrite << 1U) | 1U, std::memory_order_release,
                                              std::memory_order_acquire)) {
        if (!hasFirstTimestamp) {
            m_hasFirstMediaTimestamp.store(false, std::memory_order_release);
        }
        return RealtimePcmSubmitResult::Reset;
    }
    std::size_t const queued = queuedFrames();
    std::size_t observed = m_maximumObservedQueuedFrames.load(std::memory_order_relaxed);
    while (queued > observed &&
           !m_maximumObservedQueuedFrames.compare_exchange_weak(observed, queued, std::memory_order_relaxed)) {}
    m_producedMediaFrames.fetch_add(blockFrames, std::memory_order_release);
    return RealtimePcmSubmitResult::Accepted;
}

RealtimePcmRead RealtimePcmQueue::consume(std::span<float> output, std::size_t requestedFrames) {
    int const channelCount = m_channelCount.load(std::memory_order_relaxed);
    if (channelCount <= 0) {
        return {};
    }
    std::size_t const channels = static_cast<std::size_t>(channelCount);
    Q_ASSERT(output.size() >= requestedFrames * channels);

    std::uint64_t const read = m_readFrame.load(std::memory_order_relaxed);
    std::uint64_t const writeState = m_writeState.load(std::memory_order_acquire);
    if ((writeState & 1U) == 0) {
        std::fill_n(output.data(), requestedFrames * channels, 0.0F);
        return {
            .mediaFrames = 0,
            .silentFrames = requestedFrames,
        };
    }
    std::uint64_t const write = writeState >> 1U;
    std::size_t const available = static_cast<std::size_t>(std::min<std::uint64_t>(write - read, m_capacityFrames));
    std::size_t const mediaFrames = std::min(requestedFrames, available);
    std::size_t const readIndex = static_cast<std::size_t>(read % m_capacityFrames);
    std::size_t const firstFrames = std::min(mediaFrames, m_capacityFrames - readIndex);
    std::copy_n(m_samples.data() + readIndex * channels, firstFrames * channels, output.data());
    if (firstFrames != mediaFrames) {
        std::copy_n(m_samples.data(), (mediaFrames - firstFrames) * channels, output.data() + firstFrames * channels);
    }
    std::fill(output.begin() + static_cast<std::ptrdiff_t>(mediaFrames * channels),
              output.begin() + static_cast<std::ptrdiff_t>(requestedFrames * channels), 0.0F);
    if (mediaFrames != 0) {
        m_readFrame.store(read + mediaFrames, std::memory_order_release);
        m_wakeRevision.fetch_add(1, std::memory_order_release);
        m_wakeRevision.notify_one();
    }
    return {
        .mediaFrames = mediaFrames,
        .silentFrames = requestedFrames - mediaFrames,
    };
}

std::size_t RealtimePcmQueue::capacityFrames() const { return m_capacityFrames; }

std::size_t RealtimePcmQueue::queuedFrames() const {
    std::uint64_t const read = m_readFrame.load(std::memory_order_acquire);
    std::uint64_t const write = m_writeState.load(std::memory_order_acquire) >> 1U;
    if (write < read) {
        return 0;
    }
    return static_cast<std::size_t>(std::min<std::uint64_t>(write - read, m_capacityFrames));
}

std::size_t RealtimePcmQueue::maximumObservedQueuedFrames() const {
    return m_maximumObservedQueuedFrames.load(std::memory_order_relaxed);
}

std::size_t RealtimePcmQueue::waitingProducerCount() const {
    return m_waitingProducerCount.load(std::memory_order_acquire);
}

std::uint64_t RealtimePcmQueue::producedMediaFrames() const {
    return m_producedMediaFrames.load(std::memory_order_acquire);
}

std::optional<std::int64_t> RealtimePcmQueue::firstMediaTimestampMicroseconds() const {
    if (!m_hasFirstMediaTimestamp.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return m_firstMediaTimestampMicroseconds.load(std::memory_order_relaxed);
}

std::size_t RealtimePcmQueue::freeFrames() const { return m_capacityFrames - queuedFrames(); }
