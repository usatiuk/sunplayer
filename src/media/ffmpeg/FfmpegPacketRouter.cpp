#include "media/ffmpeg/FfmpegPacketRouter.h"

#include <algorithm>
#include <utility>

#include <QtGlobal>

extern "C" {
#include <libavcodec/packet.h>
}

FfmpegPacketRouter::FfmpegPacketRouter(FfmpegPacketRouterLimits limits) : m_limits(limits) {
    Q_ASSERT(m_limits.packetCount > 0);
    Q_ASSERT(m_limits.packetBytes > 0);
}

bool FfmpegPacketRouter::push(FfmpegPacketStream stream, FfmpegAvPacketPtr packet, std::stop_token stopToken) {
    Q_ASSERT(packet);
    std::size_t const bytes = packet->size > 0 ? static_cast<std::size_t>(packet->size) : 0;
    std::unique_lock lock(m_mutex);
    ++m_waitingProducerCount;
    bool const ready = m_wake.wait(lock, stopToken, [this, bytes] {
        if (m_terminal != FfmpegPacketRouterTerminal::Open) {
            return true;
        }
        return canAccept(bytes);
    });
    --m_waitingProducerCount;
    if (!ready || m_terminal != FfmpegPacketRouterTerminal::Open) {
        return false;
    }

    enqueue(stream, std::move(packet), bytes);
    lock.unlock();
    m_wake.notify_all();
    return true;
}

FfmpegRoutedPacket FfmpegPacketRouter::pop(FfmpegPacketStream stream, std::stop_token stopToken) {
    std::unique_lock lock(m_mutex);
    auto& selected = queue(stream);
    ++m_waitingConsumerCount;
    bool const ready = m_wake.wait(lock, stopToken,
                                   [&] { return !selected.empty() || m_terminal != FfmpegPacketRouterTerminal::Open; });
    --m_waitingConsumerCount;
    if (!ready) {
        return {
            .terminal = FfmpegPacketRouterTerminal::Cancelled,
        };
    }
    if (!selected.empty()) {
        Entry entry = std::move(selected.front());
        selected.pop_front();
        --m_queuedPacketCount;
        m_queuedBytes -= entry.bytes;
        lock.unlock();
        m_wake.notify_all();
        return {.packet = std::move(entry.packet)};
    }
    return {
        .terminal = m_terminal,
        .error = m_error,
    };
}

void FfmpegPacketRouter::finish(FfmpegPacketRouterTerminal terminal, QString error) {
    Q_ASSERT(terminal != FfmpegPacketRouterTerminal::Open);
    {
        std::lock_guard lock(m_mutex);
        if (m_terminal != FfmpegPacketRouterTerminal::Open) {
            return;
        }
        m_terminal = terminal;
        m_error = std::move(error);
    }
    m_wake.notify_all();
}

FfmpegPacketRouterStatistics FfmpegPacketRouter::statistics() const {
    std::lock_guard lock(m_mutex);
    return {
        .packetCountLimit = m_limits.packetCount,
        .packetByteLimit = m_limits.packetBytes,
        .maximumQueuedPacketCount = m_maximumQueuedPacketCount,
        .maximumQueuedPacketBytes = m_maximumQueuedPacketBytes,
        .largestQueuedPacketBytes = m_largestQueuedPacketBytes,
        .waitingProducerCount = m_waitingProducerCount,
        .waitingConsumerCount = m_waitingConsumerCount,
    };
}

std::deque<FfmpegPacketRouter::Entry>& FfmpegPacketRouter::queue(FfmpegPacketStream stream) {
    switch (stream) {
    case FfmpegPacketStream::Video:
        return m_videoPackets;
    case FfmpegPacketStream::Audio:
        return m_audioPackets;
    case FfmpegPacketStream::Subtitle:
        return m_subtitlePackets;
    }
    Q_UNREACHABLE_RETURN(m_videoPackets);
}

bool FfmpegPacketRouter::canAccept(std::size_t bytes) const {
    bool const countAvailable = m_queuedPacketCount < m_limits.packetCount;
    bool const bytesAvailable =
        m_queuedPacketCount == 0 || bytes <= m_limits.packetBytes - std::min(m_queuedBytes, m_limits.packetBytes);
    return countAvailable && bytesAvailable;
}

void FfmpegPacketRouter::enqueue(FfmpegPacketStream stream, FfmpegAvPacketPtr packet, std::size_t bytes) {
    queue(stream).push_back({
        .packet = std::move(packet),
        .bytes = bytes,
    });
    ++m_queuedPacketCount;
    m_queuedBytes += bytes;
    m_maximumQueuedPacketCount = std::max(m_maximumQueuedPacketCount, m_queuedPacketCount);
    m_maximumQueuedPacketBytes = std::max(m_maximumQueuedPacketBytes, m_queuedBytes);
    m_largestQueuedPacketBytes = std::max(m_largestQueuedPacketBytes, bytes);
}
