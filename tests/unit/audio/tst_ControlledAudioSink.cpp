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
PcmAudioBlock block(std::uint64_t generation, std::uint64_t streamFrame, std::int64_t mediaStart, std::size_t frames,
                    float value) {
    return {
        .playbackGeneration = generation,
        .streamFrameIndex = streamFrame,
        .mediaStartMicroseconds = mediaStart,
        .format = {48'000, 2},
        .samples = std::vector<float>(frames * 2, value),
    };
}
} // namespace

class ControlledAudioSinkTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void separatesSubmittedAndPresentedMediaTime();
    void underrunSilenceHoldsMediaTime();
    void gainChangesSamplesWithoutChangingPresentation();
    void boundsPcmAndWakesBlockedGeneration();
    void reportsGenerationScopedFailure();
};

void ControlledAudioSinkTest::separatesSubmittedAndPresentedMediaTime() {
    ControlledAudioSink sink(960);
    sink.reset(7, {48'000, 2});
    QVERIFY(sink.submit(block(7, 0, 500'000, 480, 0.25F)));
    QVERIFY(sink.submit(block(7, 480, 510'000, 480, 0.5F)));
    QCOMPARE(sink.bufferedFrames(), 960U);

    sink.start();
    ControlledAudioRender const first = sink.render(600);
    QCOMPARE(first.frames, 600U);
    QCOMPARE(first.samples.size(), 1'200U);
    QCOMPARE(sink.submittedFrames(), 600U);
    QCOMPARE(sink.presentedFrames(), 0U);

    ControlledAudioRender const boundedInFlight = sink.render(600);
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
    sink.finish(7);
    sink.advancePresentedFrames(10'000);
    observation = sink.snapshot();
    QCOMPARE(observation.presentedFrames, 960U);
    QCOMPARE(observation.mediaPositionMicroseconds, 520'000);
    QVERIFY(observation.producerFinished);
    QVERIFY(observation.drained);
    QVERIFY(observation.terminalPositionValid);
    QVERIFY(!observation.advancing);

    sink.setPositionAvailable(false);
    observation = sink.snapshot();
    QVERIFY(!observation.valid);
    QVERIFY(observation.terminalPositionValid);
    QCOMPARE(observation.mediaPositionMicroseconds, 520'000);
}

void ControlledAudioSinkTest::underrunSilenceHoldsMediaTime() {
    ControlledAudioSink sink(32);
    sink.reset(9, {48'000, 2});
    QVERIFY(sink.submit(block(9, 0, 400'000, 24, 0.25F)));
    sink.start();
    ControlledAudioAdvance const first = sink.advanceOutput(16);
    QCOMPARE(first.mediaFrames, 16U);
    QCOMPARE(first.holdFrames, 0U);

    AudioPresentationSnapshot const before = sink.snapshot();
    QVERIFY(before.valid);
    QVERIFY(before.audioOutputEpoch != 0);
    QVERIFY(!before.advancing);
    QCOMPARE(before.mediaPositionMicroseconds, 400'333);

    ControlledAudioAdvance const underrun = sink.advanceOutput(488);
    QCOMPARE(underrun.mediaFrames, 8U);
    QCOMPARE(underrun.holdFrames, 480U);
    AudioPresentationSnapshot const held = sink.snapshot();
    QVERIFY(held.valid);
    QVERIFY(held.holding);
    QVERIFY(!held.advancing);
    QCOMPARE(held.submittedFrames, 24U);
    QCOMPARE(held.presentedFrames, 24U);
    QCOMPARE(held.mediaPositionMicroseconds, 400'500);

    AudioSinkDiagnostics const diagnostics = sink.diagnostics();
    QCOMPARE(diagnostics.deviceFramesWritten, 504U);
    QVERIFY(diagnostics.deviceFramesPresented.has_value());
    QCOMPARE(*diagnostics.deviceFramesPresented, 504U);
    QCOMPARE(diagnostics.underrunFrames, 480U);

    QVERIFY(sink.submit(block(9, 24, 400'500, 8, 0.5F)));
    ControlledAudioAdvance const resumedOutput = sink.advanceOutput(4);
    QCOMPARE(resumedOutput.mediaFrames, 4U);
    QCOMPARE(resumedOutput.holdFrames, 0U);
    AudioPresentationSnapshot const resumed = sink.snapshot();
    QVERIFY(!resumed.holding);
    QVERIFY(!resumed.advancing);
    QVERIFY(resumed.mediaPositionMicroseconds > held.mediaPositionMicroseconds);

    std::uint64_t const firstEpoch = resumed.audioOutputEpoch;
    sink.reset(9, {48'000, 2});
    QVERIFY(sink.snapshot().audioOutputEpoch > firstEpoch);
}

void ControlledAudioSinkTest::gainChangesSamplesWithoutChangingPresentation() {
    ControlledAudioSink sink(16);
    sink.setGain(0.5F);
    sink.reset(8, {48'000, 2});
    QVERIFY(sink.submit(block(8, 0, 250'000, 4, 0.8F)));
    QVERIFY(sink.submit(block(8, 4, 250'083, 4, 0.6F)));
    sink.start();

    ControlledAudioRender const attenuated = sink.render(4);
    QCOMPARE(attenuated.frames, 4U);
    QVERIFY(std::all_of(attenuated.samples.cbegin(), attenuated.samples.cend(),
                        [](float sample) { return qFuzzyCompare(sample, 0.4F); }));
    sink.advancePresentedFrames(attenuated.frames);
    std::int64_t const afterAudible = sink.snapshot().mediaPositionMicroseconds;

    sink.setGain(0.0F);
    ControlledAudioRender const muted = sink.render(4);
    QCOMPARE(muted.frames, 4U);
    QVERIFY(std::all_of(muted.samples.cbegin(), muted.samples.cend(), [](float sample) { return sample == 0.0F; }));
    sink.advancePresentedFrames(muted.frames);

    AudioPresentationSnapshot const presentation = sink.snapshot();
    QCOMPARE(presentation.submittedFrames, 8U);
    QCOMPARE(presentation.presentedFrames, 8U);
    QVERIFY(presentation.mediaPositionMicroseconds > afterAudible);
    QVERIFY(!presentation.advancing);

    AudioSinkDiagnostics const diagnostics = sink.diagnostics();
    QCOMPARE(diagnostics.backendName, std::string("controlled"));
    QCOMPARE(diagnostics.queuedFrames, 0U);
    QCOMPARE(diagnostics.mediaFramesSubmitted, 8U);
    QCOMPARE(diagnostics.mediaFramesPresented, 8U);
    QVERIFY(diagnostics.clockReliable);
}

void ControlledAudioSinkTest::boundsPcmAndWakesBlockedGeneration() {
    using namespace std::chrono_literals;

    ControlledAudioSink sink(8);
    sink.reset(11, {48'000, 2});
    sink.finish(10);
    QVERIFY(!sink.snapshot().producerFinished);
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
        accepted = sink.submit(block(11, 8, 166, 1, 0.0F), stopToken);
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
    std::future<void> cancellationReady = cancelledStarted.get_future();
    std::promise<void> cancelledCompleted;
    std::future<void> cancellationCompleted = cancelledCompleted.get_future();
    std::atomic_bool cancellationAccepted = true;
    std::jthread cancelledProducer([&](std::stop_token stopToken) {
        cancelledStarted.set_value();
        cancellationAccepted = sink.submit(block(12, 8, 166, 1, 0.0F), stopToken);
        cancelledCompleted.set_value();
    });
    cancellationReady.wait();
    QVERIFY(cancellationCompleted.wait_for(0ms) == std::future_status::timeout);
    cancelledProducer.request_stop();
    QVERIFY(cancellationCompleted.wait_for(2s) == std::future_status::ready);
    QVERIFY(!cancellationAccepted.load());
    cancelledProducer.join();

    std::promise<void> invalidatedStarted;
    std::future<void> invalidatedReady = invalidatedStarted.get_future();
    std::promise<void> invalidatedCompleted;
    std::future<void> invalidationCompleted = invalidatedCompleted.get_future();
    std::atomic_bool invalidatedAccepted = true;
    std::jthread invalidatedProducer([&](std::stop_token stopToken) {
        invalidatedStarted.set_value();
        invalidatedAccepted = sink.submit(block(12, 8, 166, 1, 0.0F), stopToken);
        invalidatedCompleted.set_value();
    });
    invalidatedReady.wait();
    QVERIFY(invalidationCompleted.wait_for(0ms) == std::future_status::timeout);
    sink.cancel(11);
    QVERIFY(invalidationCompleted.wait_for(0ms) == std::future_status::timeout);
    sink.cancel(12);
    QVERIFY(invalidationCompleted.wait_for(2s) == std::future_status::ready);
    QVERIFY(!invalidatedAccepted.load());
    QVERIFY(!sink.snapshot().valid);
    invalidatedProducer.join();

    QCOMPARE(sink.maximumObservedBufferedFrames(), 8U);
}

void ControlledAudioSinkTest::reportsGenerationScopedFailure() {
    ControlledAudioSink sink(16);
    sink.reset(21, {48'000, 2});
    QVERIFY(sink.submit(block(21, 0, 0, 8, 0.25F)));
    sink.start();

    sink.fail(20, "stale failure");
    QVERIFY(!sink.snapshot().failed);
    QVERIFY(sink.failureReason().empty());

    sink.fail(21, "device disappeared");
    AudioPresentationSnapshot const failed = sink.snapshot();
    QVERIFY(failed.failed);
    QVERIFY(!failed.valid);
    QVERIFY(!failed.advancing);
    QCOMPARE(QString::fromStdString(sink.failureReason()), QStringLiteral("device disappeared"));
    QCOMPARE(sink.render(8).frames, 0U);
    QVERIFY(!sink.submit(block(21, 8, 166, 1, 0.25F)));

    sink.reset(22, {48'000, 2});
    QVERIFY(!sink.snapshot().failed);
    QVERIFY(sink.failureReason().empty());
}

QTEST_APPLESS_MAIN(ControlledAudioSinkTest)
#include "tst_ControlledAudioSink.moc"
