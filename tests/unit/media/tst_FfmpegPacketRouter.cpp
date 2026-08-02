#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

extern "C" {
#include <libavcodec/packet.h>
}

#include "media/ffmpeg/FfmpegPacketRouter.h"

namespace {
using namespace std::chrono_literals;

FfmpegAvPacketPtr packet(int bytes) {
    FfmpegAvPacketPtr result(av_packet_alloc());
    if (!result || av_new_packet(result.get(), bytes) < 0)
        return {};
    return result;
}

template<typename Predicate>
bool waitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}
}

class FfmpegPacketRouterTest final : public QObject {
    Q_OBJECT

public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(
            SEM_FAILCRITICALERRORS
            | SEM_NOGPFAULTERRORBOX
            | SEM_NOOPENFILEERRORBOX);
#endif
    }

private slots:
    void appliesOneAggregateBudgetAcrossStreams();
    void admitsOneOversizedPacketOnlyWhileEmpty();
    void cancellationWakesBlockedOperations();
    void drainsQueuedPacketsBeforeTheTerminal();
};

void FfmpegPacketRouterTest::
appliesOneAggregateBudgetAcrossStreams() {
    FfmpegPacketRouter router({
        .packetCount = 2,
        .packetBytes = 8,
    });
    QVERIFY(router.push(
        FfmpegPacketStream::Video, packet(4)));
    QVERIFY(router.push(
        FfmpegPacketStream::Audio, packet(4)));

    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();
    std::atomic_bool accepted = false;
    std::jthread producer([&](std::stop_token stopToken) {
        accepted = router.push(
            FfmpegPacketStream::Subtitle,
            packet(1),
            stopToken);
        completedPromise.set_value();
    });
    QVERIFY(waitUntil([&] {
        return router.statistics().waitingProducerCount == 1;
    }));
    QVERIFY(completed.wait_for(0ms)
        == std::future_status::timeout);

    FfmpegRoutedPacket audio = router.pop(
        FfmpegPacketStream::Audio);
    QVERIFY(audio.packet);
    QCOMPARE(audio.packet->size, 4);
    QVERIFY(completed.wait_for(2s)
        == std::future_status::ready);
    QVERIFY(accepted.load());
    producer.join();

    FfmpegRoutedPacket subtitle = router.pop(
        FfmpegPacketStream::Subtitle);
    QVERIFY(subtitle.packet);
    QCOMPARE(subtitle.packet->size, 1);

    const FfmpegPacketRouterStatistics statistics =
        router.statistics();
    QCOMPARE(statistics.packetCountLimit, 2U);
    QCOMPARE(statistics.packetByteLimit, 8U);
    QCOMPARE(statistics.maximumQueuedPacketCount, 2U);
    QCOMPARE(statistics.maximumQueuedPacketBytes, 8U);
    QCOMPARE(statistics.largestQueuedPacketBytes, 4U);
}

void FfmpegPacketRouterTest::
admitsOneOversizedPacketOnlyWhileEmpty() {
    FfmpegPacketRouter router({
        .packetCount = 2,
        .packetBytes = 8,
    });
    QVERIFY(router.push(
        FfmpegPacketStream::Video, packet(16)));

    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();
    std::atomic_bool accepted = false;
    std::jthread producer([&](std::stop_token stopToken) {
        accepted = router.push(
            FfmpegPacketStream::Audio,
            packet(1),
            stopToken);
        completedPromise.set_value();
    });
    QVERIFY(waitUntil([&] {
        return router.statistics().waitingProducerCount == 1;
    }));
    QVERIFY(completed.wait_for(0ms)
        == std::future_status::timeout);

    FfmpegRoutedPacket oversized = router.pop(
        FfmpegPacketStream::Video);
    QVERIFY(oversized.packet);
    QCOMPARE(oversized.packet->size, 16);
    QVERIFY(completed.wait_for(2s)
        == std::future_status::ready);
    QVERIFY(accepted.load());
    producer.join();

    const FfmpegPacketRouterStatistics statistics =
        router.statistics();
    QCOMPARE(statistics.maximumQueuedPacketBytes, 16U);
    QCOMPARE(statistics.largestQueuedPacketBytes, 16U);
}

void FfmpegPacketRouterTest::
cancellationWakesBlockedOperations() {
    FfmpegPacketRouter full({
        .packetCount = 1,
        .packetBytes = 4,
    });
    QVERIFY(full.push(
        FfmpegPacketStream::Video, packet(4)));

    std::promise<void> pushCompletedPromise;
    std::future<void> pushCompleted =
        pushCompletedPromise.get_future();
    std::atomic_bool pushed = true;
    std::jthread producer([&](std::stop_token stopToken) {
        pushed = full.push(
            FfmpegPacketStream::Audio,
            packet(1),
            stopToken);
        pushCompletedPromise.set_value();
    });
    QVERIFY(waitUntil([&] {
        return full.statistics().waitingProducerCount == 1;
    }));
    QVERIFY(pushCompleted.wait_for(0ms)
        == std::future_status::timeout);
    producer.request_stop();
    QVERIFY(pushCompleted.wait_for(2s)
        == std::future_status::ready);
    QVERIFY(!pushed.load());
    producer.join();

    FfmpegPacketRouter empty;
    std::promise<FfmpegPacketRouterTerminal>
        popCompletedPromise;
    std::future<FfmpegPacketRouterTerminal> popCompleted =
        popCompletedPromise.get_future();
    std::jthread consumer([&](std::stop_token stopToken) {
        popCompletedPromise.set_value(
            empty.pop(
                FfmpegPacketStream::Audio,
                stopToken)
                .terminal);
    });
    QVERIFY(waitUntil([&] {
        return empty.statistics().waitingConsumerCount == 1;
    }));
    QVERIFY(popCompleted.wait_for(0ms)
        == std::future_status::timeout);
    consumer.request_stop();
    QVERIFY(popCompleted.wait_for(2s)
        == std::future_status::ready);
    QCOMPARE(
        popCompleted.get(),
        FfmpegPacketRouterTerminal::Cancelled);
    consumer.join();
}

void FfmpegPacketRouterTest::
drainsQueuedPacketsBeforeTheTerminal() {
    FfmpegPacketRouter router;
    QVERIFY(router.push(
        FfmpegPacketStream::Video, packet(3)));
    router.finish(FfmpegPacketRouterTerminal::EndOfStream);

    FfmpegRoutedPacket queued = router.pop(
        FfmpegPacketStream::Video);
    QVERIFY(queued.packet);
    QCOMPARE(
        queued.terminal,
        FfmpegPacketRouterTerminal::Open);

    FfmpegRoutedPacket terminal = router.pop(
        FfmpegPacketStream::Video);
    QVERIFY(!terminal.packet);
    QCOMPARE(
        terminal.terminal,
        FfmpegPacketRouterTerminal::EndOfStream);
}

QTEST_APPLESS_MAIN(FfmpegPacketRouterTest)
#include "tst_FfmpegPacketRouter.moc"
