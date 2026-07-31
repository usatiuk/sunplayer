#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "audio/ControlledAudioSink.h"

namespace {
PcmAudioBlock block(
        std::uint64_t generation,
        std::uint64_t streamFrame,
        std::int64_t mediaStart,
        std::size_t frames,
        float value) {
    return {
        .playbackGeneration = generation,
        .streamFrameIndex = streamFrame,
        .mediaStartMicroseconds = mediaStart,
        .format = {48'000, 2},
        .samples = std::vector<float>(frames * 2, value),
    };
}
}

class ControlledAudioSinkTest final : public QObject {
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
    void separatesSubmittedAndPresentedMediaTime();
    void boundsPcmAndWakesBlockedGeneration();
};

void ControlledAudioSinkTest::
separatesSubmittedAndPresentedMediaTime() {
    ControlledAudioSink sink(960);
    sink.reset(7, {48'000, 2});
    QVERIFY(sink.submit(block(7, 0, 500'000, 480, 0.25F)));
    QVERIFY(sink.submit(block(7, 480, 510'000, 480, 0.5F)));
    QCOMPARE(sink.bufferedFrames(), 960U);

    sink.start();
    const ControlledAudioRender first = sink.render(600);
    QCOMPARE(first.frames, 600U);
    QCOMPARE(first.samples.size(), 1'200U);
    QCOMPARE(sink.submittedFrames(), 600U);
    QCOMPARE(sink.presentedFrames(), 0U);

    const ControlledAudioRender boundedInFlight = sink.render(600);
    QCOMPARE(boundedInFlight.frames, 360U);
    QCOMPARE(sink.submittedFrames(), 960U);
    QCOMPARE(sink.render(1).frames, 0U);

    AudioPresentationSnapshot observation = sink.snapshot();
    QVERIFY(observation.valid);
    QCOMPARE(observation.submittedFrames, 960U);
    QCOMPARE(observation.presentedFrames, 0U);
    QCOMPARE(observation.mediaPositionMicroseconds, 500'000);
    QVERIFY(observation.advancing);
    QVERIFY(!observation.producerFinished);
    QVERIFY(!observation.drained);

    sink.advancePresentedFrames(240);
    observation = sink.snapshot();
    QCOMPARE(observation.presentedFrames, 240U);
    QCOMPARE(observation.mediaPositionMicroseconds, 505'000);

    sink.pause();
    sink.advancePresentedFrames(240);
    QCOMPARE(sink.presentedFrames(), 240U);
    QVERIFY(!sink.snapshot().advancing);

    sink.start();
    sink.finish();
    sink.advancePresentedFrames(10'000);
    observation = sink.snapshot();
    QCOMPARE(observation.presentedFrames, 960U);
    QCOMPARE(observation.mediaPositionMicroseconds, 520'000);
    QVERIFY(observation.producerFinished);
    QVERIFY(observation.drained);
    QVERIFY(!observation.advancing);
}

void ControlledAudioSinkTest::
boundsPcmAndWakesBlockedGeneration() {
    using namespace std::chrono_literals;

    ControlledAudioSink sink(8);
    sink.reset(11, {48'000, 2});
    QVERIFY(sink.submit(block(11, 0, 0, 8, 0.0F)));
    sink.start();
    QCOMPARE(sink.render(8).frames, 8U);

    std::promise<void> producerStarted;
    std::future<void> started = producerStarted.get_future();
    std::promise<void> producerCompleted;
    std::future<void> completed = producerCompleted.get_future();
    std::atomic_bool accepted = true;
    std::jthread producer([&](std::stop_token stopToken) {
        producerStarted.set_value();
        accepted = sink.submit(
            block(11, 8, 166, 1, 0.0F),
            stopToken);
        producerCompleted.set_value();
    });
    started.wait();
    QVERIFY(completed.wait_for(0ms) == std::future_status::timeout);

    sink.reset(12, {48'000, 2});
    QVERIFY(completed.wait_for(2s) == std::future_status::ready);
    QVERIFY(!accepted.load());
    producer.join();

    QVERIFY(sink.submit(block(12, 0, 0, 8, 0.0F)));
    sink.start();
    QCOMPARE(sink.render(8).frames, 8U);
    std::promise<void> cancelledStarted;
    std::future<void> cancellationReady =
        cancelledStarted.get_future();
    std::promise<void> cancelledCompleted;
    std::future<void> cancellationCompleted =
        cancelledCompleted.get_future();
    std::atomic_bool cancellationAccepted = true;
    std::jthread cancelledProducer(
        [&](std::stop_token stopToken) {
            cancelledStarted.set_value();
            cancellationAccepted = sink.submit(
                block(12, 8, 166, 1, 0.0F),
                stopToken);
            cancelledCompleted.set_value();
        });
    cancellationReady.wait();
    QVERIFY(cancellationCompleted.wait_for(0ms)
        == std::future_status::timeout);
    cancelledProducer.request_stop();
    QVERIFY(cancellationCompleted.wait_for(2s)
        == std::future_status::ready);
    QVERIFY(!cancellationAccepted.load());
    cancelledProducer.join();

    QCOMPARE(sink.maximumObservedBufferedFrames(), 8U);
}

QTEST_APPLESS_MAIN(ControlledAudioSinkTest)
#include "tst_ControlledAudioSink.moc"
