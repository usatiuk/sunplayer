#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>

#include <QCryptographicHash>
#include <QFile>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "media/DecodedVideoFrame.h"
#include "audio/ControlledAudioSink.h"
#include "playback/MediaSession.h"

namespace {
QString fixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1.mkv");
}

QString playbackFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-playback.mkv");
}

QString synchronizedFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-flac-sync.mkv");
}

QString shortAudioFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-short-flac.mkv");
}

QString audioLateFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-audio-late-long-flac.mkv");
}

QString videoLateFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-video-late-flac.mkv");
}

QString longVideoTailFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-ffv1-short-audio-long-video-flac.mkv");
}

QString interFrameSeekFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-bt709-h264-seek.mkv");
}

QString replacementFixturePath() {
    return QStringLiteral(
        SUNROOM_TEST_FIXTURE_DIR
        "/media/sdr-rgb-first-frame.ppm");
}

FfmpegVideoDecodeResult cancelledResult() {
    FfmpegVideoDecodeResult result;
    result.cancelled = true;
    return result;
}

struct BlockingOperation {
    std::atomic_bool started = false;
    std::atomic_bool stopped = false;
    std::mutex mutex;
    std::condition_variable_any wake;

    FfmpegVideoDecodeResult wait(
            std::stop_token stopToken) {
        started = true;
        std::unique_lock lock(mutex);
        wake.wait(lock, stopToken, [] {
            return false;
        });
        stopped = true;
        return cancelledResult();
    }
};

struct DelayedStopOperation {
    std::atomic_bool started = false;
    std::atomic_bool stopObserved = false;
    std::atomic_bool exited = false;
    std::mutex mutex;
    std::condition_variable_any wake;
    bool exitAllowed = false;

    FfmpegVideoDecodeResult wait(
            std::stop_token stopToken) {
        started = true;
        std::unique_lock lock(mutex);
        wake.wait(lock, stopToken, [] {
            return false;
        });
        stopObserved = true;
        wake.wait(lock, [this] {
            return exitAllowed;
        });
        exited = true;
        return cancelledResult();
    }

    void allowExit() {
        {
            std::lock_guard lock(mutex);
            exitAllowed = true;
        }
        wake.notify_all();
    }
};

struct OperationGate {
    std::mutex mutex;
    std::condition_variable_any wake;
    bool open = false;

    bool wait(std::stop_token stopToken) {
        std::unique_lock lock(mutex);
        return wake.wait(
            lock,
            stopToken,
            [this] {
                return open;
            });
    }

    void release() {
        {
            std::lock_guard lock(mutex);
            open = true;
        }
        wake.notify_all();
    }
};
}

class MediaSessionTest final : public QObject {
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
    void opensRealMediaOffThread();
    void synchronizedPlaybackUsesPresentedAudioClock();
    void staggeredStarts_data();
    void staggeredStarts();
    void playbackProgressDoesNotRequirePresentationConsumer();
    void trailingVideoContinuesAfterAudioDrains();
    void longPostAudioSeekDoesNotStall();
    void audioOutputFailureBecomesSessionError();
    void unavailableAudioClockBecomesSessionError();
    void readyNotificationCanCancelWithoutRepublishing();
    void openingNotificationKeepsNewestRequest();
    void continuousPlaybackIsBoundedAndPauseable();
    void seekPreservesTimelineAndPlayIntent();
    void interFrameSeekPublishesRequestedFrame();
    void futureSeekFrameAdvancesClockAnchor();
    void newerSeekRejectsStaleCompletion();
    void cancelDuringSeekClearsSession();
    void seekFailureClearsSeekingState();
    void nonseekableReplayReadsNaturallyFromStart();
    void dropsSupersededDueFrames();
    void rejectsNonLocalUrls();
    void newerOpenRejectsStaleCompletion();
    void cancelReturnsBeforeWorkerExit();
    void destructionCancelsWorker();
    void presentationFailureBecomesSessionError();
    void hardwareImportFailureRetriesSoftware();
    void hardwareImportFallbackRestartsAtPosition();
    void graphicsRecoveryRestartsSoftwareAtPosition();
    void graphicsRecoveryPreservesPendingSeek();
    void graphicsRecoverySupersedesOpening();
};

void MediaSessionTest::opensRealMediaOffThread() {
    QThread *const ownerThread =
        QThread::currentThread();
    std::atomic_bool decodedOffOwnerThread = false;
    std::atomic_int hardwareFrameReserve = 0;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [ownerThread,
         &decodedOffOwnerThread,
         &hardwareFrameReserve](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            decodedOffOwnerThread =
                QThread::currentThread() != ownerThread;
            hardwareFrameReserve =
                request.extraHardwareFrames;
            return decodeVideoFrames(
                request,
                sink,
                stopToken);
        });
    QSignalSpy changes(
        &session, &MediaSession::sessionChanged);
    bool publishedReadyWithoutFrame = false;
    connect(
        &session,
        &MediaSession::sessionChanged,
        &session,
        [&] {
            if (session.state()
                        == MediaSession::State::Ready
                    && !session.hasFrame()) {
                publishedReadyWithoutFrame = true;
            }
        });

    session.openMedia(
        QUrl::fromLocalFile(fixturePath()));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());

    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.hasFrame());
    QVERIFY(!publishedReadyWithoutFrame);
    QVERIFY(decodedOffOwnerThread.load());
    QCOMPARE(
        hardwareFrameReserve.load(),
        static_cast<int>(
            VideoFrameQueue::capacity + 2));
    QVERIFY(changes.count() >= 2);
    QCOMPARE(session.decoderName(), QStringLiteral("ffv1"));
    QCOMPARE(session.decodePath(), QStringLiteral("Software"));
    QVERIFY(session.hardwareFallbackReason().isEmpty());
    QVERIFY(!session.containerFormat().isEmpty());
    QVERIFY(session.videoSummary().contains(
        QStringLiteral("yuv420p")));
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->identity()
            .playbackGeneration,
        session.playbackGeneration());
}

