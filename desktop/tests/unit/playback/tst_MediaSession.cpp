#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

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

FfmpegFirstFrameResult cancelledResult() {
    FfmpegFirstFrameResult result;
    result.cancelled = true;
    return result;
}

struct BlockingOperation {
    std::atomic_bool started = false;
    std::atomic_bool stopped = false;
    std::mutex mutex;
    std::condition_variable_any wake;

    FfmpegFirstFrameResult wait(
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

    FfmpegFirstFrameResult wait(
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
    MediaSession session(
        VideoTargetReadback::Disabled,
        [ownerThread, &decodedOffOwnerThread](
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &,
                std::stop_token stopToken) {
            decodedOffOwnerThread =
                QThread::currentThread() != ownerThread;
            return decodeFirstVideoFrame(
                path, identity, stopToken);
        });
    QSignalSpy changes(
        &session, &MediaSession::sessionChanged);

    session.openMedia(
        QUrl::fromLocalFile(fixturePath()));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());

    QTRY_COMPARE_WITH_TIMEOUT(
        session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.hasFrame());
    QVERIFY(decodedOffOwnerThread.load());
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
                const VideoHardwareDecodeCapability &,
                std::stop_token stopToken) {
            if (path.endsWith(
                    QStringLiteral("blocked.mkv"))) {
                return delayed->wait(stopToken);
            }
            return decodeFirstVideoFrame(
                path, identity, stopToken);
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
    std::atomic_bool sawSoftwareFallback = false;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&attempts, &sawSoftwareFallback](
                const QString &path,
                const VideoFrameIdentity &identity,
                const VideoHardwareDecodeCapability &capability,
                std::stop_token stopToken) {
            const int attempt = ++attempts;
            if (attempt == 2) {
                sawSoftwareFallback =
                    !capability.isAvailable()
                    && capability.unavailableReason.contains(
                        QStringLiteral(
                            "Hardware frame import failed"));
            }
            return decodeFirstVideoFrame(
                path, identity, capability, stopToken);
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
    QVERIFY(sawSoftwareFallback.load());
    QVERIFY(
        session.playbackGeneration() != firstGeneration);
    QCOMPARE(session.decodePath(), QStringLiteral("Software"));
    QVERIFY(
        session.hardwareFallbackReason().contains(
            QStringLiteral(
                "Hardware frame import failed")));

    QVERIFY(session.videoSource().reportPresentationFailure(
        {
            .kind = VideoFailureKind::
                HardwareFrameImportUnavailable,
            .reason = QStringLiteral(
                "Injected repeated hardware import failure"),
        }));
    QCOMPARE(session.state(), MediaSession::State::Error);
    QCOMPARE(attempts.load(), 2);
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
                std::stop_token stopToken) {
            ++attempts;
            return decodeFirstVideoFrame(
                path, identity, capability, stopToken);
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
                std::stop_token stopToken) {
            const int attempt = ++attempts;
            if (attempt == 1)
                return delayed->wait(stopToken);
            sawReplacementCapability =
                capability.unavailableReason
                == QStringLiteral(
                    "Replacement graphics domain has no hardware");
            return decodeFirstVideoFrame(
                path, identity, capability, stopToken);
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
