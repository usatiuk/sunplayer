#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "audio/CubebAudioSink.h"

namespace {
using namespace std::chrono_literals;

PcmAudioBlock silentBlock(
        std::uint64_t generation,
        std::uint64_t streamFrame,
        std::size_t frameCount) {
    return {
        .playbackGeneration = generation,
        .streamFrameIndex = streamFrame,
        .mediaStartMicroseconds = static_cast<std::int64_t>(
            streamFrame * 1'000'000ULL / 48'000ULL),
        .format = {48'000, 2},
        .samples = std::vector<float>(frameCount * 2, 0.0F),
    };
}

template<typename Predicate>
bool waitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

bool remainsUndrainedFor(
        const CubebAudioSink &sink,
        std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sink.snapshot().drained)
            return false;
        std::this_thread::yield();
    }
    return true;
}
}

class CubebAudioSinkTest final : public QObject {
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
    void opensDefaultOutputWithoutStartingPlayback();
    void rejectsABlockThatCouldDeadlockPreroll();
    void startsPausesAndDrainsSilentPcm();
};

void CubebAudioSinkTest::
opensDefaultOutputWithoutStartingPlayback() {
    CubebAudioSink sink(48'000);
    sink.reset(9, {48'000, 2});

    const AudioSinkDiagnostics diagnostics =
        sink.diagnostics();
    QVERIFY2(
        diagnostics.errorMessage.empty(),
        diagnostics.errorMessage.c_str());
    QCOMPARE(diagnostics.backendName, std::string("wasapi"));
    QVERIFY(diagnostics.streamOpen);
    QCOMPARE(
        diagnostics.format,
        (AudioStreamFormat{48'000, 2}));
    QCOMPARE(diagnostics.queueCapacityFrames, 48'000U);
    QVERIFY(diagnostics.requestedLatencyFrames != 0);
    QVERIFY(diagnostics.maximumSubmitFrames != 0);
    QVERIFY(diagnostics.deviceNotificationsAvailable);
    QCOMPARE(
        diagnostics.positionAvailable,
        diagnostics.deviceFramesPresented.has_value());
    QCOMPARE(diagnostics.underrunFrames, 0U);

    const AudioPresentationSnapshot snapshot = sink.snapshot();
    QCOMPARE(snapshot.playbackGeneration, 9U);
    QVERIFY(!snapshot.valid);
    QVERIFY(!snapshot.advancing);

    sink.cancel(8);
    QVERIFY(sink.diagnostics().streamOpen);
    sink.cancel(9);
    QVERIFY(!sink.diagnostics().streamOpen);
    QVERIFY(!sink.snapshot().valid);
}

void CubebAudioSinkTest::
rejectsABlockThatCouldDeadlockPreroll() {
    CubebAudioSink sink(100);
    sink.reset(10, {48'000, 2});
    QCOMPARE(sink.diagnostics().maximumSubmitFrames, 50U);
    QVERIFY(sink.submit(silentBlock(10, 0, 40)));

    auto blocked = std::async(std::launch::async, [&sink] {
        return sink.submit(silentBlock(10, 40, 70));
    });
    if (blocked.wait_for(2s) != std::future_status::ready) {
        sink.reset(11, {48'000, 2});
        blocked.wait();
        QFAIL("an oversized pre-preroll block blocked submission");
    }
    QVERIFY(!blocked.get());
}

void CubebAudioSinkTest::startsPausesAndDrainsSilentPcm() {
    CubebAudioSink sink(48'000);
    sink.reset(20, {48'000, 2});
    sink.start();
    QVERIFY(sink.submit(silentBlock(20, 0, 12'000)));
    sink.finish(20);

    QVERIFY(waitUntil([&] {
        const AudioPresentationSnapshot current = sink.snapshot();
        return current.valid && current.presentedFrames != 0;
    }));
    QVERIFY(waitUntil([&] { return sink.snapshot().drained; }));
    const AudioPresentationSnapshot drained = sink.snapshot();
    QCOMPARE(drained.playbackGeneration, 20U);
    QCOMPARE(drained.submittedFrames, 12'000U);
    QCOMPARE(drained.presentedFrames, 12'000U);
    QVERIFY(drained.valid || drained.terminalPositionValid);
    QVERIFY(drained.drained);
    QVERIFY(drained.terminalPositionValid);
    QVERIFY(!drained.advancing);

    const AudioSinkDiagnostics drainedDiagnostics =
        sink.diagnostics();
    QCOMPARE(drainedDiagnostics.mediaFramesSubmitted, 12'000U);
    QCOMPARE(drainedDiagnostics.mediaFramesPresented, 12'000U);
    QVERIFY(drainedDiagnostics.deviceFramesPresented.has_value());
    QVERIFY(drainedDiagnostics.clockReliable);

    sink.reset(21, {48'000, 2});
    sink.finish(20);
    QVERIFY(!sink.snapshot().producerFinished);
    sink.start();
    QVERIFY(sink.submit(silentBlock(21, 0, 24'000)));
    sink.finish(21);
    QVERIFY(waitUntil([&] {
        const AudioPresentationSnapshot current = sink.snapshot();
        return current.advancing
            && current.presentedFrames < current.submittedFrames;
    }));
    sink.pause();
    QVERIFY(remainsUndrainedFor(sink, 100ms));
    const AudioPresentationSnapshot paused = sink.snapshot();
    QVERIFY(paused.producerFinished);
    QVERIFY(!paused.advancing);
    QVERIFY(!paused.drained);
    QVERIFY(sink.diagnostics().streamOpen);
    sink.start();
    QVERIFY(waitUntil([&] { return sink.snapshot().drained; }));
}

QTEST_APPLESS_MAIN(CubebAudioSinkTest)
#include "tst_CubebAudioSink.moc"
