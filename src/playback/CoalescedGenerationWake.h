#pragma once

#include <atomic>
#include <cstdint>
#include <QtGlobal>

// Bounds a cross-thread wake to one queued owner while retaining only the
// newest nonzero generation. drain() closes the producer/consumer handoff
// race before it releases that ownership.
class CoalescedGenerationWake final {
public:
    bool request(std::uint64_t generation) {
        Q_ASSERT(generation != 0);
        m_latestGeneration.store(generation);
        return !m_pending.exchange(true);
    }

    template<typename Handler>
    void drain(Handler &&handler) {
        while (true) {
            const std::uint64_t generation =
                m_latestGeneration.exchange(0);
            if (generation != 0)
                handler(generation);

            m_pending.store(false);
            if (m_latestGeneration.load() == 0
                    || m_pending.exchange(true)) {
                return;
            }
        }
    }

private:
    std::atomic_uint64_t m_latestGeneration = 0;
    std::atomic_bool m_pending = false;
};
