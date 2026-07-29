#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "media/DecodedVideoFrame.h"
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
    void readyNotificationCanCancelWithoutRepublishing();
    void openingNotificationKeepsNewestRequest();
    void continuousPlaybackIsBoundedAndPauseable();
    void dropsSupersededDueFrames();
    void rejectsNonLocalUrls();
    void newerOpenRejectsStaleCompletion();
    void cancelReturnsBeforeWorkerExit();
    void destructionCancelsWorker();
    void presentationFailureBecomesSessionError();
    void hardwareImportFailureRetriesSoftware();
    void graphicsRecoveryKeepsReadySoftwareFrame();
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
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &capability,
                int extraHardwareFrames,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            decodedOffOwnerThread =
                QThread::currentThread() != ownerThread;
            hardwareFrameReserve =
                extraHardwareFrames;
            return decodeVideoFrames(
                path,
                identity,
                capability,
                extraHardwareFrames,
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
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &capability,
                int extraHardwareFrames,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (!gate->wait(stopToken))
                return cancelledResult();
            return decodeVideoFrames(
                path,
                identity,
                capability,
                extraHardwareFrames,
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
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &capability,
                int extraHardwareFrames,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            return decodeVideoFrames(
                path,
                identity,
                capability,
                extraHardwareFrames,
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
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &capability,
                int extraHardwareFrames,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            if (path.endsWith(
                    QStringLiteral("blocked.mkv"))) {
                return delayed->wait(stopToken);
            }
            return decodeVideoFrames(
                path,
                identity,
                capability,
                extraHardwareFrames,
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
                const QString &,
                const VideoFrameIdentity &,
                const VideoHardwareDecodeCapability &,
                int,
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
                const QString &,
                const VideoFrameIdentity &,
                const VideoHardwareDecodeCapability &,
                int,
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
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &capability,
                int extraHardwareFrames,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            ++attempts;
            if (!capability.isAvailable()
                    && capability.unavailableReason.contains(
                        QStringLiteral(
                            "Hardware frame import failed"))) {
                ++softwareFallbackAttempts;
            }
            return decodeVideoFrames(
                path,
                identity,
                capability,
                extraHardwareFrames,
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
graphicsRecoveryKeepsReadySoftwareFrame() {
    std::atomic_int attempts = 0;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&attempts](
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &capability,
                int extraHardwareFrames,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            ++attempts;
            return decodeVideoFrames(
                path,
                identity,
                capability,
                extraHardwareFrames,
                sink,
                stopToken);
        });
    session.openMedia(
        QUrl::fromLocalFile(fixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    const std::uint64_t firstGeneration =
        session.playbackGeneration();
    const std::shared_ptr<const DecodedVideoFrame> firstFrame =
        session.videoSource().currentFrame();
    QVERIFY(firstFrame);
    QVERIFY(!firstFrame->storage().isHardware());

    session.invalidateGraphicsDevice();
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(session.hasFrame());
    QCOMPARE(session.playbackGeneration(), firstGeneration);
    QCOMPARE(session.videoSource().currentFrame(), firstFrame);
    session.setVideoDecodeCapability({
        .device = {},
        .unavailableReason = QStringLiteral(
            "Replacement graphics domain has no hardware"),
    });

    QCOMPARE(attempts.load(), 1);
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QCOMPARE(
        session.videoSource().currentFrame(),
        firstFrame);
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
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &capability,
                int extraHardwareFrames,
                const FfmpegVideoFrameSink &sink,
                std::stop_token stopToken) {
            const int attempt = ++attempts;
            if (attempt == 1)
                return delayed->wait(stopToken);
            sawReplacementCapability =
                capability.unavailableReason
                == QStringLiteral(
                    "Replacement graphics domain has no hardware");
            return decodeVideoFrames(
                path,
                identity,
                capability,
                extraHardwareFrames,
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
