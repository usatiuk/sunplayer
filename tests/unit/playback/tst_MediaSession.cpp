#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <numeric>

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

extern "C" {
#include <libavutil/frame.h>
}

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "audio/ControlledAudioSink.h"
#include "media/DecodedVideoFrame.h"
#include "playback/MediaSession.h"

namespace {
QString fixturePath() { return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1.mkv"); }

QString playbackFixturePath() { return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-playback.mkv"); }

QString synchronizedFixturePath() {
    return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-flac-sync.mkv");
}

QString shortAudioFixturePath() {
    return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-short-flac.mkv");
}

QString audioLateFixturePath() {
    return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-audio-late-long-flac.mkv");
}

QString videoLateFixturePath() {
    return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-video-late-flac.mkv");
}

QString longVideoTailFixturePath() {
    return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-short-audio-long-video-flac.mkv");
}

QString subtitleFixturePath() { return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-subtitles.mkv"); }

QString multitrackFixturePath() {
    return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-ffv1-multitrack-flac.mkv");
}

QString interFrameSeekFixturePath() {
    return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-bt709-h264-seek.mkv");
}

QString replacementFixturePath() { return QStringLiteral(SUNPLAYER_TEST_FIXTURE_DIR "/media/sdr-rgb-first-frame.ppm"); }

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

    FfmpegVideoDecodeResult wait(std::stop_token stopToken) {
        started = true;
        std::unique_lock lock(mutex);
        wake.wait(lock, stopToken, [] { return false; });
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

    FfmpegVideoDecodeResult wait(std::stop_token stopToken) {
        started = true;
        std::unique_lock lock(mutex);
        wake.wait(lock, stopToken, [] { return false; });
        stopObserved = true;
        wake.wait(lock, [this] { return exitAllowed; });
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
        return wake.wait(lock, stopToken, [this] { return open; });
    }

    void release() {
        {
            std::lock_guard lock(mutex);
            open = true;
        }
        wake.notify_all();
    }
};
} // namespace

class MediaSessionTest final : public QObject {
    Q_OBJECT

  public:
    static void initMain() {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    }

  private slots:
    void opensRealMediaOffThread();
    void synchronizedPlaybackUsesPresentedAudioClock();
    void mutedPlaybackKeepsPresentedAudioAsMaster();
    void staggeredStarts_data();
    void staggeredStarts();
    void playbackProgressDoesNotRequirePresentationConsumer();
    void trailingVideoContinuesAfterAudioDrains();
    void longPostAudioSeekDoesNotStall();
    void embeddedMediaTrackSelectionUsesPlaybackGeneration();
    void embeddedSubtitleSelectionUsesPlaybackGeneration();
    void audioOutputFailureBecomesSessionError();
    void sustainedAudioUnderrunEntersBuffering();
    void unavailableAudioClockBecomesSessionError();
    void unanchoredAudioOutputEpochBecomesSessionError();
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
    QThread* const ownerThread = QThread::currentThread();
    std::atomic_bool decodedOffOwnerThread = false;
    std::atomic_int hardwareFrameReserve = 0;
    MediaSession session(VideoTargetReadback::Disabled,
                         [ownerThread, &decodedOffOwnerThread,
                          &hardwareFrameReserve](FfmpegVideoDecodeRequest const& request,
                                                 FfmpegVideoFrameSink const& sink, std::stop_token stopToken) {
                             decodedOffOwnerThread = QThread::currentThread() != ownerThread;
                             hardwareFrameReserve = request.extraHardwareFrames;
                             return decodeVideoFrames(request, sink, stopToken);
                         });
    QSignalSpy changes(&session, &MediaSession::sessionChanged);
    bool publishedReadyWithoutFrame = false;
    connect(&session, &MediaSession::sessionChanged, &session, [&] {
        if (session.state() == MediaSession::State::Ready && !session.hasFrame()) {
            publishedReadyWithoutFrame = true;
        }
    });

    session.openMedia(QUrl::fromLocalFile(fixturePath()));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());

    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.hasFrame());
    QVERIFY(!publishedReadyWithoutFrame);
    QVERIFY(decodedOffOwnerThread.load());
    QCOMPARE(hardwareFrameReserve.load(), static_cast<int>(VideoFrameQueue::capacity + 2));
    QVERIFY(changes.count() >= 2);
    QCOMPARE(session.decoderName(), QStringLiteral("ffv1"));
    QCOMPARE(session.decodePath(), QStringLiteral("Software"));
    QVERIFY(session.hardwareFallbackReason().isEmpty());
    QVERIFY(!session.containerFormat().isEmpty());
    QVERIFY(session.videoSummary().contains(QStringLiteral("yuv420p")));
    QVERIFY(session.videoSummary().contains(QStringLiteral("fps")));
    QCOMPARE(session.videoDynamicRange(), QStringLiteral("SDR"));
    QVERIFY(!session.videoHdr());
    QVERIFY(session.videoSignalSummary().contains(QStringLiteral("BT.709 primaries")));
    QCOMPARE(session.videoSource().currentFrame()->identity().playbackGeneration, session.playbackGeneration());
}

void MediaSessionTest::synchronizedPlaybackUsesPresentedAudioClock() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    std::atomic_int decodeOperations = 0;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&decodeOperations](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
                            FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
                            FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            ++decodeOperations;
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);

    auto const servicePlayback = [&] {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::hours(24));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        return rendered.frames;
    };
    auto const waitUntil = [&](auto predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs) {
            servicePlayback();
            QThread::yieldCurrentThread();
        }
        return predicate();
    };

    session.openMedia(QUrl::fromLocalFile(synchronizedFixturePath()));
    QVERIFY(waitUntil([&] { return session.state() == MediaSession::State::Ready && session.hasFrame(); }, 10'000));
    QCOMPARE(decodeOperations.load(), 1);
    QVERIFY(session.hasFrame());
    QVERIFY(session.playing());

    for (int iteration = 0; iteration != 20; ++iteration) {
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::hours(24));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    qlonglong const stablePosition = session.positionMilliseconds();
    std::uint64_t const stableFrame = session.videoSource().currentFrame()->identity().frameId;
    for (int iteration = 0; iteration != 20; ++iteration) {
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::hours(24));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    QCOMPARE(session.positionMilliseconds(), stablePosition);
    QCOMPARE(session.videoSource().currentFrame()->identity().frameId, stableFrame);

    std::uint64_t const advanceTarget = audioSink->presentedFrames() + 12'000;
    QVERIFY(waitUntil([&] { return audioSink->presentedFrames() >= advanceTarget; }, 10'000));
    qlonglong const expectedAudioPosition = static_cast<qlonglong>(audioSink->presentedFrames() * 1'000 / 48'000);
    QVERIFY(std::abs(session.positionMilliseconds() - expectedAudioPosition) <= 1);
    QVERIFY(session.positionMilliseconds() > stablePosition);

    ControlledAudioRender submittedNotPresented;
    QElapsedTimer submitTimer;
    submitTimer.start();
    while (submittedNotPresented.frames == 0 && submitTimer.elapsed() < 3'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        submittedNotPresented = audioSink->render(257);
        QThread::yieldCurrentThread();
    }
    QVERIFY(submittedNotPresented.frames != 0);
    AudioPresentationSnapshot const inFlight = audioSink->snapshot();
    QVERIFY(inFlight.submittedFrames > inFlight.presentedFrames);
    session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::hours(24));
    QCOMPARE(session.positionMilliseconds(), static_cast<qlonglong>(inFlight.presentedFrames * 1'000 / 48'000));
    audioSink->advancePresentedFrames(submittedNotPresented.frames);

    session.pause();
    qlonglong const pausedPosition = session.positionMilliseconds();
    QCOMPARE(servicePlayback(), 0U);
    QCOMPARE(session.positionMilliseconds(), pausedPosition);

    session.seekToMilliseconds(1'250);
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(waitUntil([&] { return session.state() == MediaSession::State::Ready && session.hasFrame(); }, 10'000));
    QCOMPARE(decodeOperations.load(), 2);
    QVERIFY(!session.playing());
    QCOMPARE(session.positionMilliseconds(), 1'250);
    QCOMPARE(session.videoSource().currentFrame()->identity().playbackGeneration, session.playbackGeneration());

    QTest::qWait(1'200);
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(!session.playing());
    QVERIFY(session.errorMessage().isEmpty());

    session.play();
    QVERIFY(waitUntil([&] { return session.ended(); }, 15'000));
    QCOMPARE(decodeOperations.load(), 2);
    QVERIFY(audioSink->snapshot().drained);
    QCOMPARE(session.positionMilliseconds(), 3'000);
    QVERIFY(!session.playing());
}

void MediaSessionTest::embeddedSubtitleSelectionUsesPlaybackGeneration() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    std::mutex requestsMutex;
    std::vector<int> requestedSubtitleStreams;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&requestsMutex,
         &requestedSubtitleStreams](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
                                    FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
                                    FfmpegSubtitleOutputSink const& subtitleSink, std::stop_token stopToken) {
            {
                std::lock_guard lock(requestsMutex);
                requestedSubtitleStreams.push_back(request.selectedSubtitleStreamIndex);
            }
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, subtitleSink, stopToken);
        },
        audioSink);

    auto const servicePlayback = [&] {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::hours(24));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    auto const waitUntil = [&](auto predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs) {
            servicePlayback();
            QThread::yieldCurrentThread();
        }
        return predicate();
    };

    session.openMedia(QUrl::fromLocalFile(subtitleFixturePath()));
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready && session.hasFrame() &&
                   session.subtitleTracks()->rowCount() == 3;
        },
        10'000));
    QCOMPARE(session.selectedSubtitleStreamIndex(), -1);
    QCOMPARE(session.selectedSubtitleTrackSummary(), QStringLiteral("Off"));
    session.pause();

    std::uint64_t const initialGeneration = session.playbackGeneration();
    session.selectSubtitleStream(2);
    QVERIFY(session.playbackGeneration() > initialGeneration);
    QVERIFY(waitUntil(
        [&] {
            SubtitlePresentationSnapshot const snapshot =
                session.subtitlePresentationSnapshot(std::chrono::steady_clock::now());
            return session.state() == MediaSession::State::Ready && session.selectedSubtitleStreamIndex() == 2 &&
                   snapshot.state.isEnabled() && snapshot.state.configuration &&
                   snapshot.state.configuration->streamIndex == 2 && snapshot.state.events &&
                   !snapshot.state.events->empty();
        },
        10'000));
    QVERIFY(session.selectedSubtitleTrackSummary().contains(QStringLiteral("English - Styled Ahem")));
    QVERIFY(session.selectedSubtitleTrackSummary().contains(QStringLiteral("ass · text")));
    QVERIFY(!session.playing());

    std::uint64_t const selectedGeneration = session.playbackGeneration();
    session.seekToMilliseconds(4'000);
    QVERIFY(waitUntil(
        [&] { return session.state() == MediaSession::State::Ready && session.selectedSubtitleStreamIndex() == 2; },
        10'000));
    QVERIFY(session.playbackGeneration() > selectedGeneration);
    QCOMPARE(session.subtitlePresentationSnapshot(std::chrono::steady_clock::now()).state.playbackGeneration,
             session.playbackGeneration());

    session.selectSubtitleStream(-1);
    QCOMPARE(session.selectedSubtitleStreamIndex(), -1);
    QCOMPARE(session.selectedSubtitleTrackSummary(), QStringLiteral("Off"));
    QVERIFY(!session.subtitlePresentationSnapshot(std::chrono::steady_clock::now()).state.isEnabled());
    QVERIFY(waitUntil(
        [&] {
            std::lock_guard lock(requestsMutex);
            return !requestedSubtitleStreams.empty() && requestedSubtitleStreams.back() == -1 &&
                   requestedSubtitleStreams.size() >= 3;
        },
        10'000));
    {
        std::lock_guard lock(requestsMutex);
        QVERIFY(requestedSubtitleStreams.size() >= 3);
        QCOMPARE(requestedSubtitleStreams.front(), -1);
        QCOMPARE(requestedSubtitleStreams.back(), -1);
        QVERIFY(std::find(requestedSubtitleStreams.begin(), requestedSubtitleStreams.end(), 2) !=
                requestedSubtitleStreams.end());
    }
}

void MediaSessionTest::embeddedMediaTrackSelectionUsesPlaybackGeneration() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const& subtitleSink, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, subtitleSink, stopToken);
        },
        audioSink);

    auto const prepareVideo = [&] {
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::hours(24));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    auto const waitUntil = [&](auto predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            prepareVideo();
            QThread::yieldCurrentThread();
        }
        return predicate();
    };
    auto const renderFreshAudio = [&] {
        ControlledAudioRender rendered;
        QElapsedTimer timer;
        timer.start();
        while (rendered.frames == 0 && timer.elapsed() < 5'000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            rendered = audioSink->render(257);
            prepareVideo();
            QThread::yieldCurrentThread();
        }
        return rendered;
    };
    auto const meanSample = [](ControlledAudioRender const& rendered) {
        return std::accumulate(rendered.samples.begin(), rendered.samples.end(), 0.0) /
               static_cast<double>(rendered.samples.size());
    };
    auto const currentLuma = [&] {
        AVFrame const& frame = session.videoSource().currentFrame()->ffmpegFrame();
        return static_cast<int>(frame.data[0][frame.height / 2 * frame.linesize[0] + frame.width / 2]);
    };

    session.openMedia(QUrl::fromLocalFile(multitrackFixturePath()));
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready && session.hasFrame() &&
                   session.videoTracks()->rowCount() == 2 && session.audioTracks()->rowCount() == 2;
        },
        10'000));
    QCOMPARE(session.selectedVideoStreamIndex(), 2);
    QCOMPARE(session.selectedAudioStreamIndex(), 3);
    QVERIFY(session.selectedVideoTrackSummary().contains(QStringLiteral("Czech - Light")));
    QVERIFY(session.selectedAudioTrackSummary().contains(QStringLiteral("Czech - Negative")));
    QVERIFY(session.selectedAudioTrackSummary().contains(QStringLiteral("48 kHz")));
    QCOMPARE(currentLuma(), 235);
    ControlledAudioRender rendered = renderFreshAudio();
    QVERIFY(rendered.frames != 0);
    QVERIFY(meanSample(rendered) < -0.10);
    audioSink->advancePresentedFrames(rendered.frames);

    QElapsedTimer playbackTimer;
    playbackTimer.start();
    while (audioSink->presentedFrames() < 9'600 && playbackTimer.elapsed() < 10'000) {
        rendered = audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        prepareVideo();
    }
    QVERIFY(audioSink->presentedFrames() >= 9'600);
    qlonglong const playingPosition = session.positionMilliseconds();
    std::uint64_t const initialGeneration = session.playbackGeneration();
    session.selectVideoStream(0);
    QVERIFY(session.playbackGeneration() > initialGeneration);
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready && session.hasFrame() &&
                   session.selectedVideoStreamIndex() == 0;
        },
        10'000));
    QVERIFY(session.playing());
    QCOMPARE(currentLuma(), 16);
    rendered = renderFreshAudio();
    QVERIFY(rendered.frames != 0);
    QVERIFY(meanSample(rendered) < -0.10);
    QVERIFY(std::abs(session.positionMilliseconds() - playingPosition) <= 1);

    session.pause();
    qlonglong const pausedPosition = session.positionMilliseconds();
    std::uint64_t const videoGeneration = session.playbackGeneration();
    session.selectAudioStream(1);
    QVERIFY(session.playbackGeneration() > videoGeneration);
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready && session.hasFrame() &&
                   session.selectedAudioStreamIndex() == 1;
        },
        10'000));
    QVERIFY(!session.playing());
    QVERIFY(std::abs(session.positionMilliseconds() - pausedPosition) <= 1);
    QCOMPARE(session.selectedVideoStreamIndex(), 0);
    QCOMPARE(currentLuma(), 16);
    session.play();
    rendered = renderFreshAudio();
    QVERIFY(rendered.frames != 0);
    QVERIFY(meanSample(rendered) > 0.10);

    std::uint64_t const selectedGeneration = session.playbackGeneration();
    session.selectVideoStream(0);
    session.selectAudioStream(99);
    QCOMPARE(session.playbackGeneration(), selectedGeneration);

    session.seekToMilliseconds(1'000);
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready && session.hasFrame() &&
                   session.selectedVideoStreamIndex() == 0 && session.selectedAudioStreamIndex() == 1;
        },
        10'000));
    QCOMPARE(currentLuma(), 16);
    rendered = renderFreshAudio();
    QVERIFY(rendered.frames != 0);
    QVERIFY(meanSample(rendered) > 0.10);

    session.pause();
    session.selectVideoStream(2);
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready && session.hasFrame() &&
                   session.selectedVideoStreamIndex() == 2;
        },
        10'000));
    session.seekToMilliseconds(1'500);
    QVERIFY(waitUntil([&] { return session.state() == MediaSession::State::Ready && session.hasFrame(); }, 10'000));
    session.selectVideoStream(0);
    QVERIFY(waitUntil(
        [&] {
            return session.state() == MediaSession::State::Ready && session.hasFrame() &&
                   session.selectedVideoStreamIndex() == 0;
        },
        10'000));
    QVERIFY(!session.playing());
    QCOMPARE(session.positionMilliseconds(), 1'249);
    QCOMPARE(currentLuma(), 16);
}

void MediaSessionTest::mutedPlaybackKeepsPresentedAudioAsMaster() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);
    session.setVolume(0.35);
    session.setMuted(true);
    QCOMPARE(session.volume(), 0.35);
    QVERIFY(session.muted());

    session.openMedia(QUrl::fromLocalFile(synchronizedFixturePath()));
    std::uint64_t mutedFramesPresented = 0;
    QElapsedTimer timer;
    timer.start();
    while (mutedFramesPresented < 30'000 && timer.elapsed() < 10'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(480);
        if (rendered.frames != 0) {
            QVERIFY(std::all_of(rendered.samples.cbegin(), rendered.samples.cend(),
                                [](float sample) { return sample == 0.0F; }));
            audioSink->advancePresentedFrames(rendered.frames);
            mutedFramesPresented += rendered.frames;
        }
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::hours(24));
        QThread::yieldCurrentThread();
    }

    QVERIFY(mutedFramesPresented >= 30'000);
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(session.playing());
    QVERIFY(session.positionMilliseconds() >= 625);
    QTRY_VERIFY_WITH_TIMEOUT(session.hasAudioOutput(), 1'000);
    QCOMPARE(session.audioBackend(), QStringLiteral("controlled"));
    QCOMPARE(session.mediaClockSource(), MediaSession::MediaClockSource::PresentedAudio);
    QVERIFY(session.audioClockReliable());
    QVERIFY(session.audioPresentedFrames() >= 24'000);
    QCOMPARE(session.audioUnderrunFrames(), 0U);
    QVERIFY(session.audioQueuedMilliseconds() >= 0);

    session.setMuted(false);
    QVERIFY(!session.muted());
    QCOMPARE(session.volume(), 0.35);
    bool heardNonzeroSample = false;
    timer.restart();
    while (audioSink->presentedFrames() < 76'000 && timer.elapsed() < 5'000) {
        ControlledAudioRender const rendered = audioSink->render(480);
        if (rendered.frames != 0) {
            heardNonzeroSample =
                heardNonzeroSample || std::any_of(rendered.samples.cbegin(), rendered.samples.cend(),
                                                  [](float sample) { return std::abs(sample) > 0.0001F; });
            audioSink->advancePresentedFrames(rendered.frames);
        }
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::hours(24));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    QVERIFY(audioSink->presentedFrames() >= 76'000);
    QVERIFY(heardNonzeroSample);
}

void MediaSessionTest::staggeredStarts_data() {
    QTest::addColumn<QString>("path");
    QTest::addColumn<QByteArray>("sha256");
    QTest::addColumn<bool>("videoStartsLate");

    QTest::newRow("audio-starts-four-seconds-late-at-60fps") << audioLateFixturePath()
                                                             << QByteArray("4d1a1969a9e5b24a9a5e7eacd788d537"
                                                                           "ef5db9a882dcb5a303a73d0c5cc4f6d4")
                                                             << false;
    QTest::newRow("video-starts-one-second-late") << videoLateFixturePath()
                                                  << QByteArray("3182e5e996c147ef817a7b9b43ff63e3"
                                                                "e3b1f110111372ef6ff8ad34bcf5685d")
                                                  << true;
}

void MediaSessionTest::staggeredStarts() {
    QFETCH(QString, path);
    QFETCH(QByteArray, sha256);
    QFETCH(bool, videoStartsLate);

    QFile fixture(path);
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256), QByteArray::fromHex(sha256));

    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);
    auto presentationTime = std::chrono::steady_clock::now();
    auto const service = [&] {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender rendered = audioSink->render(480);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        presentationTime += std::chrono::milliseconds(10);
        session.videoSource().prepareForPresentation(presentationTime);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        return rendered;
    };

    session.openMedia(QUrl::fromLocalFile(path));
    auto const frameFailure = [&] {
        return QStringLiteral("position=%1 decoded=%2 queued=%3 selected=%4 audioPresented=%5 state=%6 error=%7")
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
    while (session.state() != MediaSession::State::Ready && timer.elapsed() < 5'000) {
        service();
        QThread::yieldCurrentThread();
    }
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(session.positionMilliseconds() <= 10);
    timer.restart();

    if (videoStartsLate) {
        // FIXME: This pre-PTS checkpoint failed twice in a 100-run stress
        // pass. Keep the state-rich assertions until the audio-clock boundary
        // can be diagnosed from a future recurrence.
        QVERIFY2(!session.hasFrame(), qPrintable(frameFailure()));
        while (audioSink->presentedFrames() < 43'200 && timer.elapsed() < 3'000) {
            service();
            QVERIFY2(!session.hasFrame(), qPrintable(frameFailure()));
        }
        QVERIFY(audioSink->presentedFrames() >= 43'200);
        QTest::qWait(120);
        QVERIFY2(!session.hasFrame(), qPrintable(frameFailure()));
        while (!session.hasFrame() && timer.elapsed() < 5'000) {
            service();
        }
        QVERIFY2(session.hasFrame(), qPrintable(frameFailure()));
        QVERIFY(session.positionMilliseconds() >= 1'000);
    } else {
        while (!session.hasFrame() && timer.elapsed() < 5'000) {
            service();
        }
        QVERIFY2(session.hasFrame(), qPrintable(frameFailure()));
        while (audioSink->presentedFrames() < 204'000 && timer.elapsed() < 5'000) {
            ControlledAudioRender const rendered = service();
            if (audioSink->presentedFrames() <= 192'000) {
                QVERIFY(std::all_of(rendered.samples.cbegin(), rendered.samples.cend(),
                                    [](float sample) { return sample == 0.0F; }));
            }
        }
        QVERIFY(audioSink->presentedFrames() >= 204'000);
        QVERIFY(session.positionMilliseconds() >= 4'250);
        while (session.decodedFrameCount() <= 128U && timer.elapsed() < 5'000) {
            service();
            QThread::yieldCurrentThread();
        }
        QVERIFY(session.decodedFrameCount() > 128U);
    }
    QVERIFY(session.state() != MediaSession::State::Error);
    session.cancel();
}

void MediaSessionTest::playbackProgressDoesNotRequirePresentationConsumer() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);
    QSignalSpy timelineChanges(&session, &MediaSession::timelineChanged);

    session.openMedia(QUrl::fromLocalFile(shortAudioFixturePath()));
    QElapsedTimer timer;
    timer.start();
    while (!session.ended() && timer.elapsed() < 6'000) {
        ControlledAudioRender const rendered = audioSink->render(480);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        QTest::qWait(10);
    }

    QVERIFY(session.ended());
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QCOMPARE(session.positionMilliseconds(), 3'000);
    QCOMPARE(session.selectedFrameCount() + session.droppedFrameCount(), 12U);
    QVERIFY(session.maximumQueuedFrameCount() <= VideoFrameQueue::capacity);
    QVERIFY(timelineChanges.count() >= 10);
    QVERIFY(session.errorMessage().isEmpty());
}

void MediaSessionTest::trailingVideoContinuesAfterAudioDrains() {
    QFile fixture(shortAudioFixturePath());
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256),
             QByteArray::fromHex("a053d298cf6f53e06b8022579f0f0efb"
                                 "40d12e8b2c4b8bc09a609fa678148156"));

    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    std::atomic_int decodeOperations = 0;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&decodeOperations](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
                            FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
                            FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            ++decodeOperations;
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);

    auto presentationTime = std::chrono::steady_clock::now();
    auto const servicePlayback = [&] {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
            if (audioSink->snapshot().drained) {
                audioSink->setPositionAvailable(false);
            }
        }
        presentationTime += std::chrono::milliseconds(50);
        session.videoSource().prepareForPresentation(presentationTime);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };

    session.openMedia(QUrl::fromLocalFile(shortAudioFixturePath()));
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
    QCOMPARE(session.selectedFrameCount() + session.droppedFrameCount(), 12U);
    QCOMPARE(session.videoSource().currentFrame()->timing().ptsMicroseconds(), std::optional<std::int64_t>(7'750'000));
    QVERIFY(audioSink->snapshot().drained);

    session.seekToMilliseconds(2'000);
    timer.restart();
    while ((session.state() != MediaSession::State::Ready || !session.hasFrame()) && timer.elapsed() < 5'000) {
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
    QCOMPARE(QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256),
             QByteArray::fromHex("f70f9e51e83aa49c41774343e7616ed5"
                                 "255ffe95b1761c45ac5e557a95857eba"));

    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);

    auto presentationTime = std::chrono::steady_clock::now();
    auto const service = [&] {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(480);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        presentationTime += std::chrono::milliseconds(25);
        session.videoSource().prepareForPresentation(presentationTime);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    auto const serviceUntil = [&](auto predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMs) {
            service();
            QThread::yieldCurrentThread();
        }
        return predicate();
    };

    session.openMedia(QUrl::fromLocalFile(longVideoTailFixturePath()));
    QVERIFY(serviceUntil([&] { return session.state() == MediaSession::State::Ready && session.hasFrame(); }, 5'000));
    session.pause();
    session.seekToMilliseconds(4'000);
    QVERIFY(serviceUntil([&] { return session.state() == MediaSession::State::Ready && session.hasFrame(); }, 5'000));
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
    QCOMPARE(session.positionMilliseconds(), session.durationMilliseconds());
    QVERIFY(session.decodedFrameCount() > 128U);
    QVERIFY(session.errorMessage().isEmpty());
}

void MediaSessionTest::audioOutputFailureBecomesSessionError() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);

    auto presentationTime = std::chrono::steady_clock::now();
    QElapsedTimer timer;
    timer.start();
    session.openMedia(QUrl::fromLocalFile(synchronizedFixturePath()));
    while ((!audioSink->snapshot().producerFinished || session.state() != MediaSession::State::Ready) &&
           timer.elapsed() < 10'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        presentationTime += std::chrono::milliseconds(10);
        session.videoSource().prepareForPresentation(presentationTime);
        QThread::yieldCurrentThread();
    }
    QVERIFY(audioSink->snapshot().producerFinished);
    QCOMPARE(session.state(), MediaSession::State::Ready);

    constexpr auto reason = "Injected post-decode device failure";
    audioSink->fail(session.playbackGeneration(), reason);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Error, 1'000);
    QCOMPARE(session.errorMessage(), QString::fromLatin1(reason));
    QVERIFY(!session.hasFrame());
    QVERIFY(!session.playing());
}

void MediaSessionTest::sustainedAudioUnderrunEntersBuffering() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);

    session.openMedia(QUrl::fromLocalFile(synchronizedFixturePath()));
    QElapsedTimer timer;
    timer.start();
    while ((session.state() != MediaSession::State::Ready || !audioSink->snapshot().valid) && timer.elapsed() < 3'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now());
        QThread::yieldCurrentThread();
    }
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(audioSink->snapshot().valid);
    QCOMPARE(session.playbackInterruption(), MediaSession::PlaybackInterruption::None);
    QTRY_VERIFY_WITH_TIMEOUT(audioSink->bufferedFrames() != 0, 1'000);
    QTRY_VERIFY_WITH_TIMEOUT(([&session] {
                                 session.videoSource().prepareForPresentation(std::chrono::steady_clock::now());
                                 return session.videoSource().currentFrame() != nullptr;
                             }()),
                             1'000);

    auto const holdStarted = std::chrono::steady_clock::now();
    ControlledAudioAdvance const stalledOutput = audioSink->advanceOutput(4'096 + 24'000);
    QVERIFY(stalledOutput.mediaFrames != 0);
    QVERIFY(stalledOutput.holdFrames >= 24'000);
    session.videoSource().prepareForPresentation(holdStarted);
    qlonglong const positionBefore = session.positionMilliseconds();
    std::shared_ptr<DecodedVideoFrame const> const frameBeforeHold = session.videoSource().currentFrame();
    QVERIFY(frameBeforeHold);
    QCOMPARE(session.playbackInterruption(), MediaSession::PlaybackInterruption::None);

    session.videoSource().prepareForPresentation(holdStarted + std::chrono::milliseconds(600));
    QCOMPARE(session.playbackInterruption(), MediaSession::PlaybackInterruption::Buffering);
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(session.playRequested());
    QVERIFY(!session.playing());
    QVERIFY(!session.videoSource().wantsContinuousFrames());
    QCOMPARE(session.positionMilliseconds(), positionBefore);
    QVERIFY(session.videoSource().currentFrame());
    QCOMPARE(session.videoSource().currentFrame()->identity(), frameBeforeHold->identity());
    QCOMPARE(session.mediaClockSource(), MediaSession::MediaClockSource::FrozenAudio);

    session.pause();
    QVERIFY(!session.playRequested());
    QVERIFY(!session.playing());
    QCOMPARE(session.playbackInterruption(), MediaSession::PlaybackInterruption::Buffering);
    QVERIFY(!session.videoSource().wantsContinuousFrames());

    session.play();
    QCOMPARE(session.playbackInterruption(), MediaSession::PlaybackInterruption::Buffering);
    QTRY_VERIFY_WITH_TIMEOUT(audioSink->bufferedFrames() != 0, 1'000);
    ControlledAudioRender const resumed = audioSink->render(257);
    QVERIFY(resumed.frames != 0);
    audioSink->advancePresentedFrames(resumed.frames);
    session.videoSource().prepareForPresentation(holdStarted + std::chrono::milliseconds(700));
    QVERIFY(session.playRequested());
    QCOMPARE(session.playbackInterruption(), MediaSession::PlaybackInterruption::None);
    QCOMPARE(session.mediaClockSource(), MediaSession::MediaClockSource::PresentedAudio);
    QVERIFY(session.videoSource().wantsContinuousFrames());
    QVERIFY(session.positionMilliseconds() > positionBefore);
}

void MediaSessionTest::unavailableAudioClockBecomesSessionError() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);

    session.openMedia(QUrl::fromLocalFile(synchronizedFixturePath()));
    QElapsedTimer timer;
    timer.start();
    while ((session.state() != MediaSession::State::Ready || !audioSink->snapshot().valid) && timer.elapsed() < 3'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now());
        QThread::yieldCurrentThread();
    }
    QCOMPARE(session.state(), MediaSession::State::Ready);
    QVERIFY(audioSink->snapshot().valid);

    QTRY_VERIFY_WITH_TIMEOUT(audioSink->bufferedFrames() != 0, 1'000);
    audioSink->setPositionAvailable(false);
    ControlledAudioAdvance const stillConsumed = audioSink->advanceOutput(257);
    QVERIFY(stillConsumed.mediaFrames != 0);
    auto const unavailableSince = std::chrono::steady_clock::now();
    session.videoSource().prepareForPresentation(unavailableSince);
    session.videoSource().prepareForPresentation(unavailableSince + std::chrono::milliseconds(1'100));
    QCOMPARE(session.state(), MediaSession::State::Error);
    QCOMPARE(session.errorMessage(), QStringLiteral("The audio presentation clock became unavailable."));
    QVERIFY(!session.playRequested());
    QVERIFY(!session.playing());
    QVERIFY(!session.hasFrame());
}

void MediaSessionTest::unanchoredAudioOutputEpochBecomesSessionError() {
    auto audioSink = std::make_shared<ControlledAudioSink>(4'096);
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
           FfmpegAudioOutputSink const& audioOutput, FfmpegMediaStreamSink const& streamSink,
           FfmpegSubtitleOutputSink const&, std::stop_token stopToken) {
            return decodeMediaFrames(request, videoSink, audioOutput, streamSink, stopToken);
        },
        audioSink);

    session.openMedia(QUrl::fromLocalFile(synchronizedFixturePath()));
    QElapsedTimer timer;
    timer.start();
    while ((session.state() != MediaSession::State::Ready || !audioSink->snapshot().valid) && timer.elapsed() < 3'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ControlledAudioRender const rendered = audioSink->render(257);
        if (rendered.frames != 0) {
            audioSink->advancePresentedFrames(rendered.frames);
        }
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now());
        QThread::yieldCurrentThread();
    }
    QCOMPARE(session.state(), MediaSession::State::Ready);
    AudioPresentationSnapshot const established = audioSink->snapshot();
    QVERIFY(established.valid);

    audioSink->reset(established.playbackGeneration, {48'000, 2});
    QVERIFY(audioSink->snapshot().audioOutputEpoch > established.audioOutputEpoch);
    session.videoSource().prepareForPresentation(std::chrono::steady_clock::now());

    QCOMPARE(session.state(), MediaSession::State::Error);
    QCOMPARE(session.errorMessage(), QStringLiteral("The audio output clock changed without being re-anchored."));
}

void MediaSessionTest::readyNotificationCanCancelWithoutRepublishing() {
    MediaSession session(VideoTargetReadback::Disabled);
    bool cancelledFromReadyNotification = false;
    connect(&session, &MediaSession::sessionChanged, &session, [&] {
        if (!cancelledFromReadyNotification && session.state() == MediaSession::State::Ready) {
            cancelledFromReadyNotification = true;
            session.cancel();
        }
    });

    session.openMedia(QUrl::fromLocalFile(fixturePath()));
    QTRY_VERIFY_WITH_TIMEOUT(cancelledFromReadyNotification, 5000);
    QCOMPARE(session.state(), MediaSession::State::Empty);
    QVERIFY(!session.hasFrame());
    QCOMPARE(session.queuedFrameCount(), 0U);
}

void MediaSessionTest::openingNotificationKeepsNewestRequest() {
    auto gate = std::make_shared<OperationGate>();
    MediaSession session(
        VideoTargetReadback::Disabled,
        [gate](FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink, std::stop_token stopToken) {
            if (!gate->wait(stopToken)) {
                return cancelledResult();
            }
            return decodeVideoFrames(request, sink, stopToken);
        });
    QUrl const first = QUrl::fromLocalFile(fixturePath());
    QUrl const replacement = QUrl::fromLocalFile(replacementFixturePath());
    bool replacementRequested = false;
    connect(&session, &MediaSession::playbackMetricsChanged, &session, [&] {
        if (!replacementRequested && session.state() == MediaSession::State::Opening && session.mediaUrl() == first) {
            replacementRequested = true;
            session.openMedia(replacement);
        }
    });

    session.openMedia(first);
    QVERIFY(replacementRequested);
    QCOMPARE(session.mediaUrl(), replacement);
    gate->release();

    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(session.mediaUrl(), replacement);
    QCOMPARE(session.displayName(), QStringLiteral("sdr-rgb-first-frame.ppm"));
    QVERIFY(session.videoSummary().startsWith(QStringLiteral("4×4")));
    QCOMPARE(session.videoSource().currentFrame()->identity().playbackGeneration, session.playbackGeneration());
}

void MediaSessionTest::continuousPlaybackIsBoundedAndPauseable() {
    QFile fixture(playbackFixturePath());
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256).toHex(),
             QByteArray("771e53aa2f15725d334bb7fcaecdb41cf"
                        "69707ba2f21918e830a9abfd2dfe19d"));

    auto fifthFrameGate = std::make_shared<OperationGate>();
    auto decoderOutputCount = std::make_shared<std::atomic_uint64_t>(0);
    MediaSession session(VideoTargetReadback::Disabled, [fifthFrameGate,
                                                         decoderOutputCount](FfmpegVideoDecodeRequest const& request,
                                                                             FfmpegVideoFrameSink const& sink,
                                                                             std::stop_token stopToken) {
        return decodeVideoFrames(
            request,
            [fifthFrameGate, decoderOutputCount, &sink, stopToken](std::shared_ptr<DecodedVideoFrame const> frame,
                                                                   FfmpegVideoStreamDiagnostics const& diagnostics) {
                std::uint64_t const output = ++*decoderOutputCount;
                if (output == 5 && !fifthFrameGate->wait(stopToken)) {
                    return false;
                }
                return sink(std::move(frame), diagnostics);
            },
            stopToken);
    });
    std::uint64_t notifiedDecodedFrames = 0;
    QSignalSpy metricsSpy(&session, &MediaSession::playbackMetricsChanged);
    QSignalSpy updateSpy(&session.videoSource(), &RenderedVideoSource::updateRequested);
    connect(&session, &MediaSession::playbackMetricsChanged, &session,
            [&] { notifiedDecodedFrames = session.decodedFrameCount(); });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.playing());
    QVERIFY(session.hasFrame());
    std::uint64_t const firstFrameId = session.videoSource().currentFrame()->identity().frameId;

    session.pause();
    QVERIFY(!session.playing());
    session.videoSource().prepareForPresentation(std::chrono::steady_clock::now() + std::chrono::seconds(5));
    QCOMPARE(session.videoSource().currentFrame()->identity().frameId, firstFrameId);

    QTRY_COMPARE_WITH_TIMEOUT(decoderOutputCount->load(), 5U, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(notifiedDecodedFrames, 4U, 5000);
    QCOMPARE(session.decodedFrameCount(), 4U);
    QCOMPARE(session.selectedFrameCount(), 1U);
    QCOMPARE(session.queuedFrameCount(), VideoFrameQueue::capacity);
    QCoreApplication::sendPostedEvents(&session, QEvent::MetaCall);
    qsizetype const metricsBeforeFifth = metricsSpy.count();
    qsizetype const updatesBeforeFifth = updateSpy.count();

    fifthFrameGate->release();
    QTRY_VERIFY_WITH_TIMEOUT(metricsSpy.count() > metricsBeforeFifth, 5000);
    QCOMPARE(notifiedDecodedFrames, 5U);
    QCOMPARE(session.decodedFrameCount(), 5U);
    QCOMPARE(session.selectedFrameCount(), 1U);
    QCOMPARE(session.queuedFrameCount(), VideoFrameQueue::capacity);
    QCOMPARE(updateSpy.count(), updatesBeforeFifth);
    QCOMPARE(session.maximumQueuedFrameCount(), VideoFrameQueue::capacity);

    session.play();
    QVERIFY(session.playing());
    auto const playbackAnchor = std::chrono::steady_clock::now();
    for (std::uint64_t frameOffset = 1; frameOffset < 12; ++frameOffset) {
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     session.videoSource().prepareForPresentation(
                                         playbackAnchor + std::chrono::milliseconds(frameOffset * 250 + 20));
                                     auto const& frame = session.videoSource().currentFrame();
                                     return frame && frame->identity().frameId == firstFrameId + frameOffset;
                                 }()),
                                 5000);
    }

    QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                 session.videoSource().prepareForPresentation(playbackAnchor +
                                                                              std::chrono::milliseconds(3020));
                                 return session.ended();
                             }()),
                             5000);
    QCOMPARE(session.videoSource().currentFrame()->identity().frameId, firstFrameId + 11);
    QVERIFY(!session.playing());
    QCOMPARE(session.decodedFrameCount(), 12U);
    QCOMPARE(session.selectedFrameCount(), 12U);
    QCOMPARE(session.droppedFrameCount(), 0U);
    QCOMPARE(session.maximumQueuedFrameCount(), VideoFrameQueue::capacity);

    std::uint64_t const completedGeneration = session.playbackGeneration();
    session.play();
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.playing());
    QVERIFY(session.playbackGeneration() != completedGeneration);
    QCOMPARE(session.videoSource().currentFrame()->identity().frameId, 1U);
}

void MediaSessionTest::dropsSupersededDueFrames() {
    MediaSession session(VideoTargetReadback::Disabled);
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    std::uint64_t const firstFrameId = session.videoSource().currentFrame()->identity().frameId;

    session.pause();
    QTRY_COMPARE_WITH_TIMEOUT(session.decodedFrameCount(), 5U, 5000);
    QCOMPARE(session.queuedFrameCount(), VideoFrameQueue::capacity);

    session.play();
    auto const playbackAnchor = std::chrono::steady_clock::now();
    session.videoSource().prepareForPresentation(playbackAnchor + std::chrono::milliseconds(770));

    QCOMPARE(session.videoSource().currentFrame()->identity().frameId, firstFrameId + 3);
    QCOMPARE(session.selectedFrameCount(), 2U);
    QCOMPARE(session.droppedFrameCount(), 2U);
}

void MediaSessionTest::seekPreservesTimelineAndPlayIntent() {
    std::atomic_bool sawExplicitZero = false;
    std::atomic_bool zeroPerformedDemuxSeek = false;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&sawExplicitZero, &zeroPerformedDemuxSeek](FfmpegVideoDecodeRequest const& request,
                                                    FfmpegVideoFrameSink const& sink, std::stop_token stopToken) {
            if (request.start.targetPositionMicroseconds && *request.start.targetPositionMicroseconds == 0) {
                sawExplicitZero = true;
                zeroPerformedDemuxSeek = request.start.performDemuxSeek;
            }
            return decodeVideoFrames(request, sink, stopToken);
        });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.seekable());
    QCOMPARE(session.durationMilliseconds(), 3'000);

    session.pause();
    session.seekToMilliseconds(-500);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(session.positionMilliseconds(), 0);
    QCOMPARE(session.videoSource().currentFrame()->timing().ptsMicroseconds(), std::optional<std::int64_t>(0));
    QVERIFY(sawExplicitZero.load());
    QVERIFY(zeroPerformedDemuxSeek.load());

    session.seekToMilliseconds(1'250);
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(session.seeking());
    QCOMPARE(session.positionMilliseconds(), 1'250);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seeking());
    QVERIFY(!session.playing());
    QCOMPARE(session.positionMilliseconds(), 1'250);
    QCOMPARE(session.videoSource().currentFrame()->timing().ptsMicroseconds(), std::optional<std::int64_t>(1'250'000));

    session.play();
    session.seekToMilliseconds(2'000);
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(session.seeking());
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.playing());
    QVERIFY(session.positionMilliseconds() >= 2'000);
    QCOMPARE(session.videoSource().currentFrame()->timing().ptsMicroseconds(), std::optional<std::int64_t>(2'000'000));

    session.seekToMilliseconds(3'000);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(session.ended());
    QVERIFY(!session.playing());
    QCOMPARE(session.positionMilliseconds(), 3'000);
    QCOMPARE(session.videoSource().currentFrame()->timing().ptsMicroseconds(), std::optional<std::int64_t>(2'750'000));
}

void MediaSessionTest::interFrameSeekPublishesRequestedFrame() {
    MediaSession session(VideoTargetReadback::Disabled);
    session.openMedia(QUrl::fromLocalFile(interFrameSeekFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    session.pause();

    session.seekToMilliseconds(3'250);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seeking());
    QCOMPARE(session.positionMilliseconds(), 3'250);
    QTRY_VERIFY_WITH_TIMEOUT(session.videoSource().currentFrame() != nullptr, 1'000);
    std::shared_ptr<DecodedVideoFrame const> const currentFrame = session.videoSource().currentFrame();
    QVERIFY(currentFrame);
    QCOMPARE(currentFrame->timing().ptsMicroseconds(), std::optional<std::int64_t>(3'250'000));
    QVERIFY(session.decodedFrameCount() > 1);
}

void MediaSessionTest::futureSeekFrameAdvancesClockAnchor() {
    MediaSession session(VideoTargetReadback::Disabled, [](FfmpegVideoDecodeRequest const& request,
                                                           FfmpegVideoFrameSink const& sink,
                                                           std::stop_token stopToken) {
        if (request.start.targetPositionMicroseconds != 600'000) {
            return decodeVideoFrames(request, sink, stopToken);
        }
        return decodeVideoFrames(
            request,
            [&sink](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const& diagnostics) {
                auto const pts = frame->timing().ptsMicroseconds();
                if (pts && *pts < 750'000) {
                    return true;
                }
                return sink(std::move(frame), diagnostics);
            },
            stopToken);
    });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    session.pause();

    session.seekToMilliseconds(600);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(session.positionMilliseconds(), 600);
    QVERIFY(!session.hasFrame());

    session.play();
    QElapsedTimer timer;
    timer.start();
    while (!session.hasFrame() && timer.elapsed() < 1'000) {
        session.videoSource().prepareForPresentation(std::chrono::steady_clock::now());
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::yieldCurrentThread();
    }
    QVERIFY(session.hasFrame());
    QVERIFY(session.positionMilliseconds() >= 750);
    QCOMPARE(session.videoSource().currentFrame()->timing().ptsMicroseconds(), std::optional<std::int64_t>(750'000));
}

void MediaSessionTest::newerSeekRejectsStaleCompletion() {
    auto delayed = std::make_shared<DelayedStopOperation>();
    MediaSession session(VideoTargetReadback::Disabled,
                         [delayed](FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink,
                                   std::stop_token stopToken) {
                             if (request.start.targetPositionMicroseconds == 500'000) {
                                 return delayed->wait(stopToken);
                             }
                             return decodeVideoFrames(request, sink, stopToken);
                         });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    session.pause();

    session.seekToMilliseconds(500);
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);
    std::uint64_t const olderGeneration = session.playbackGeneration();
    session.seekToMilliseconds(2'000);
    QVERIFY(session.playbackGeneration() != olderGeneration);
    QCOMPARE(session.positionMilliseconds(), 2'000);
    QVERIFY(session.seeking());

    delayed->allowExit();
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seeking());
    QCOMPARE(session.positionMilliseconds(), 2'000);
    QCOMPARE(session.videoSource().currentFrame()->timing().ptsMicroseconds(), std::optional<std::int64_t>(2'000'000));
}

void MediaSessionTest::cancelDuringSeekClearsSession() {
    auto delayed = std::make_shared<DelayedStopOperation>();
    MediaSession session(VideoTargetReadback::Disabled,
                         [delayed](FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink,
                                   std::stop_token stopToken) {
                             if (request.start.targetPositionMicroseconds) {
                                 return delayed->wait(stopToken);
                             }
                             return decodeVideoFrames(request, sink, stopToken);
                         });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);

    session.seekToMilliseconds(1'000);
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);
    session.cancel();
    QCOMPARE(session.state(), MediaSession::State::Empty);
    QVERIFY(!session.seeking());
    QVERIFY(!session.hasFrame());
    QVERIFY(session.mediaUrl().isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(delayed->stopObserved.load(), 2000);
    delayed->allowExit();
}

void MediaSessionTest::seekFailureClearsSeekingState() {
    MediaSession session(
        VideoTargetReadback::Disabled,
        [](FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink, std::stop_token stopToken) {
            if (request.start.targetPositionMicroseconds) {
                FfmpegVideoDecodeResult failed;
                failed.error = QStringLiteral("Injected seek failure");
                return failed;
            }
            return decodeVideoFrames(request, sink, stopToken);
        });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    session.pause();

    session.seekToMilliseconds(1'000);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Error, 5000);
    QVERIFY(!session.seeking());
    QVERIFY(!session.hasFrame());
    QCOMPARE(session.errorMessage(), QStringLiteral("Injected seek failure"));
}

void MediaSessionTest::nonseekableReplayReadsNaturallyFromStart() {
    std::atomic_bool sawNaturalZeroRestart = false;
    MediaSession session(VideoTargetReadback::Disabled, [&sawNaturalZeroRestart](
                                                            FfmpegVideoDecodeRequest const& request,
                                                            FfmpegVideoFrameSink const& sink,
                                                            std::stop_token stopToken) {
        if (request.start.targetPositionMicroseconds && *request.start.targetPositionMicroseconds == 0) {
            sawNaturalZeroRestart = !request.start.performDemuxSeek;
        }
        FfmpegVideoDecodeResult result = decodeVideoFrames(
            request,
            [&sink](std::shared_ptr<DecodedVideoFrame const> frame, FfmpegVideoStreamDiagnostics const& diagnostics) {
                FfmpegVideoStreamDiagnostics nonseekable = diagnostics;
                nonseekable.seekable = false;
                return sink(std::move(frame), nonseekable);
            },
            stopToken);
        result.diagnostics.seekable = false;
        return result;
    });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seekable());

    auto const playbackAnchor = std::chrono::steady_clock::now();
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                 session.videoSource().prepareForPresentation(playbackAnchor + std::chrono::seconds(4));
                                 return session.ended();
                             }()),
                             5000);
    session.play();
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(sawNaturalZeroRestart.load());
    QCOMPARE(session.videoSource().currentFrame()->timing().ptsMicroseconds(), std::optional<std::int64_t>(0));
}

void MediaSessionTest::rejectsNonLocalUrls() {
    MediaSession session(VideoTargetReadback::Disabled);
    session.openMedia(QUrl(QStringLiteral("https://example.invalid/video.mkv")));

    QCOMPARE(session.state(), MediaSession::State::Error);
    QVERIFY(!session.hasFrame());
    QVERIFY(!session.errorMessage().isEmpty());
}

void MediaSessionTest::newerOpenRejectsStaleCompletion() {
    auto delayed = std::make_shared<DelayedStopOperation>();
    MediaSession session(VideoTargetReadback::Disabled,
                         [delayed](FfmpegVideoDecodeRequest const& request, FfmpegVideoFrameSink const& sink,
                                   std::stop_token stopToken) {
                             if (request.path.endsWith(QStringLiteral("blocked.mkv"))) {
                                 return delayed->wait(stopToken);
                             }
                             return decodeVideoFrames(request, sink, stopToken);
                         });

    session.openMedia(QUrl::fromLocalFile(QStringLiteral("blocked.mkv")));
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);
    std::uint64_t const blockedGeneration = session.playbackGeneration();

    session.openMedia(QUrl::fromLocalFile(fixturePath()));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QTRY_VERIFY_WITH_TIMEOUT(delayed->stopObserved.load(), 2000);
    QVERIFY(!delayed->exited.load());
    QVERIFY(session.playbackGeneration() != blockedGeneration);
    delayed->allowExit();
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(session.displayName(), QStringLiteral("sdr-bt709-ffv1.mkv"));
    QCOMPARE(session.videoSource().currentFrame()->identity().playbackGeneration, session.playbackGeneration());
}

void MediaSessionTest::cancelReturnsBeforeWorkerExit() {
    auto delayed = std::make_shared<DelayedStopOperation>();
    MediaSession session(VideoTargetReadback::Disabled,
                         [delayed](FfmpegVideoDecodeRequest const&, FfmpegVideoFrameSink const&,
                                   std::stop_token stopToken) { return delayed->wait(stopToken); });
    session.openMedia(QUrl::fromLocalFile(QStringLiteral("blocked.mkv")));
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);

    session.cancel();
    QCOMPARE(session.state(), MediaSession::State::Empty);
    QTRY_VERIFY_WITH_TIMEOUT(delayed->stopObserved.load(), 2000);
    QVERIFY(!delayed->exited.load());

    delayed->allowExit();
    QTRY_VERIFY_WITH_TIMEOUT(delayed->exited.load(), 2000);
    QCOMPARE(session.state(), MediaSession::State::Empty);
}

void MediaSessionTest::destructionCancelsWorker() {
    auto blocking = std::make_shared<BlockingOperation>();
    auto session = std::make_unique<MediaSession>(
        VideoTargetReadback::Disabled, [blocking](FfmpegVideoDecodeRequest const&, FfmpegVideoFrameSink const&,
                                                  std::stop_token stopToken) { return blocking->wait(stopToken); });
    session->openMedia(QUrl::fromLocalFile(QStringLiteral("blocked.mkv")));
    QTRY_VERIFY_WITH_TIMEOUT(blocking->started.load(), 2000);

    session.reset();
    QVERIFY(blocking->stopped.load());
}

void MediaSessionTest::presentationFailureBecomesSessionError() {
    MediaSession session(VideoTargetReadback::Disabled);
    session.openMedia(QUrl::fromLocalFile(fixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);

    QVERIFY(session.videoSource().reportPresentationFailure({
        .kind = VideoFailureKind::General,
        .reason = QStringLiteral("unsupported mapped surface"),
    }));
    QCOMPARE(session.state(), MediaSession::State::Error);
    QVERIFY(!session.hasFrame());
    QCOMPARE(session.errorMessage(), QStringLiteral("unsupported mapped surface"));
}

void MediaSessionTest::hardwareImportFailureRetriesSoftware() {
    std::atomic_int attempts = 0;
    std::atomic_int softwareFallbackAttempts = 0;
    MediaSession session(
        VideoTargetReadback::Disabled,
        [&attempts, &softwareFallbackAttempts](FfmpegVideoDecodeRequest const& request,
                                               FfmpegVideoFrameSink const& sink, std::stop_token stopToken) {
            ++attempts;
            if (!request.hardwareDecode.isAvailable() &&
                request.hardwareDecode.unavailableReason.contains(QStringLiteral("Hardware frame import failed"))) {
                ++softwareFallbackAttempts;
            }
            return decodeVideoFrames(request, sink, stopToken);
        });
    session.openMedia(QUrl::fromLocalFile(fixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    std::uint64_t const firstGeneration = session.playbackGeneration();

    QVERIFY(session.videoSource().reportPresentationFailure({
        .kind = VideoFailureKind::HardwareFrameImportUnavailable,
        .reason = QStringLiteral("Injected unsupported hardware surface"),
    }));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);

    QCOMPARE(attempts.load(), 2);
    QCOMPARE(softwareFallbackAttempts.load(), 1);
    QVERIFY(session.playbackGeneration() != firstGeneration);
    QCOMPARE(session.decodePath(), QStringLiteral("Software"));
    QVERIFY(session.hardwareFallbackReason().contains(QStringLiteral("Hardware frame import failed")));

    auto const playbackAnchor = std::chrono::steady_clock::now();
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                 session.videoSource().prepareForPresentation(playbackAnchor + std::chrono::seconds(1));
                                 return session.ended();
                             }()),
                             5000);
    std::uint64_t const completedGeneration = session.playbackGeneration();

    session.play();
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(attempts.load(), 3);
    QVERIFY(session.playbackGeneration() != completedGeneration);

    QVERIFY(session.videoSource().reportPresentationFailure({
        .kind = VideoFailureKind::HardwareFrameImportUnavailable,
        .reason = QStringLiteral("Injected replay hardware import failure"),
    }));
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(attempts.load(), 4);
    QCOMPARE(softwareFallbackAttempts.load(), 2);

    QVERIFY(session.videoSource().reportPresentationFailure({
        .kind = VideoFailureKind::HardwareFrameImportUnavailable,
        .reason = QStringLiteral("Injected repeated hardware import failure"),
    }));
    QCOMPARE(session.state(), MediaSession::State::Error);
    QCOMPARE(attempts.load(), 4);
}

void MediaSessionTest::hardwareImportFallbackRestartsAtPosition() {
    std::atomic<std::int64_t> fallbackPosition = -1;
    MediaSession session(VideoTargetReadback::Disabled, [&fallbackPosition](FfmpegVideoDecodeRequest const& request,
                                                                            FfmpegVideoFrameSink const& sink,
                                                                            std::stop_token stopToken) {
        if (request.hardwareDecode.unavailableReason.contains(QStringLiteral("Hardware frame import failed"))) {
            fallbackPosition = *request.start.targetPositionMicroseconds;
        }
        return decodeVideoFrames(request, sink, stopToken);
    });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    session.pause();
    session.seekToMilliseconds(1'250);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);

    QVERIFY(session.videoSource().reportPresentationFailure({
        .kind = VideoFailureKind::HardwareFrameImportUnavailable,
        .reason = QStringLiteral("Injected unsupported hardware surface"),
    }));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(fallbackPosition.load(), 1'250'000);
    QCOMPARE(session.positionMilliseconds(), 1'250);
    QVERIFY(!session.playing());
}

void MediaSessionTest::graphicsRecoveryRestartsSoftwareAtPosition() {
    std::atomic_int attempts = 0;
    std::atomic_bool sawReplacementCapability = false;
    std::atomic<std::int64_t> replacementPosition = -1;
    MediaSession session(VideoTargetReadback::Disabled, [&attempts, &sawReplacementCapability,
                                                         &replacementPosition](FfmpegVideoDecodeRequest const& request,
                                                                               FfmpegVideoFrameSink const& sink,
                                                                               std::stop_token stopToken) {
        ++attempts;
        if (request.hardwareDecode.unavailableReason == QStringLiteral("Replacement graphics domain has no hardware")) {
            sawReplacementCapability = true;
            if (request.start.targetPositionMicroseconds) {
                replacementPosition = *request.start.targetPositionMicroseconds;
            }
        }
        return decodeVideoFrames(request, sink, stopToken);
    });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    session.pause();
    session.seekToMilliseconds(1'250);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);

    session.invalidateGraphicsDevice();
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());
    session.setVideoDecodeCapability({
        .device = {},
        .unavailableReason = QStringLiteral("Replacement graphics domain has no hardware"),
    });

    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(sawReplacementCapability.load());
    QCOMPARE(replacementPosition.load(), 1'250'000);
    QCOMPARE(session.positionMilliseconds(), 1'250);

    replacementPosition = -1;
    session.seekToMilliseconds(2'000);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QCOMPARE(replacementPosition.load(), 2'000'000);
    QCOMPARE(attempts.load(), 4);
}

void MediaSessionTest::graphicsRecoveryPreservesPendingSeek() {
    auto delayed = std::make_shared<DelayedStopOperation>();
    std::atomic<std::int64_t> replacementPosition = -1;
    MediaSession session(VideoTargetReadback::Disabled,
                         [delayed, &replacementPosition](FfmpegVideoDecodeRequest const& request,
                                                         FfmpegVideoFrameSink const& sink, std::stop_token stopToken) {
                             if (request.start.targetPositionMicroseconds == 500'000) {
                                 return delayed->wait(stopToken);
                             }
                             if (request.hardwareDecode.unavailableReason ==
                                 QStringLiteral("Replacement graphics domain")) {
                                 replacementPosition = *request.start.targetPositionMicroseconds;
                             }
                             return decodeVideoFrames(request, sink, stopToken);
                         });
    session.openMedia(QUrl::fromLocalFile(playbackFixturePath()));
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    session.pause();
    session.seekToMilliseconds(500);
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);

    session.invalidateGraphicsDevice();
    QVERIFY(session.seeking());
    QCOMPARE(session.positionMilliseconds(), 500);
    QTRY_VERIFY_WITH_TIMEOUT(delayed->stopObserved.load(), 2000);
    session.seekToMilliseconds(1'000);
    QVERIFY(session.seeking());
    QCOMPARE(session.positionMilliseconds(), 1'000);

    session.setVideoDecodeCapability({
        .device = {},
        .unavailableReason = QStringLiteral("Replacement graphics domain"),
    });
    delayed->allowExit();
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);
    QVERIFY(!session.seeking());
    QCOMPARE(replacementPosition.load(), 1'000'000);
    QCOMPARE(session.positionMilliseconds(), 1'000);
    QVERIFY(!session.playing());
}

void MediaSessionTest::graphicsRecoverySupersedesOpening() {
    auto delayed = std::make_shared<DelayedStopOperation>();
    std::atomic_int attempts = 0;
    std::atomic_bool sawReplacementCapability = false;
    MediaSession session(VideoTargetReadback::Disabled,
                         [delayed, &attempts, &sawReplacementCapability](FfmpegVideoDecodeRequest const& request,
                                                                         FfmpegVideoFrameSink const& sink,
                                                                         std::stop_token stopToken) {
                             int const attempt = ++attempts;
                             if (attempt == 1) {
                                 return delayed->wait(stopToken);
                             }
                             sawReplacementCapability = request.hardwareDecode.unavailableReason ==
                                                        QStringLiteral("Replacement graphics domain has no hardware");
                             return decodeVideoFrames(request, sink, stopToken);
                         });
    session.openMedia(QUrl::fromLocalFile(fixturePath()));
    QTRY_VERIFY_WITH_TIMEOUT(delayed->started.load(), 2000);
    std::uint64_t const firstGeneration = session.playbackGeneration();

    session.invalidateGraphicsDevice();
    QCOMPARE(session.state(), MediaSession::State::Opening);
    QVERIFY(!session.hasFrame());
    QVERIFY(session.playbackGeneration() != firstGeneration);
    QTRY_VERIFY_WITH_TIMEOUT(delayed->stopObserved.load(), 2000);
    session.setVideoDecodeCapability({
        .device = {},
        .unavailableReason = QStringLiteral("Replacement graphics domain has no hardware"),
    });
    delayed->allowExit();
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), MediaSession::State::Ready, 5000);

    QCOMPARE(attempts.load(), 2);
    QVERIFY(sawReplacementCapability.load());
    QCOMPARE(session.videoSource().currentFrame()->identity().playbackGeneration, session.playbackGeneration());
}

// Worker results are delivered through queued Qt events, so this test needs
// QCoreApplication without loading a GUI platform plugin.
QTEST_GUILESS_MAIN(MediaSessionTest)
#include "tst_MediaSession.moc"
