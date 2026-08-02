#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stop_token>

#include <QString>

#include "media/ffmpeg/FfmpegVideoPacketDecoder.h"

enum class FfmpegPacketStream {
    Video,
    Audio,
    Subtitle,
};

enum class FfmpegPacketRouterTerminal {
    Open,
    EndOfStream,
    Failed,
    Cancelled,
};

struct FfmpegRoutedPacket {
    FfmpegAvPacketPtr packet;
    FfmpegPacketRouterTerminal terminal =
        FfmpegPacketRouterTerminal::Open;
    QString error;
};

struct FfmpegPacketRouterLimits {
    std::size_t packetCount = 8'192;
    std::size_t packetBytes = 512U * 1024U * 1024U;
};

struct FfmpegPacketRouterStatistics {
    std::size_t packetCountLimit = 0;
    std::size_t packetByteLimit = 0;
    std::size_t maximumQueuedPacketCount = 0;
    std::size_t maximumQueuedPacketBytes = 0;
    std::size_t largestQueuedPacketBytes = 0;
    std::size_t waitingProducerCount = 0;
    std::size_t waitingConsumerCount = 0;
};

// Routes selected media streams through one aggregate memory budget. A
// single packet larger than the byte limit is admitted only while the router
// is empty, so unusual valid packets cannot deadlock demuxing while memory
// remains bounded by max(limit, largest packet).
class FfmpegPacketRouter final {
public:
    explicit FfmpegPacketRouter(
        FfmpegPacketRouterLimits limits = {});

    bool push(
        FfmpegPacketStream stream,
        FfmpegAvPacketPtr packet,
        std::stop_token stopToken = {});
    FfmpegRoutedPacket pop(
        FfmpegPacketStream stream,
        std::stop_token stopToken = {});
    void finish(
        FfmpegPacketRouterTerminal terminal,
        QString error = {});

    FfmpegPacketRouterStatistics statistics() const;

private:
    struct Entry {
        FfmpegAvPacketPtr packet;
        std::size_t bytes = 0;
    };

    std::deque<Entry> &queue(FfmpegPacketStream stream);
    bool canAccept(std::size_t bytes) const;
    void enqueue(
        FfmpegPacketStream stream,
        FfmpegAvPacketPtr packet,
        std::size_t bytes);

    FfmpegPacketRouterLimits m_limits;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Entry> m_videoPackets;
    std::deque<Entry> m_audioPackets;
    std::deque<Entry> m_subtitlePackets;
    std::size_t m_queuedPacketCount = 0;
    std::size_t m_queuedBytes = 0;
    std::size_t m_maximumQueuedPacketCount = 0;
    std::size_t m_maximumQueuedPacketBytes = 0;
    std::size_t m_largestQueuedPacketBytes = 0;
    std::size_t m_waitingProducerCount = 0;
    std::size_t m_waitingConsumerCount = 0;
    FfmpegPacketRouterTerminal m_terminal =
        FfmpegPacketRouterTerminal::Open;
    QString m_error;
};
