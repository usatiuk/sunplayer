#include <atomic>
#include <array>
#include <chrono>
#include <future>
#include <thread>

#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "audio/RealtimePcmQueue.h"

namespace {
using namespace std::chrono_literals;

PcmAudioBlock block(
        std::uint64_t generation,
        std::uint64_t streamFrame,
        std::int64_t mediaStart,
        std::initializer_list<float> samples) {
    return {
        .playbackGeneration = generation,
        .streamFrameIndex = streamFrame,
        .mediaStartMicroseconds = mediaStart,
        .format = {48'000, 2},
        .samples = samples,
    };
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

class RealtimePcmQueueTest final : public QObject {
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
    void preservesSamplesAcrossRingWrapAndZeroFills();
    void boundsCapacityAndWakesOnStopOrReset();
    void cancellationDoesNotPartiallyPublishABlock();
    void rejectsGenerationAndTimelineDiscontinuities();
};

void RealtimePcmQueueTest::
preservesSamplesAcrossRingWrapAndZeroFills() {
    RealtimePcmQueue queue(4, 2);
    queue.reset(3, {48'000, 2});
    QVERIFY(queue.submit(block(
        3,
        0,
        500'000,
        {0.0F, 1.0F, 2.0F, 3.0F,
         4.0F, 5.0F, 6.0F, 7.0F})));

    std::array<float, 6> first{};
    const RealtimePcmRead firstRead = queue.consume(first, 3);
    QCOMPARE(firstRead.mediaFrames, 3U);
    QCOMPARE(firstRead.silentFrames, 0U);
    QCOMPARE(first, (std::array<float, 6>{
        0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F}));

    QVERIFY(queue.submit(block(
        3, 4, 500'083, {8.0F, 9.0F})));
    std::array<float, 8> wrapped{};
    const RealtimePcmRead wrappedRead = queue.consume(
        wrapped, 4);
    QCOMPARE(wrappedRead.mediaFrames, 2U);
    QCOMPARE(wrappedRead.silentFrames, 2U);
    QCOMPARE(wrapped, (std::array<float, 8>{
        6.0F, 7.0F, 8.0F, 9.0F,
        0.0F, 0.0F, 0.0F, 0.0F}));
    QCOMPARE(queue.queuedFrames(), 0U);
    QCOMPARE(queue.maximumObservedQueuedFrames(), 4U);
    QCOMPARE(queue.producedMediaFrames(), 5U);
    QCOMPARE(
        queue.firstMediaTimestampMicroseconds(),
        std::optional<std::int64_t>(500'000));
}

void RealtimePcmQueueTest::
boundsCapacityAndWakesOnStopOrReset() {
    RealtimePcmQueue queue(2, 2);
    queue.reset(7, {48'000, 2});
    QVERIFY(queue.submit(block(
        7, 0, 0, {0.0F, 0.0F, 1.0F, 1.0F})));

    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();
    std::atomic_bool accepted = true;
    std::jthread producer([&](std::stop_token stopToken) {
        accepted = queue.submit(block(
            7, 2, 41, {2.0F, 2.0F}), stopToken);
        completedPromise.set_value();
    });
    QVERIFY(waitUntil([&] {
        return queue.waitingProducerCount() == 1;
    }));
    producer.request_stop();
    QVERIFY(completed.wait_for(2s)
        == std::future_status::ready);
    QVERIFY(!accepted.load());
    producer.join();

    std::promise<void> resetCompletedPromise;
    std::future<void> resetCompleted =
        resetCompletedPromise.get_future();
    std::atomic_bool resetAccepted = true;
    std::jthread resetProducer([&] {
        resetAccepted = queue.submit(block(
            7, 2, 41, {2.0F, 2.0F}));
        resetCompletedPromise.set_value();
    });
    QVERIFY(waitUntil([&] {
        return queue.waitingProducerCount() == 1;
    }));
    queue.reset(8, {48'000, 2});
    QVERIFY(resetCompleted.wait_for(2s)
        == std::future_status::ready);
    QVERIFY(!resetAccepted.load());
    resetProducer.join();

    queue.reset(9, {48'000, 2});
    QVERIFY(queue.submit(block(
        9, 0, 0, {0.0F, 0.0F, 1.0F, 1.0F})));
    std::promise<RealtimePcmSubmitResult> cancelPromise;
    auto cancelled = cancelPromise.get_future();
    std::jthread cancelledProducer([&] {
        cancelPromise.set_value(queue.submitResult(block(
            9, 2, 41, {2.0F, 2.0F})));
    });
    QVERIFY(waitUntil([&] {
        return queue.waitingProducerCount() == 1;
    }));
    queue.cancel();
    QCOMPARE(cancelled.get(), RealtimePcmSubmitResult::Reset);
    cancelledProducer.join();
    QCOMPARE(queue.waitingProducerCount(), 0U);
    QCOMPARE(
        queue.submitResult(block(
            9, 2, 41, {2.0F, 2.0F})),
        RealtimePcmSubmitResult::Reset);
    std::array<float, 4> cancelledOutput{};
    const RealtimePcmRead cancelledRead = queue.consume(
        cancelledOutput, 2);
    QCOMPARE(cancelledRead.mediaFrames, 0U);
    QCOMPARE(cancelledRead.silentFrames, 2U);
}

void RealtimePcmQueueTest::
cancellationDoesNotPartiallyPublishABlock() {
    RealtimePcmQueue queue(4, 2);
    queue.reset(11, {48'000, 2});
    QVERIFY(queue.submit(block(
        11,
        0,
        250'000,
        {0.0F, 0.0F, 1.0F, 1.0F})));

    std::stop_source stopSource;
    std::promise<RealtimePcmSubmitResult> resultPromise;
    auto result = resultPromise.get_future();
    std::jthread producer([&] {
        resultPromise.set_value(queue.submitResult(block(
            11,
            2,
            250'041,
            {2.0F, 2.0F, 3.0F, 3.0F,
             4.0F, 4.0F}), stopSource.get_token()));
    });
    QVERIFY(waitUntil([&] {
        return queue.waitingProducerCount() == 1;
    }));
    stopSource.request_stop();
    QCOMPARE(result.get(), RealtimePcmSubmitResult::Cancelled);
    producer.join();

    QCOMPARE(queue.queuedFrames(), 2U);
    QCOMPARE(queue.producedMediaFrames(), 2U);
    std::array<float, 8> output{};
    const RealtimePcmRead read = queue.consume(output, 4);
    QCOMPARE(read.mediaFrames, 2U);
    QCOMPARE(read.silentFrames, 2U);
    QCOMPARE(output, (std::array<float, 8>{
        0.0F, 0.0F, 1.0F, 1.0F,
        0.0F, 0.0F, 0.0F, 0.0F}));

    queue.reset(12, {48'000, 2});
    QVERIFY(!queue.submit(block(
        12,
        0,
        700'000,
        {0.0F, 0.0F, 1.0F, 1.0F,
         2.0F, 2.0F, 3.0F, 3.0F,
         4.0F, 4.0F})));
    QVERIFY(!queue.firstMediaTimestampMicroseconds());
}

void RealtimePcmQueueTest::
rejectsGenerationAndTimelineDiscontinuities() {
    RealtimePcmQueue queue(8, 2);
    queue.reset(5, {48'000, 2});
    QCOMPARE(
        queue.submitResult(block(
            4, 0, 0, {0.0F, 0.0F})),
        RealtimePcmSubmitResult::Stale);
    QVERIFY(queue.submit(block(
        5, 0, 100'000, {0.0F, 0.0F})));
    QVERIFY(!queue.submit(block(
        5, 2, 100'041, {1.0F, 1.0F})));
    QVERIFY(!queue.submit(block(
        5, 1, 200'000, {1.0F, 1.0F})));
}

QTEST_APPLESS_MAIN(RealtimePcmQueueTest)
#include "tst_RealtimePcmQueue.moc"