void MediaSessionTest::
synchronizedPlaybackUsesPresentedAudioClock() {
    auto audioSink =
        std::make_shared<ControlledAudioSink>(4'096);
    std::atomic_int decodeOperations = 0;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&decodeOperations](
                const FfmpegMediaDecodeRequest &request,
                const FfmpegVideoFrameSink &videoSink,
                const FfmpegAudioOutputSink &audioOutput,
                const FfmpegMediaStreamSink &streamSink,
                std::stop_token stopToken) {
            ++decodeOperations;
            return decodeMediaFrames(
                request,
                videoSink,
                audioOutput,
                streamSink,
                stopToken);
        },
        audioSink);

    const auto servicePlayback = [&] {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        const ControlledAudioRender rendered =
            audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(
                rendered.frames);
        }
        session.videoSource().prepareForPresentation(
            std::chrono::steady_clock::now()
                + std::chrono::hours(24));
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        return rendered.frames;
    };
    const auto waitUntil = [&](auto predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs) {
            servicePlayback();
            QThread::yieldCurrentThread();
        }
        return predicate();
    };

    session.openMedia(
        QUrl::fromLocalFile(synchronizedFixturePath()));
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready
                && session.hasFrame();
        },
        10'000));
    QCOMPARE(decodeOperations.load(), 1);
    QVERIFY(session.hasFrame());
    QVERIFY(session.playing());

    for (int iteration = 0; iteration != 20; ++iteration) {
        session.videoSource().prepareForPresentation(
            std::chrono::steady_clock::now()
                + std::chrono::hours(24));
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 1);
    }
    const qlonglong stablePosition =
        session.positionMilliseconds();
    const std::uint64_t stableFrame = session.videoSource()
        .currentFrame()->identity().frameId;
    for (int iteration = 0; iteration != 20; ++iteration) {
        session.videoSource().prepareForPresentation(
            std::chrono::steady_clock::now()
                + std::chrono::hours(24));
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 1);
    }
    QCOMPARE(session.positionMilliseconds(), stablePosition);
    QCOMPARE(
        session.videoSource().currentFrame()->identity().frameId,
        stableFrame);

    const std::uint64_t advanceTarget =
        audioSink->presentedFrames() + 12'000;
    QVERIFY(waitUntil(
        [&] {
            return audioSink->presentedFrames()
                >= advanceTarget;
        },
        10'000));
    const qlonglong expectedAudioPosition =
        static_cast<qlonglong>(
            audioSink->presentedFrames() * 1'000 / 48'000);
    QVERIFY(std::abs(
        session.positionMilliseconds()
            - expectedAudioPosition) <= 1);
    QVERIFY(session.positionMilliseconds() > stablePosition);

    ControlledAudioRender submittedNotPresented;
    QElapsedTimer submitTimer;
    submitTimer.start();
    while (submittedNotPresented.frames == 0
            && submitTimer.elapsed() < 3'000) {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        submittedNotPresented = audioSink->render(257);
        QThread::yieldCurrentThread();
    }
    QVERIFY(submittedNotPresented.frames != 0);
    const AudioPresentationSnapshot inFlight =
        audioSink->snapshot();
    QVERIFY(inFlight.submittedFrames > inFlight.presentedFrames);
    session.videoSource().prepareForPresentation(
        std::chrono::steady_clock::now()
            + std::chrono::hours(24));
    QCOMPARE(
        session.positionMilliseconds(),
        static_cast<qlonglong>(
            inFlight.presentedFrames * 1'000 / 48'000));
    audioSink->advancePresentedFrames(
        submittedNotPresented.frames);

    session.pause();
    const qlonglong pausedPosition =
        session.positionMilliseconds();
    QCOMPARE(servicePlayback(), 0U);
    QCOMPARE(session.positionMilliseconds(), pausedPosition);

    session.seekToMilliseconds(1'250);
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready
                && session.hasFrame();
        },
        10'000));
    QCOMPARE(decodeOperations.load(), 2);
    QVERIFY(!session.playing());
    QCOMPARE(session.positionMilliseconds(), 1'250);
    QCOMPARE(
        session.videoSource().currentFrame()
            ->identity().playbackGeneration,
        session.playbackGeneration());

    QTest::qWait(1'200);
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(!session.playing());
    QVERIFY(session.errorMessage().isEmpty());

    session.play();
    QVERIFY(waitUntil(
        [&] { return session.ended(); },
        15'000));
    QCOMPARE(decodeOperations.load(), 2);
    QVERIFY(audioSink->snapshot().drained);
    QCOMPARE(session.positionMilliseconds(), 3'000);
    QVERIFY(!session.playing());
}

void MediaSessionTest::staggeredStarts_data() {
    QTest::addColumn<QString>("path");
    QTest::addColumn<QByteArray>("sha256");
    QTest::addColumn<bool>("videoStartsLate");

    QTest::newRow("audio-starts-four-seconds-late-at-60fps")
        << audioLateFixturePath()
        << QByteArray(
            "4d1a1969a9e5b24a9a5e7eacd788d537"
            "ef5db9a882dcb5a303a73d0c5cc4f6d4")
        << false;
    QTest::newRow("video-starts-one-second-late")
        << videoLateFixturePath()
        << QByteArray(
            "3182e5e996c147ef817a7b9b43ff63e3"
            "e3b1f110111372ef6ff8ad34bcf5685d")
        << true;
}

void MediaSessionTest::staggeredStarts() {
    QFETCH(QString, path);
    QFETCH(QByteArray, sha256);
    QFETCH(bool, videoStartsLate);

    QFile fixture(path);
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QCOMPARE(
        QCryptographicHash::hash(
            fixture.readAll(), QCryptographicHash::Sha256),
        QByteArray::fromHex(sha256));

    auto audioSink =
        std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](
                const FfmpegMediaDecodeRequest &request,
                const FfmpegVideoFrameSink &videoSink,
                const FfmpegAudioOutputSink &audioOutput,
                const FfmpegMediaStreamSink &streamSink,
                std::stop_token stopToken) {
            return decodeMediaFrames(
                request,
                videoSink,
                audioOutput,
                streamSink,
                stopToken);
        },
        audioSink);
    auto presentationTime = std::chrono::steady_clock::now();
    const auto service = [&] {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        ControlledAudioRender rendered =
            audioSink->render(480);
        if (rendered.frames != 0)
            audioSink->advancePresentedFrames(rendered.frames);
        presentationTime += std::chrono::milliseconds(10);
        session.videoSource().prepareForPresentation(
            presentationTime);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        return rendered;
    };

    session.openMedia(QUrl::fromLocalFile(path));
    const auto frameFailure = [&] {
        return QStringLiteral(
            "position=%1 decoded=%2 queued=%3 selected=%4 audioPresented=%5 state=%6 error=%7")
            .arg(session.positionMilliseconds())
            .arg(session.decodedFrameCount())
            .arg(session.queuedFrameCount())
            .arg(session.selectedFrameCount())
            .arg(audioSink->presentedFrames())
            .arg(static_cast<int>(session.state()))
            .arg(session.errorMessage());
    };
    QElapsedTimer timer;
    timer.start();
    while (session.state() != MediaSession::State::Ready
            && timer.elapsed() < 5'000) {
        service();
        QThread::yieldCurrentThread();
    }
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(session.positionMilliseconds() <= 10);
    timer.restart();

    if (videoStartsLate) {
        QVERIFY(!session.hasFrame());
        while (audioSink->presentedFrames() < 43'200
                && timer.elapsed() < 3'000) {
            service();
            QVERIFY(!session.hasFrame());
        }
        QVERIFY(audioSink->presentedFrames() >= 43'200);
        QTest::qWait(120);
        QVERIFY(!session.hasFrame());
        while (!session.hasFrame()
                && timer.elapsed() < 5'000) {
            service();
        }
        QVERIFY2(
            session.hasFrame(),
            qPrintable(frameFailure()));
        QVERIFY(session.positionMilliseconds() >= 1'000);
    } else {
        while (!session.hasFrame()
                && timer.elapsed() < 5'000) {
            service();
        }
        QVERIFY2(
            session.hasFrame(),
            qPrintable(frameFailure()));
        while (audioSink->presentedFrames() < 204'000
                && timer.elapsed() < 5'000) {
            const ControlledAudioRender rendered = service();
            if (audioSink->presentedFrames() <= 192'000) {
                QVERIFY(std::all_of(
                    rendered.samples.cbegin(),
                    rendered.samples.cend(),
                    [](float sample) { return sample == 0.0F; }));
            }
        }
        QVERIFY(audioSink->presentedFrames() >= 204'000);
        QVERIFY(session.positionMilliseconds() >= 4'250);
        while (session.decodedFrameCount() <= 128U
                && timer.elapsed() < 5'000) {
            service();
            QThread::yieldCurrentThread();
        }
        QVERIFY(session.decodedFrameCount() > 128U);
    }
    QVERIFY(session.state() != MediaSession::State::Error);
    session.cancel();
}

void MediaSessionTest::
playbackProgressDoesNotRequirePresentationConsumer() {
    auto audioSink =
        std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](
                const FfmpegMediaDecodeRequest &request,
                const FfmpegVideoFrameSink &videoSink,
                const FfmpegAudioOutputSink &audioOutput,
                const FfmpegMediaStreamSink &streamSink,
                std::stop_token stopToken) {
            return decodeMediaFrames(
                request,
                videoSink,
                audioOutput,
                streamSink,
                stopToken);
        },
        audioSink);
    QSignalSpy timelineChanges(
        &session, &MediaSession::timelineChanged);

    session.openMedia(
        QUrl::fromLocalFile(shortAudioFixturePath()));
    QElapsedTimer timer;
    timer.start();
    while (!session.ended() && timer.elapsed() < 6'000) {
        const ControlledAudioRender rendered =
            audioSink->render(480);
        if (rendered.frames != 0)
            audioSink->advancePresentedFrames(rendered.frames);
        QTest::qWait(10);
    }

    QVERIFY(session.ended());
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QCOMPARE(session.positionMilliseconds(), 3'000);
    QCOMPARE(
        session.selectedFrameCount()
            + session.droppedFrameCount(),
        12U);
    QVERIFY(session.maximumQueuedFrameCount()
        <= VideoFrameQueue::capacity);
    QVERIFY(timelineChanges.count() >= 10);
    QVERIFY(session.errorMessage().isEmpty());
}

void MediaSessionTest::trailingVideoContinuesAfterAudioDrains() {
    QFile fixture(shortAudioFixturePath());
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QCOMPARE(
        QCryptographicHash::hash(
            fixture.readAll(), QCryptographicHash::Sha256),
        QByteArray::fromHex(
            "a053d298cf6f53e06b8022579f0f0efb"
            "40d12e8b2c4b8bc09a609fa678148156"));

    auto audioSink =
        std::make_shared<ControlledAudioSink>(4'096);
    std::atomic_int decodeOperations = 0;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&decodeOperations](
                const FfmpegMediaDecodeRequest &request,
                const FfmpegVideoFrameSink &videoSink,
                const FfmpegAudioOutputSink &audioOutput,
                const FfmpegMediaStreamSink &streamSink,
                std::stop_token stopToken) {
            ++decodeOperations;
            return decodeMediaFrames(
                request,
                videoSink,
                audioOutput,
                streamSink,
                stopToken);
        },
        audioSink);

    auto presentationTime = std::chrono::steady_clock::now();
    const auto servicePlayback = [&] {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        const ControlledAudioRender rendered =
            audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
            if (audioSink->snapshot().drained)
                audioSink->setPositionAvailable(false);
        }
        presentationTime += std::chrono::milliseconds(50);
        session.videoSource().prepareForPresentation(
            presentationTime);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
    };

    session.openMedia(
        QUrl::fromLocalFile(shortAudioFixturePath()));
    QElapsedTimer timer;
    timer.start();
    while (!session.ended() && timer.elapsed() < 10'000) {
        servicePlayback();
        QThread::yieldCurrentThread();
    }

    QVERIFY(session.ended());
    QCOMPARE(decodeOperations.load(), 1);
    QCOMPARE(session.durationMilliseconds(), 3'000);
    QCOMPARE(session.positionMilliseconds(), 3'000);
    QCOMPARE(
        session.selectedFrameCount()
            + session.droppedFrameCount(),
        12U);
    QCOMPARE(
        session.videoSource().currentFrame()
            ->timing().ptsMicroseconds(),
        std::optional<std::int64_t>(7'750'000));
    QVERIFY(audioSink->snapshot().drained);

    session.seekToMilliseconds(2'000);
    timer.restart();
    while ((session.state() != MediaSession::State::Ready
                || !session.hasFrame())
            && timer.elapsed() < 5'000) {
        servicePlayback();
        QThread::yieldCurrentThread();
    }
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(session.hasFrame());
    QCOMPARE(decodeOperations.load(), 2);
    QCOMPARE(session.positionMilliseconds(), 2'000);
    QVERIFY(!session.playing());

    presentationTime = std::chrono::steady_clock::now();
    session.play();
    timer.restart();
    while (!session.ended() && timer.elapsed() < 5'000) {
        servicePlayback();
        QThread::yieldCurrentThread();
    }
    QVERIFY(session.ended());
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QCOMPARE(session.positionMilliseconds(), 3'000);
    QVERIFY(session.errorMessage().isEmpty());
}

void MediaSessionTest::longPostAudioSeekDoesNotStall() {
    QFile fixture(longVideoTailFixturePath());
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QCOMPARE(
        QCryptographicHash::hash(
            fixture.readAll(), QCryptographicHash::Sha256),
        QByteArray::fromHex(
            "f70f9e51e83aa49c41774343e7616ed5"
            "255ffe95b1761c45ac5e557a95857eba"));

    auto audioSink =
        std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](
                const FfmpegMediaDecodeRequest &request,
                const FfmpegVideoFrameSink &videoSink,
                const FfmpegAudioOutputSink &audioOutput,
                const FfmpegMediaStreamSink &streamSink,
                std::stop_token stopToken) {
            return decodeMediaFrames(
                request,
                videoSink,
                audioOutput,
                streamSink,
                stopToken);
        },
        audioSink);

    auto presentationTime = std::chrono::steady_clock::now();
    const auto service = [&] {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        const ControlledAudioRender rendered =
            audioSink->render(480);
        if (rendered.frames != 0)
            audioSink->advancePresentedFrames(rendered.frames);
        presentationTime += std::chrono::milliseconds(25);
        session.videoSource().prepareForPresentation(
            presentationTime);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
    };
    const auto serviceUntil = [&](auto predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs) {
            service();
            QThread::yieldCurrentThread();
        }
        return predicate();
    };

    session.openMedia(
        QUrl::fromLocalFile(longVideoTailFixturePath()));
    QVERIFY(serviceUntil(
        [&] {
            return session.state() == MediaSession::State::Ready
                && session.hasFrame();
        },
        5'000));
    session.pause();
    session.seekToMilliseconds(4'000);
    QVERIFY(serviceUntil(
        [&] {
            return session.state() == MediaSession::State::Ready
                && session.hasFrame();
        },
        5'000));
    QCOMPARE(session.positionMilliseconds(), 4'000);
    QVERIFY(!session.playing());
    QVERIFY(!audioSink->snapshot().valid);

    QTest::qWait(1'200);
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(session.errorMessage().isEmpty());

    presentationTime = std::chrono::steady_clock::now();
    session.play();
    QVERIFY(serviceUntil([&] { return session.ended(); }, 10'000));
    QVERIFY(session.durationMilliseconds() >= 7'990);
    QCOMPARE(
        session.positionMilliseconds(),
        session.durationMilliseconds());
    QVERIFY(session.decodedFrameCount() > 128U);
    QVERIFY(session.errorMessage().isEmpty());
}

void MediaSessionTest::audioOutputFailureBecomesSessionError() {
    auto audioSink =
        std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](
                const FfmpegMediaDecodeRequest &request,
                const FfmpegVideoFrameSink &videoSink,
                const FfmpegAudioOutputSink &audioOutput,
                const FfmpegMediaStreamSink &streamSink,
                std::stop_token stopToken) {
            return decodeMediaFrames(
                request,
                videoSink,
                audioOutput,
                streamSink,
                stopToken);
        },
        audioSink);

    auto presentationTime = std::chrono::steady_clock::now();
    QElapsedTimer timer;
    timer.start();
    session.openMedia(
        QUrl::fromLocalFile(synchronizedFixturePath()));
    while ((!audioSink->snapshot().producerFinished
                || session.state() != MediaSession::State::Ready)
            && timer.elapsed() < 10'000) {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        const ControlledAudioRender rendered =
            audioSink->render(257);
        if (rendered.frames != 0)
            audioSink->advancePresentedFrames(rendered.frames);
        presentationTime += std::chrono::milliseconds(10);
        session.videoSource().prepareForPresentation(
            presentationTime);
        QThread::yieldCurrentThread();
    }
    QVERIFY(audioSink->snapshot().producerFinished);
    QCOMPARE(session.state(), MediaSession::State::Ready);

    constexpr auto reason = "Injected post-decode device failure";
    audioSink->fail(session.playbackGeneration(), reason);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Error, 1'000);
    QCOMPARE(session.errorMessage(), QString::fromLatin1(reason));
    QVERIFY(!session.hasFrame());
    QVERIFY(!session.playing());
}

void MediaSessionTest::unavailableAudioClockBecomesSessionError() {
    auto audioSink =
        std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](
                const FfmpegMediaDecodeRequest &request,
                const FfmpegVideoFrameSink &videoSink,
                const FfmpegAudioOutputSink &audioOutput,
                const FfmpegMediaStreamSink &streamSink,
                std::stop_token stopToken) {
            return decodeMediaFrames(
                request,
                videoSink,
                audioOutput,
                streamSink,
                stopToken);
        },
        audioSink);

    session.openMedia(
        QUrl::fromLocalFile(synchronizedFixturePath()));
    QElapsedTimer timer;
    timer.start();
    while ((session.state() != MediaSession::State::Ready
                || !audioSink->snapshot().valid)
            && timer.elapsed() < 3'000) {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        const ControlledAudioRender rendered =
            audioSink->render(257);
        if (rendered.frames != 0)
            audioSink->advancePresentedFrames(rendered.frames);
        session.videoSource().prepareForPresentation(
            std::chrono::steady_clock::now());
        QThread::yieldCurrentThread();
    }
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(audioSink->snapshot().valid);

    audioSink->setPositionAvailable(false);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Error, 2'000);
    QCOMPARE(
        session.errorMessage(),
        QStringLiteral(
            "The audio presentation clock became unavailable."));
    QVERIFY(!session.hasFrame());
}

void MediaSessionTest::
readyNotificationCanCancelWithoutRepublishing() {
    MediaSession session(VideoTargetReadback::Disabled);
    bool cancelledFromReadyNotification = false;
    connect(
        &session,
        &MediaSession::sessionChanged,
        &session,
        [&] {
            if (!cancelledFromReadyNotification
                    && session.state()
                        == MediaSession::State::Ready) {
                cancelledFromReadyNotification = true;
                session.cancel();
            }
        });

    session.openMedia(
        QUrl::fromLocalFile(fixturePath()));
    QTRY_VERIFY_WITH_TIMEOUT(
        cancelledFromReadyNotification, 5000);
    QCOMPARE(session.state(), MediaSession::State::Empty);
    QVERIFY(!session.hasFrame());
    QCOMPARE(session.queuedFrameCount(), 0U);
}

void MediaSessionTest::
openingNotificationKeepsNewestRequest() {
    auto gate = std::make_shared<OperationGate>();
    MediaSession session(
        VideoTargetReadback::Disabled,
        [gate](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (!gate->wait(stopToken))
                return cancelledResult();
            return decodeVideoFrames(
                request,
                sink,
                stopToken);
        });
    const QUrl first =
        QUrl::fromLocalFile(fixturePath());
    const QUrl replacement =
        QUrl::fromLocalFile(
            replacementFixturePath());
    bool replacementRequested = false;
    connect(
        &session,
        &MediaSession::playbackMetricsChanged,
        &session,
        [&] {
            if (!replacementRequested
                    && session.state()
                        == MediaSession::State::Opening
                    && session.mediaUrl() == first) {
                replacementRequested = true;
                session.openMedia(replacement);
            }
        });

    session.openMedia(first);
    QVERIFY(replacementRequested);
    QCOMPARE(session.mediaUrl(), replacement);
    gate->release();

    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(session.mediaUrl(), replacement);
    QCOMPARE(
        session.displayName(),
        QStringLiteral("sdr-rgb-first-frame.ppm"));
    QVERIFY(session.videoSummary().startsWith(
        QStringLiteral("4×4")));
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->identity()
            .playbackGeneration,
        session.playbackGeneration());
}

void MediaSessionTest::
continuousPlaybackIsBoundedAndPauseable() {
    QFile fixture(playbackFixturePath());
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QCOMPARE(
        QCryptographicHash::hash(
            fixture.readAll(),
            QCryptographicHash::Sha256)
            .toHex(),
        QByteArray(
            "771e53aa2f15725d334bb7fcaecdb41cf"
            "69707ba2f21918e830a9abfd2dfe19d"));

    auto fifthFrameGate =
        std::make_shared<OperationGate>();
    auto decoderOutputCount =
        std::make_shared<std::atomic_uint64_t>(0);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [fifthFrameGate, decoderOutputCount](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            return decodeVideoFrames(
                request,
                [fifthFrameGate,
                 decoderOutputCount,
                 &sink,
                 stopToken](
                        std::shared_ptr<
                            const DecodedVideoFrame> frame,
                        const FfmpegVideoStreamDiagnostics
                            &diagnostics) {
                    const std::uint64_t output =
                        ++*decoderOutputCount;
                    if (output == 5
                            && !fifthFrameGate->wait(
                                stopToken)) {
                        return false;
                    }
                    return sink(
                        std::move(frame), diagnostics);
                },
                stopToken);
        });
    std::uint64_t notifiedDecodedFrames = 0;
    QSignalSpy metricsSpy(
        &session,
        &MediaSession::playbackMetricsChanged);
    QSignalSpy updateSpy(
        &session.videoSource(),
        &RenderedVideoSource::updateRequested);
    connect(
        &session,
        &MediaSession::playbackMetricsChanged,
        &session,
        [&] {
            notifiedDecodedFrames =
                session.decodedFrameCount();
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.playing());
    QVERIFY(session.hasFrame());
    const std::uint64_t firstFrameId =
        session.videoSource()
            .currentFrame()
            ->identity()
            .frameId;

    session.pause();
    QVERIFY(!session.playing());
    session.videoSource().prepareForPresentation(
        std::chrono::steady_clock::now()
        + std::chrono::seconds(5));
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->identity()
            .frameId,
        firstFrameId);

    QTRY_COMPARE_WITH_TIMEOUT(
        decoderOutputCount->load(), 5U, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        notifiedDecodedFrames, 4U, 5000);
    QCOMPARE(session.decodedFrameCount(), 4U);
    QCOMPARE(session.selectedFrameCount(), 1U);
    QCOMPARE(
        session.queuedFrameCount(),
        VideoFrameQueue::capacity);
    QCoreApplication::sendPostedEvents(
        &session, QEvent::MetaCall);
    const qsizetype metricsBeforeFifth =
        metricsSpy.count();
    const qsizetype updatesBeforeFifth =
        updateSpy.count();

    fifthFrameGate->release();
    QTRY_VERIFY_WITH_TIMEOUT(
        metricsSpy.count() > metricsBeforeFifth,
        5000);
    QCOMPARE(notifiedDecodedFrames, 5U);
    QCOMPARE(session.decodedFrameCount(), 5U);
    QCOMPARE(session.selectedFrameCount(), 1U);
    QCOMPARE(
        session.queuedFrameCount(),
        VideoFrameQueue::capacity);
    QCOMPARE(updateSpy.count(), updatesBeforeFifth);
    QCOMPARE(
        session.maximumQueuedFrameCount(),
        VideoFrameQueue::capacity);

    session.play();
    QVERIFY(session.playing());
    const auto playbackAnchor =
        std::chrono::steady_clock::now();
    for (std::uint64_t frameOffset = 1;
            frameOffset < 12;
            ++frameOffset) {
        QTRY_VERIFY_WITH_TIMEOUT(
            ([&] {
                session.videoSource()
                    .prepareForPresentation(
                        playbackAnchor
                        + std::chrono::milliseconds(
                            frameOffset * 250 + 20));
                const auto &frame =
                    session.videoSource()
                        .currentFrame();
                return frame
                    && frame->identity().frameId
                        == firstFrameId + frameOffset;
            }()),
            5000);
    }

    QTRY_VERIFY_WITH_TIMEOUT(
        ([&] {
            session.videoSource().prepareForPresentation(
                playbackAnchor
                + std::chrono::milliseconds(3020));
            return session.ended();
        }()),
        5000);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->identity()
            .frameId,
        firstFrameId + 11);
    QVERIFY(!session.playing());
    QCOMPARE(session.decodedFrameCount(), 12U);
    QCOMPARE(session.selectedFrameCount(), 12U);
    QCOMPARE(session.droppedFrameCount(), 0U);
    QCOMPARE(
        session.maximumQueuedFrameCount(),
        VideoFrameQueue::capacity);

    const std::uint64_t completedGeneration =
        session.playbackGeneration();
    session.play();
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.playing());
    QVERIFY(
        session.playbackGeneration()
        != completedGeneration);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->identity()
            .frameId,
        1U);
}

void MediaSessionTest::dropsSupersededDueFrames() {
    MediaSession session(VideoTargetReadback::Disabled);
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    const std::uint64_t firstFrameId =
        session.videoSource()
            .currentFrame()
            ->identity()
            .frameId;

    session.pause();
    QTRY_COMPARE_WITH_TIMEOUT(
        session.decodedFrameCount(), 5U, 5000);
    QCOMPARE(
        session.queuedFrameCount(),
        VideoFrameQueue::capacity);

    session.play();
    const auto playbackAnchor =
        std::chrono::steady_clock::now();
    session.videoSource().prepareForPresentation(
        playbackAnchor
        + std::chrono::milliseconds(770));

    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->identity()
            .frameId,
        firstFrameId + 3);
    QCOMPARE(session.selectedFrameCount(), 2U);
    QCOMPARE(session.droppedFrameCount(), 2U);
}

void MediaSessionTest::
seekPreservesTimelineAndPlayIntent() {
    std::atomic_bool sawExplicitZero = false;
    std::atomic_bool zeroPerformedDemuxSeek = false;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&sawExplicitZero,
         &zeroPerformedDemuxSeek](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.start.targetPositionMicroseconds
                    && *request.start
                        .targetPositionMicroseconds == 0) {
                sawExplicitZero = true;
                zeroPerformedDemuxSeek =
                    request.start.performDemuxSeek;
            }
            return decodeVideoFrames(
                request, sink, stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.seekable());
    QCOMPARE(session.durationMilliseconds(), 3'000);

    session.pause();
    session.seekToMilliseconds(-500);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(session.positionMilliseconds(), 0);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->timing()
            .ptsMicroseconds(),
        std::optional<std::int64_t>(0));
    QVERIFY(sawExplicitZero.load());
    QVERIFY(zeroPerformedDemuxSeek.load());

    session.seekToMilliseconds(1'250);
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(session.seeking());
    QCOMPARE(session.positionMilliseconds(), 1'250);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seeking());
    QVERIFY(!session.playing());
    QCOMPARE(session.positionMilliseconds(), 1'250);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->timing()
            .ptsMicroseconds(),
        std::optional<std::int64_t>(1'250'000));

    session.play();
    session.seekToMilliseconds(2'000);
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(session.seeking());
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.playing());
    QVERIFY(session.positionMilliseconds() >= 2'000);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->timing()
            .ptsMicroseconds(),
        std::optional<std::int64_t>(2'000'000));

    session.seekToMilliseconds(3'000);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.ended());
    QVERIFY(!session.playing());
    QCOMPARE(session.positionMilliseconds(), 3'000);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->timing()
            .ptsMicroseconds(),
        std::optional<std::int64_t>(2'750'000));
}

void MediaSessionTest::
interFrameSeekPublishesRequestedFrame() {
    MediaSession session(VideoTargetReadback::Disabled);
    session.openMedia(
        QUrl::fromLocalFile(interFrameSeekFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    session.pause();

    session.seekToMilliseconds(3'250);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seeking());
    QCOMPARE(session.positionMilliseconds(), 3'250);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->timing()
            .ptsMicroseconds(),
        std::optional<std::int64_t>(3'250'000));
    QVERIFY(session.decodedFrameCount() > 1);
}

void MediaSessionTest::
futureSeekFrameAdvancesClockAnchor() {
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.start
                    .targetPositionMicroseconds
                    != 600'000) {
                return decodeVideoFrames(
                    request, sink, stopToken);
            }
            return decodeVideoFrames(
                request,
                [&sink](
                        std::shared_ptr<
                            const DecodedVideoFrame> frame,
                        const FfmpegVideoStreamDiagnostics
                            &diagnostics) {
                    const auto pts =
                        frame->timing().ptsMicroseconds();
                    if (pts && *pts < 750'000)
                        return true;
                    return sink(
                        std::move(frame), diagnostics);
                },
                stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    session.pause();

    session.seekToMilliseconds(600);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(session.positionMilliseconds(), 600);
    QVERIFY(!session.hasFrame());

    session.play();
    QElapsedTimer timer;
    timer.start();
    while (!session.hasFrame() && timer.elapsed() < 1'000) {
        session.videoSource().prepareForPresentation(
            std::chrono::steady_clock::now());
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
        QThread::yieldCurrentThread();
    }
    QVERIFY(session.hasFrame());
    QVERIFY(session.positionMilliseconds() >= 750);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->timing()
            .ptsMicroseconds(),
        std::optional<std::int64_t>(750'000));
}

void MediaSessionTest::
newerSeekRejectsStaleCompletion() {
    auto delayed =
        std::make_shared<DelayedStopOperation>();
    MediaSession session(
        VideoTargetReadback::Disabled,
        [delayed](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.start
                    .targetPositionMicroseconds
                    == 500'000) {
                return delayed->wait(stopToken);
            }
            return decodeVideoFrames(
                request, sink, stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    session.pause();

    session.seekToMilliseconds(500);
    QTRY_VERIFY_WITH_TIMEOUT(
        delayed->started.load(), 2000);
    const std::uint64_t olderGeneration =
        session.playbackGeneration();
    session.seekToMilliseconds(2'000);
    QVERIFY(
        session.playbackGeneration()
        != olderGeneration);
    QCOMPARE(session.positionMilliseconds(), 2'000);
    QVERIFY(session.seeking());

    delayed->allowExit();
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seeking());
    QCOMPARE(session.positionMilliseconds(), 2'000);
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->timing()
            .ptsMicroseconds(),
        std::optional<std::int64_t>(2'000'000));
}

void MediaSessionTest::
cancelDuringSeekClearsSession() {
    auto delayed =
        std::make_shared<DelayedStopOperation>();
    MediaSession session(
        VideoTargetReadback::Disabled,
        [delayed](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.start.targetPositionMicroseconds)
                return delayed->wait(stopToken);
            return decodeVideoFrames(
                request, sink, stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);

    session.seekToMilliseconds(1'000);
    QTRY_VERIFY_WITH_TIMEOUT(
        delayed->started.load(), 2000);
    session.cancel();
    QCOMPARE(session.state(), MediaSession::State::Empty);
    QVERIFY(!session.seeking());
    QVERIFY(!session.hasFrame());
    QVERIFY(session.mediaUrl().isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(
        delayed->stopObserved.load(), 2000);
    delayed->allowExit();
}

void MediaSessionTest::
seekFailureClearsSeekingState() {
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.start.targetPositionMicroseconds) {
                FfmpegVideoDecodeResult failed;
                failed.error =
                    QStringLiteral("Injected seek failure");
                return failed;
            }
            return decodeVideoFrames(
                request, sink, stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    session.pause();

    session.seekToMilliseconds(1'000);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Error, 5000);
    QVERIFY(!session.seeking());
    QVERIFY(!session.hasFrame());
    QCOMPARE(
        session.errorMessage(),
        QStringLiteral("Injected seek failure"));
}

void MediaSessionTest::
nonseekableReplayReadsNaturallyFromStart() {
    std::atomic_bool sawNaturalZeroRestart = false;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&sawNaturalZeroRestart](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.start.targetPositionMicroseconds
                    && *request.start
                        .targetPositionMicroseconds == 0) {
                sawNaturalZeroRestart =
                    !request.start.performDemuxSeek;
            }
            FfmpegVideoDecodeResult result =
                decodeVideoFrames(
                    request,
                    [&sink](
                            std::shared_ptr<
                                const DecodedVideoFrame> frame,
                            const FfmpegVideoStreamDiagnostics
                                &diagnostics) {
                        FfmpegVideoStreamDiagnostics
                            nonseekable = diagnostics;
                        nonseekable.seekable = false;
                        return sink(
                            std::move(frame),
                            nonseekable);
                    },
                    stopToken);
            result.diagnostics.seekable = false;
            return result;
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seekable());

    const auto playbackAnchor =
        std::chrono::steady_clock::now();
    QTRY_VERIFY_WITH_TIMEOUT(
        ([&] {
            session.videoSource().prepareForPresentation(
                playbackAnchor
                + std::chrono::seconds(4));
            return session.ended();
        }()),
        5000);
    session.play();
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(sawNaturalZeroRestart.load());
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->timing()
            .ptsMicroseconds(),
        std::optional<std::int64_t>(0));
}

void MediaSessionTest::rejectsNonLocalUrls() {
    MediaSession session(VideoTargetReadback::Disabled);
    session.openMedia(QUrl(QStringLiteral(
        "https://example.invalid/video.mkv")));

    QCOMPARE(session.state(), MediaSession::State::Error);
    QVERIFY(!session.hasFrame());
    QVERIFY(!session.errorMessage().isEmpty());
}

void MediaSessionTest::newerOpenRejectsStaleCompletion() {
    auto delayed =
        std::make_shared<DelayedStopOperation>();
    MediaSession session(
        VideoTargetReadback::Disabled,
        [delayed](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.path.endsWith(
                    QStringLiteral("blocked.mkv"))) {
                return delayed->wait(stopToken);
            }
            return decodeVideoFrames(
                request,
                sink,
                stopToken);
        });

    session.openMedia(QUrl::fromLocalFile(
        QStringLiteral("blocked.mkv")));
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);
    const std::uint64_t blockedGeneration =
        session.playbackGeneration();

    session.openMedia(
        QUrl::fromLocalFile(fixturePath()));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QTRY_VERIFY_WITH_TIMEOUT(
        delayed->stopObserved.load(), 2000);
    QVERIFY(!delayed->exited.load());
    QVERIFY(
        session.playbackGeneration()
        != blockedGeneration);
    delayed->allowExit();
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(session.displayName(),
             QStringLiteral("sdr-bt709-ffv1.mkv"));
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->identity()
            .playbackGeneration,
        session.playbackGeneration());
}

void MediaSessionTest::cancelReturnsBeforeWorkerExit() {
    auto delayed =
        std::make_shared<DelayedStopOperation>();
    MediaSession session(
        VideoTargetReadback::Disabled,
        [delayed](
                const FfmpegVideoDecodeRequest &,
                const FfmpegVideoFrameSink &,
                std::stop_token stopToken) {
            return delayed->wait(stopToken);
        });
    session.openMedia(QUrl::fromLocalFile(
        QStringLiteral("blocked.mkv")));
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);

    session.cancel();
    QCOMPARE(session.state(), MediaSession::State::Empty);
    QTRY_VERIFY_WITH_TIMEOUT(
        delayed->stopObserved.load(), 2000);
    QVERIFY(!delayed->exited.load());

    delayed->allowExit();
    QTRY_VERIFY_WITH_TIMEOUT(delayed->exited.load(), 2000);
    QCOMPARE(session.state(), MediaSession::State::Empty);
}

void MediaSessionTest::destructionCancelsWorker() {
    auto blocking =
        std::make_shared<BlockingOperation>();
    auto session = std::make_unique<MediaSession>(
        VideoTargetReadback::Disabled,
        [blocking](
                const FfmpegVideoDecodeRequest &,
                const FfmpegVideoFrameSink &,
                std::stop_token stopToken) {
            return blocking->wait(stopToken);
        });
    session->openMedia(QUrl::fromLocalFile(
        QStringLiteral("blocked.mkv")));
    QTRY_VERIFY_WITH_TIMEOUT(blocking->started.load(), 2000);

    session.reset();
    QVERIFY(blocking->stopped.load());
}

void MediaSessionTest::
presentationFailureBecomesSessionError() {
    MediaSession session(VideoTargetReadback::Disabled);
    session.openMedia(
        QUrl::fromLocalFile(fixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);

    QVERIFY(session.videoSource().reportPresentationFailure(
        {
            .kind = VideoFailureKind::General,
            .reason =
                QStringLiteral("unsupported mapped surface"),
        }));
    QCOMPARE(session.state(), MediaSession::State::Error);
    QVERIFY(!session.hasFrame());
    QCOMPARE(
        session.errorMessage(),
        QStringLiteral("unsupported mapped surface"));
}

void MediaSessionTest::
hardwareImportFailureRetriesSoftware() {
    std::atomic_int attempts = 0;
    std::atomic_int softwareFallbackAttempts = 0;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&attempts, &softwareFallbackAttempts](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            ++attempts;
            if (!request.hardwareDecode.isAvailable()
                    && request.hardwareDecode
                        .unavailableReason.contains(
                        QStringLiteral(
                            "Hardware frame import failed"))) {
                ++softwareFallbackAttempts;
            }
            return decodeVideoFrames(
                request,
                sink,
                stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(fixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    const std::uint64_t firstGeneration =
        session.playbackGeneration();

    QVERIFY(session.videoSource().reportPresentationFailure(
        {
            .kind = VideoFailureKind::
                HardwareFrameImportUnavailable,
            .reason = QStringLiteral(
                "Injected unsupported hardware surface"),
        }));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);

    QCOMPARE(attempts.load(), 2);
    QCOMPARE(softwareFallbackAttempts.load(), 1);
    QVERIFY(
        session.playbackGeneration() != firstGeneration);
    QCOMPARE(session.decodePath(), QStringLiteral("Software"));
    QVERIFY(
        session.hardwareFallbackReason().contains(
            QStringLiteral(
                "Hardware frame import failed")));

    const auto playbackAnchor =
        std::chrono::steady_clock::now();
    QTRY_VERIFY_WITH_TIMEOUT(
        ([&] {
            session.videoSource().prepareForPresentation(
                playbackAnchor
                + std::chrono::seconds(1));
            return session.ended();
        }()),
        5000);
    const std::uint64_t completedGeneration =
        session.playbackGeneration();

    session.play();
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(attempts.load(), 3);
    QVERIFY(
        session.playbackGeneration()
        != completedGeneration);

    QVERIFY(session.videoSource().reportPresentationFailure(
        {
            .kind = VideoFailureKind::
                HardwareFrameImportUnavailable,
            .reason = QStringLiteral(
                "Injected replay hardware import failure"),
        }));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(attempts.load(), 4);
    QCOMPARE(softwareFallbackAttempts.load(), 2);

    QVERIFY(session.videoSource().reportPresentationFailure(
        {
            .kind = VideoFailureKind::
                HardwareFrameImportUnavailable,
            .reason = QStringLiteral(
                "Injected repeated hardware import failure"),
        }));
    QCOMPARE(session.state(), MediaSession::State::Error);
    QCOMPARE(attempts.load(), 4);
}

void MediaSessionTest::
hardwareImportFallbackRestartsAtPosition() {
    std::atomic<std::int64_t> fallbackPosition = -1;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&fallbackPosition](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.hardwareDecode.unavailableReason
                    .contains(QStringLiteral(
                        "Hardware frame import failed"))) {
                fallbackPosition =
                    *request.start
                        .targetPositionMicroseconds;
            }
            return decodeVideoFrames(
                request, sink, stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    session.pause();
    session.seekToMilliseconds(1'250);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);

    QVERIFY(session.videoSource().reportPresentationFailure(
        {
            .kind = VideoFailureKind::
                HardwareFrameImportUnavailable,
            .reason = QStringLiteral(
                "Injected unsupported hardware surface"),
        }));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(fallbackPosition.load(), 1'250'000);
    QCOMPARE(session.positionMilliseconds(), 1'250);
    QVERIFY(!session.playing());
}

void MediaSessionTest::
graphicsRecoveryRestartsSoftwareAtPosition() {
    std::atomic_int attempts = 0;
    std::atomic_bool sawReplacementCapability = false;
    std::atomic<std::int64_t> replacementPosition = -1;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&attempts,
         &sawReplacementCapability,
         &replacementPosition](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            ++attempts;
            if (request.hardwareDecode.unavailableReason
                    == QStringLiteral(
                        "Replacement graphics domain has no hardware")) {
                sawReplacementCapability = true;
                if (request.start.targetPositionMicroseconds) {
                    replacementPosition =
                        *request.start
                            .targetPositionMicroseconds;
                }
            }
            return decodeVideoFrames(
                request,
                sink,
                stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    session.pause();
    session.seekToMilliseconds(1'250);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);

    session.invalidateGraphicsDevice();
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());
    session.setVideoDecodeCapability({
        .device = {},
        .unavailableReason = QStringLiteral(
            "Replacement graphics domain has no hardware"),
    });

    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(sawReplacementCapability.load());
    QCOMPARE(replacementPosition.load(), 1'250'000);
    QCOMPARE(session.positionMilliseconds(), 1'250);

    replacementPosition = -1;
    session.seekToMilliseconds(2'000);
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(replacementPosition.load(), 2'000'000);
    QCOMPARE(attempts.load(), 4);
}

void MediaSessionTest::
graphicsRecoveryPreservesPendingSeek() {
    auto delayed =
        std::make_shared<DelayedStopOperation>();
    std::atomic<std::int64_t> replacementPosition = -1;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [delayed, &replacementPosition](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (request.start
                    .targetPositionMicroseconds
                    == 500'000) {
                return delayed->wait(stopToken);
            }
            if (request.hardwareDecode.unavailableReason
                    == QStringLiteral(
                        "Replacement graphics domain")) {
                replacementPosition =
                    *request.start
                        .targetPositionMicroseconds;
            }
            return decodeVideoFrames(
                request, sink, stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    session.pause();
    session.seekToMilliseconds(500);
    QTRY_VERIFY_WITH_TIMEOUT(
        delayed->started.load(), 2000);

    session.invalidateGraphicsDevice();
    QVERIFY(session.seeking());
    QCOMPARE(session.positionMilliseconds(), 500);
    QTRY_VERIFY_WITH_TIMEOUT(
        delayed->stopObserved.load(), 2000);
    session.seekToMilliseconds(1'000);
    QVERIFY(session.seeking());
    QCOMPARE(session.positionMilliseconds(), 1'000);

    session.setVideoDecodeCapability({
        .device = {},
        .unavailableReason =
            QStringLiteral("Replacement graphics domain"),
    });
    delayed->allowExit();
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seeking());
    QCOMPARE(replacementPosition.load(), 1'000'000);
    QCOMPARE(session.positionMilliseconds(), 1'000);
    QVERIFY(!session.playing());
}

void MediaSessionTest::
graphicsRecoverySupersedesOpening() {
    auto delayed =
        std::make_shared<DelayedStopOperation>();
    std::atomic_int attempts = 0;
    std::atomic_bool sawReplacementCapability = false;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [delayed, &attempts, &sawReplacementCapability](
                const FfmpegVideoDecodeRequest &request,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            const int attempt = ++attempts;
            if (attempt == 1)
                return delayed->wait(stopToken);
            sawReplacementCapability =
                request.hardwareDecode.unavailableReason
                == QStringLiteral(
                    "Replacement graphics domain has no hardware");
            return decodeVideoFrames(
                request,
                sink,
                stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(fixturePath()));
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);
    const std::uint64_t firstGeneration =
        session.playbackGeneration();

    session.invalidateGraphicsDevice();
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());
    QVERIFY(
        session.playbackGeneration() != firstGeneration);
    QTRY_VERIFY_WITH_TIMEOUT(
        delayed->stopObserved.load(), 2000);
    session.setVideoDecodeCapability({
        .device = {},
        .unavailableReason = QStringLiteral(
            "Replacement graphics domain has no hardware"),
    });
    delayed->allowExit();
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);

    QCOMPARE(attempts.load(), 2);
    QVERIFY(sawReplacementCapability.load());
    QCOMPARE(
        session.videoSource()
            .currentFrame()
            ->identity()
            .playbackGeneration,
        session.playbackGeneration());
}

// Worker results are delivered through queued Qt events, so this test needs
// QCoreApplication without loading a GUI platform plugin.
QTEST_GUILESS_MAIN(MediaSessionTest)
#include "tst_MediaSession.moc"
