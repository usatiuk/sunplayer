#include "playback/MediaSession.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include "media/DecodedVideoFrame.h"
#include "diagnostics/LogCategories.h"

namespace {
FfmpegVideoDecodeResult decodeVideo(
        const FfmpegVideoDecodeRequest &request,
        const FfmpegVideoFrameSink &sink,
        std::stop_token stopToken) {
    return decodeVideoFrames(
        request,
        sink,
        stopToken);
}
}

MediaSession::MediaSession(
        VideoTargetReadback readback,
        QObject *parent)
    : MediaSession(
        readback, decodeVideo, parent) {}

MediaSession::MediaSession(
        VideoTargetReadback readback,
        DecodeOperation decodeOperation,
        QObject *parent)
    : QObject(parent),
      m_decodeOperation(std::move(decodeOperation)),
      m_videoSource({}, readback) {
    Q_ASSERT(m_decodeOperation);
    m_videoSource.setFrameSelector(this);
    m_frameQueue.reset(m_playbackGeneration);
    connect(
        &m_videoSource,
        &DecodedVideoSource::presentationFailed,
        this,
        &MediaSession::handlePresentationFailure);
    connect(
        &m_videoSource,
        &DecodedVideoSource::frameChanged,
        this,
        &MediaSession::handleVideoFrameChanged);
    m_worker = std::jthread(
        [this](std::stop_token stopToken) {
            workerLoop(stopToken);
        });
}

MediaSession::~MediaSession() {
    Q_ASSERT(QThread::currentThread() == thread());
    shutdownWorker();
    m_videoSource.setFrameSelector(nullptr);
}

MediaSession::State MediaSession::state() const {
    return m_state;
}

QUrl MediaSession::mediaUrl() const {
    return m_mediaUrl;
}

QString MediaSession::displayName() const {
    return m_displayName;
}

QString MediaSession::errorMessage() const {
    return m_errorMessage;
}

QString MediaSession::containerFormat() const {
    return m_containerFormat;
}

QString MediaSession::decoderName() const {
    return m_decoderName;
}

QString MediaSession::decodePath() const {
    return m_decodePath;
}

QString MediaSession::hardwareFallbackReason() const {
    return m_hardwareFallbackReason;
}

QString MediaSession::videoSummary() const {
    return m_videoSummary;
}

bool MediaSession::hasFrame() const {
    return static_cast<bool>(m_videoSource.currentFrame());
}

bool MediaSession::playing() const {
    return m_state == State::Ready
        && m_userWantsPlaying
        && !m_ended;
}

bool MediaSession::ended() const {
    return m_ended;
}

bool MediaSession::seekable() const {
    return m_seekable;
}

bool MediaSession::seeking() const {
    return m_seeking;
}

qlonglong MediaSession::positionMilliseconds() const {
    const std::int64_t position =
        mediaClockSnapshotAt(
            std::chrono::steady_clock::now())
            .positionMicroseconds;
    const std::int64_t clamped =
        m_durationMicroseconds
        ? std::clamp<std::int64_t>(
            position, 0, *m_durationMicroseconds)
        : std::max<std::int64_t>(0, position);
    return clamped / 1'000;
}

qlonglong MediaSession::durationMilliseconds() const {
    return m_durationMicroseconds
        ? *m_durationMicroseconds / 1'000
        : -1;
}

std::uint64_t MediaSession::playbackGeneration() const {
    return m_playbackGeneration;
}

std::uint64_t MediaSession::decodedFrameCount() const {
    std::lock_guard lock(m_playbackMetricsMutex);
    return m_playbackMetricsGeneration
            == m_playbackGeneration
        ? m_decodedFrameCount
        : 0;
}

std::uint64_t MediaSession::selectedFrameCount() const {
    return m_selectedFrameCount;
}

std::uint64_t MediaSession::droppedFrameCount() const {
    return m_droppedFrameCount;
}

std::size_t MediaSession::queuedFrameCount() const {
    return m_frameQueue.size(m_playbackGeneration);
}

std::size_t MediaSession::maximumQueuedFrameCount() const {
    return m_frameQueue.maximumObservedSize();
}

int MediaSession::queuedVideoFrames() const {
    return static_cast<int>(queuedFrameCount());
}

DecodedVideoSource &MediaSession::videoSource() {
    return m_videoSource;
}

const DecodedVideoSource &MediaSession::videoSource() const {
    return m_videoSource;
}

void MediaSession::invalidateGraphicsDevice() {
    Q_ASSERT(QThread::currentThread() == thread());
    const bool recoverySeeking = m_seeking;
    const std::int64_t recoveryPosition =
        m_state == State::Opening
        ? m_requestedPositionMicroseconds
        : mediaClockSnapshotAt(
            std::chrono::steady_clock::now())
            .positionMicroseconds;
    m_videoDecodeCapability = {
        .device = {},
        .unavailableReason = QStringLiteral(
            "The graphics device is being recreated"),
    };
    m_activeVideoDecodeCapability =
        m_videoDecodeCapability;
    if (m_state != State::Opening
            && m_state != State::Ready) {
        return;
    }
    const QString path = m_mediaUrl.toLocalFile();
    if (path.isEmpty())
        return;

    advanceGeneration();
    const std::uint64_t generation =
        m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration)
        return;
    m_state = State::Opening;
    m_errorMessage.clear();
    resetPlayback(recoveryPosition);
    m_reopenAfterGraphicsRecovery = true;
    m_graphicsRecoveryPositionMicroseconds =
        recoveryPosition;
    m_graphicsRecoverySeeking = recoverySeeking;
    m_seeking = recoverySeeking;
    m_hardwareImportFallbackConsumed = false;
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::setVideoDecodeCapability(
        VideoHardwareDecodeCapability capability) {
    Q_ASSERT(QThread::currentThread() == thread());
    m_videoDecodeCapability = std::move(capability);
    if (!m_reopenAfterGraphicsRecovery) {
        m_activeVideoDecodeCapability =
            m_videoDecodeCapability;
        return;
    }

    m_reopenAfterGraphicsRecovery = false;
    const std::int64_t recoveryPosition =
        std::exchange(
            m_graphicsRecoveryPositionMicroseconds, 0);
    const bool seeking =
        std::exchange(m_graphicsRecoverySeeking, false);
    const QString path = m_mediaUrl.toLocalFile();
    if (!path.isEmpty()) {
        restartAt(
            recoveryPosition,
            m_videoDecodeCapability,
            seeking);
    }
}

void MediaSession::openMedia(const QUrl &url) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!url.isValid() || !url.isLocalFile()) {
        failWithoutWorker(
            url,
            tr("Only local media files are supported right now."));
        return;
    }

    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        failWithoutWorker(
            url,
            tr("The selected media path is empty."));
        return;
    }
    m_userWantsPlaying = true;
    m_reopenAfterGraphicsRecovery = false;
    m_graphicsRecoveryPositionMicroseconds = 0;
    m_graphicsRecoverySeeking = false;
    m_hardwareImportFallbackConsumed = false;
    startOpen(url, path, m_videoDecodeCapability);
}

void MediaSession::cancel() {
    Q_ASSERT(QThread::currentThread() == thread());
    advanceGeneration();
    const std::uint64_t generation =
        m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration)
        return;
    m_reopenAfterGraphicsRecovery = false;
    m_graphicsRecoveryPositionMicroseconds = 0;
    m_graphicsRecoverySeeking = false;
    m_hardwareImportFallbackConsumed = false;
    m_userWantsPlaying = true;
    m_state = State::Empty;
    m_mediaUrl = {};
    m_displayName.clear();
    m_errorMessage.clear();
    resetDiagnostics();
    resetPlayback();
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::retry() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_mediaUrl.isValid())
        return;
    openMedia(m_mediaUrl);
}

void MediaSession::play() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != State::Ready)
        return;
    if (m_ended) {
        const QString path = m_mediaUrl.toLocalFile();
        if (!path.isEmpty()) {
            m_userWantsPlaying = true;
            m_hardwareImportFallbackConsumed = false;
            restartAt(
                0,
                m_videoDecodeCapability,
                false);
        }
        return;
    }
    if (m_userWantsPlaying)
        return;
    m_userWantsPlaying = true;
    m_clockAnchorTime =
        std::chrono::steady_clock::now();
    emit sessionChanged();
    emit timelineChanged();
    m_videoSource.requestFrameSelection();
}

void MediaSession::pause() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != State::Ready
            || !m_userWantsPlaying
            || m_ended) {
        return;
    }
    const auto now =
        std::chrono::steady_clock::now();
    m_clockAnchorMediaMicroseconds =
        mediaClockSnapshotAt(now)
            .positionMicroseconds;
    m_clockAnchorTime = now;
    m_userWantsPlaying = false;
    emit sessionChanged();
    emit timelineChanged();
    m_videoSource.requestFrameSelection();
}

void MediaSession::seekToMilliseconds(
        qlonglong positionMilliseconds) {
    Q_ASSERT(QThread::currentThread() == thread());
    if ((!m_seekable && !m_seeking)
            || !m_durationMicroseconds
            || (m_state != State::Ready
                && !m_seeking)) {
        return;
    }

    const std::int64_t duration =
        *m_durationMicroseconds;
    const std::int64_t requested =
        positionMilliseconds <= 0
        ? 0
        : positionMilliseconds
                >= duration / 1'000
            ? duration
            : static_cast<std::int64_t>(
                positionMilliseconds) * 1'000;
    if (m_reopenAfterGraphicsRecovery) {
        advanceGeneration();
        const std::uint64_t generation =
            m_playbackGeneration;
        cancelPipeline();
        m_frameQueue.reset(generation);
        m_videoSource.clearFrame();
        if (generation != m_playbackGeneration)
            return;
        m_state = State::Opening;
        m_errorMessage.clear();
        resetPlayback(requested);
        m_seeking = true;
        m_graphicsRecoveryPositionMicroseconds =
            requested;
        m_graphicsRecoverySeeking = true;
        publishSessionAndPlaybackMetrics(generation);
        return;
    }
    restartAt(
        requested,
        m_activeVideoDecodeCapability,
        true);
}

void MediaSession::startOpen(
        const QUrl &url,
        const QString &path,
        VideoHardwareDecodeCapability hardwareDecode) {
    startDecode(
        url,
        path,
        std::move(hardwareDecode),
        0,
        true,
        false);
}

void MediaSession::restartAt(
        std::int64_t positionMicroseconds,
        VideoHardwareDecodeCapability hardwareDecode,
        bool seeking) {
    const QString path = m_mediaUrl.toLocalFile();
    if (path.isEmpty())
        return;
    startDecode(
        m_mediaUrl,
        path,
        std::move(hardwareDecode),
        positionMicroseconds,
        false,
        seeking);
}

void MediaSession::startDecode(
        const QUrl &url,
        const QString &path,
        VideoHardwareDecodeCapability hardwareDecode,
        std::int64_t requestedPositionMicroseconds,
        bool newMedia,
        bool seeking) {
    Q_ASSERT(requestedPositionMicroseconds >= 0);
    advanceGeneration();
    const std::uint64_t generation =
        m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration)
        return;
    m_state = State::Opening;
    m_errorMessage.clear();
    if (newMedia) {
        m_mediaUrl = url;
        m_displayName = QFileInfo(path).fileName();
        resetDiagnostics();
    }
    m_activeVideoDecodeCapability = hardwareDecode;
    resetPlayback(requestedPositionMicroseconds);
    m_seeking = seeking;

    qCInfo(sunroomLogPlayback).noquote()
        << (
            newMedia
            ? "event=playback.open_start"
            : seeking
                ? "event=playback.seek_start"
                : "event=playback.restart_start")
        << "generation=" + QString::number(generation)
        << "positionUs=" + QString::number(
            requestedPositionMicroseconds);
    qCDebug(sunroomLogPlayback).noquote()
        << "event=playback.decode_request"
        << "generation=" + QString::number(generation)
        << "path=" + path;

    std::int64_t decodePosition =
        requestedPositionMicroseconds;
    if (m_durationMicroseconds
            && decodePosition >= *m_durationMicroseconds) {
        decodePosition = std::max<std::int64_t>(
            0, *m_durationMicroseconds - 1);
    }
    const bool seekToTarget =
        !newMedia && m_timelineOrigin.has_value();

    const VideoFrameIdentity identity{
        .playbackGeneration = generation,
        .decoderRevision = 1,
        .frameId = 1,
    };
    submitOpen({
        .generation = generation,
        .decode = {
            .path = path,
            .firstFrameIdentity = identity,
            .hardwareDecode = std::move(hardwareDecode),
            .extraHardwareFrames =
                static_cast<int>(
                    VideoFrameQueue::capacity + 2),
            .start = {
                .targetPositionMicroseconds =
                    seekToTarget
                    ? std::optional<std::int64_t>(
                        decodePosition)
                    : std::nullopt,
                .timelineOrigin = m_timelineOrigin,
                .performDemuxSeek =
                    seekToTarget
                    && (decodePosition > 0
                        || m_seekable),
            },
        },
    });
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::submitOpen(OpenRequest request) {
    {
        std::lock_guard lock(m_workerMutex);
        m_operationStopSource.request_stop();
        m_pendingOpen = std::move(request);
    }
    m_workerWake.notify_one();
}

void MediaSession::cancelPipeline() {
    {
        std::lock_guard lock(m_workerMutex);
        m_operationStopSource.request_stop();
        m_pendingOpen.reset();
    }
    m_workerWake.notify_one();
}

void MediaSession::workerLoop(
        std::stop_token workerStopToken) {
    while (!workerStopToken.stop_requested()) {
        OpenRequest request;
        std::stop_token operationStopToken;
        {
            std::unique_lock lock(m_workerMutex);
            m_workerWake.wait(
                lock,
                workerStopToken,
                [this] {
                    return m_pendingOpen.has_value();
                });
            if (workerStopToken.stop_requested())
                return;

            request = std::move(*m_pendingOpen);
            m_pendingOpen.reset();
            m_operationStopSource = std::stop_source();
            operationStopToken =
                m_operationStopSource.get_token();
        }

        VideoFrameTimeline timeline(
            request.decode.start.timelineOrigin);
        VideoSeekPrerollGate preroll(
            request.decode.start
                .targetPositionMicroseconds);
        const auto operationStarted =
            std::chrono::steady_clock::now();
        std::uint64_t seekPrerollFrames = 0;
        std::optional<std::int64_t>
            firstSeekTimelineMicroseconds;
        bool seekAdmissionLogged = false;
        FfmpegVideoDecodeResult result =
            m_decodeOperation(
                request.decode,
                [this,
                 generation = request.generation,
                 targetPosition =
                    request.decode.start
                        .targetPositionMicroseconds,
                 &timeline,
                 &preroll,
                 &seekPrerollFrames,
                 &firstSeekTimelineMicroseconds,
                 &seekAdmissionLogged,
                 operationStarted,
                 operationStopToken](
                        std::shared_ptr<
                            const DecodedVideoFrame> frame,
                        const FfmpegVideoStreamDiagnostics
                            &diagnostics) {
                    if (!recordDecodedFrame(generation))
                        return false;
                    postPlaybackMetricsChanged(generation);
                    QueuedVideoFrame queued =
                        timeline.schedule(
                            std::move(frame), diagnostics);
                    if (targetPosition
                            && !seekAdmissionLogged) {
                        ++seekPrerollFrames;
                        if (!firstSeekTimelineMicroseconds) {
                            firstSeekTimelineMicroseconds =
                                queued.timelineTimeMicroseconds;
                            qCDebug(sunroomLogPlayback).noquote()
                                << "event=playback.seek_first_frame"
                                << "generation="
                                    + QString::number(generation)
                                << "targetUs=" + QString::number(
                                    *targetPosition)
                                << "timelineUs=" + QString::number(
                                    queued.timelineTimeMicroseconds)
                                << "rawPts=" + (
                                    queued.frame->timing().pts
                                    ? QString::number(
                                        *queued.frame
                                            ->timing().pts)
                                    : QStringLiteral("none"));
                        }
                    }
                    VideoSeekPrerollAdmission admitted =
                        preroll.admit(std::move(queued));
                    if (targetPosition
                            && !seekAdmissionLogged
                            && (admitted.first
                                || admitted.second)) {
                        seekAdmissionLogged = true;
                        const QueuedVideoFrame &firstAdmitted =
                            admitted.first
                            ? *admitted.first
                            : *admitted.second;
                        const auto elapsed =
                            std::chrono::duration_cast<
                                std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now()
                                    - operationStarted)
                                .count();
                        qCInfo(sunroomLogPlayback).noquote()
                            << "event=playback.seek_preroll_complete"
                            << "generation="
                                + QString::number(generation)
                            << "targetUs=" + QString::number(
                                *targetPosition)
                            << "firstTimelineUs=" + QString::number(
                                firstSeekTimelineMicroseconds
                                    .value_or(0))
                            << "admittedTimelineUs="
                                + QString::number(
                                    firstAdmitted
                                        .timelineTimeMicroseconds)
                            << "decodedFrames=" + QString::number(
                                seekPrerollFrames)
                            << "elapsedMs=" + QString::number(
                                elapsed);
                    } else if (targetPosition
                            && !seekAdmissionLogged
                            && seekPrerollFrames % 64 == 0) {
                        qCDebug(sunroomLogPlayback).noquote()
                            << "event=playback.seek_preroll_progress"
                            << "generation="
                                + QString::number(generation)
                            << "targetUs=" + QString::number(
                                *targetPosition)
                            << "decodedFrames=" + QString::number(
                                seekPrerollFrames);
                    }
                    const auto publish =
                        [this,
                         generation,
                         operationStopToken](
                                std::optional<
                                    QueuedVideoFrame> &ready) {
                        if (!ready)
                            return true;
                        if (!m_frameQueue.push(
                                generation,
                                std::move(*ready),
                                operationStopToken)) {
                            return false;
                        }
                        postFramesAvailable(generation);
                        return true;
                    };
                    return publish(admitted.first)
                        && publish(admitted.second);
                },
                operationStopToken);
        const auto operationElapsed =
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()
                    - operationStarted)
                .count();
        if (result.isSuccess()) {
            std::optional<QueuedVideoFrame> endFallback =
                preroll.finish();
            if (endFallback) {
                const std::int64_t fallbackTimeline =
                    endFallback
                        ->timelineTimeMicroseconds;
                if (m_frameQueue.push(
                        request.generation,
                        std::move(*endFallback),
                        operationStopToken)) {
                    postFramesAvailable(
                        request.generation);
                    if (request.decode.start
                                .targetPositionMicroseconds
                            && !seekAdmissionLogged) {
                        seekAdmissionLogged = true;
                        qCInfo(
                            sunroomLogPlayback).noquote()
                            << "event=playback.seek_preroll_fallback_complete"
                            << "generation="
                                + QString::number(
                                    request.generation)
                            << "targetUs=" + QString::number(
                                *request.decode.start
                                    .targetPositionMicroseconds)
                            << "fallbackTimelineUs="
                                + QString::number(
                                    fallbackTimeline)
                            << "decodedFrames="
                                + QString::number(
                                    seekPrerollFrames)
                            << "elapsedMs="
                                + QString::number(
                                    operationElapsed);
                    }
                }
            }
        }
        if (request.decode.start.targetPositionMicroseconds
                && !seekAdmissionLogged
                && result.isSuccess()
                && !operationStopToken.stop_requested()) {
            qCWarning(sunroomLogPlayback).noquote()
                << "event=playback.seek_no_admitted_frame"
                << "generation="
                    + QString::number(request.generation)
                << "targetUs=" + QString::number(
                    *request.decode.start
                        .targetPositionMicroseconds)
                << "firstTimelineUs=" + (
                    firstSeekTimelineMicroseconds
                    ? QString::number(
                        *firstSeekTimelineMicroseconds)
                    : QStringLiteral("none"))
                << "decodedFrames=" + QString::number(
                    seekPrerollFrames)
                << "elapsedMs=" + QString::number(
                    operationElapsed);
        }
        if (result.isCancelled()) {
            qCDebug(sunroomLogPlayback).noquote()
                << "event=playback.decode_cancelled"
                << "generation="
                    + QString::number(request.generation)
                << "frames=" + QString::number(
                    result.framesDecoded)
                << "elapsedMs=" + QString::number(
                    operationElapsed);
        } else if (result.isSuccess()) {
            qCInfo(sunroomLogPlayback).noquote()
                << "event=playback.decode_complete"
                << "generation="
                    + QString::number(request.generation)
                << "frames=" + QString::number(
                    result.framesDecoded)
                << "elapsedMs=" + QString::number(
                    operationElapsed)
                << "endOfStream=" + QString(
                    result.endOfStream
                    ? QStringLiteral("true")
                    : QStringLiteral("false"))
                << "stopped=" + QString(
                    result.stopped
                    ? QStringLiteral("true")
                    : QStringLiteral("false"));
        } else {
            qCWarning(sunroomLogPlayback).noquote()
                << "event=playback.decode_failed"
                << "generation="
                    + QString::number(request.generation)
                << "frames=" + QString::number(
                    result.framesDecoded)
                << "elapsedMs=" + QString::number(
                    operationElapsed)
                << "error=" + result.error;
        }
        if (workerStopToken.stop_requested())
            return;

        QMetaObject::invokeMethod(
            this,
            [this,
             generation = request.generation,
             result = std::move(result)]() mutable {
                completeDecode(
                    generation, std::move(result));
            },
            Qt::QueuedConnection);
    }
}

void MediaSession::completeDecode(
        std::uint64_t generation,
        FfmpegVideoDecodeResult result) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation != m_playbackGeneration
            || (m_state != State::Opening
                && m_state != State::Ready)) {
        return;
    }
    if (result.isCancelled())
        return;
    if (!result.isSuccess()) {
        m_frameQueue.reset(m_playbackGeneration);
        m_videoSource.clearFrame();
        if (generation != m_playbackGeneration)
            return;
        m_state = State::Error;
        m_errorMessage = result.error.isEmpty()
            ? tr("The selected media could not be decoded.")
            : result.error;
        resetPlayback();
        publishSessionAndPlaybackMetrics(generation);
        return;
    }

    m_decoderDrained = result.endOfStream;
    handleFramesAvailable(generation);
}

void MediaSession::postFramesAvailable(
        std::uint64_t generation) {
    if (!m_frameWake.request(generation))
        return;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_frameWake.drain(
                [this](std::uint64_t generation) {
                    handleFramesAvailable(generation);
                });
        },
        Qt::QueuedConnection);
}

bool MediaSession::recordDecodedFrame(
        std::uint64_t generation) {
    std::lock_guard lock(m_playbackMetricsMutex);
    if (generation != m_playbackMetricsGeneration)
        return false;
    ++m_decodedFrameCount;
    return true;
}

void MediaSession::postPlaybackMetricsChanged(
        std::uint64_t generation) {
    if (!m_metricsWake.request(generation))
        return;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_metricsWake.drain(
                [this](std::uint64_t generation) {
                    if (generation
                            == m_playbackGeneration) {
                        emit playbackMetricsChanged();
                    }
                });
        },
        Qt::QueuedConnection);
}

void MediaSession::handleFramesAvailable(
        std::uint64_t generation) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation != m_playbackGeneration
            || (m_state != State::Opening
                && m_state != State::Ready)) {
        return;
    }
    if (m_state == State::Opening) {
        std::shared_ptr<const DecodedVideoFrame> selected =
            selectFrameForPresentation(
                std::chrono::steady_clock::now());
        if (selected)
            m_videoSource.setFrame(std::move(selected));
    } else {
        m_videoSource.requestFrameSelection();
    }
    if (generation != m_playbackGeneration)
        return;
    emit playbackMetricsChanged();
}

void MediaSession::handleVideoFrameChanged() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pendingPublicationGeneration
            != m_playbackGeneration) {
        return;
    }

    const std::uint64_t generation =
        m_pendingPublicationGeneration;
    const bool notifySession =
        std::exchange(
            m_sessionNotificationPending, false);
    const bool notifyMetrics =
        std::exchange(
            m_playbackMetricsNotificationPending, false);
    m_pendingPublicationGeneration = 0;

    if (notifySession)
        emit sessionChanged();
    if (generation == m_playbackGeneration)
        emit timelineChanged();
    if (generation == m_playbackGeneration
            && notifyMetrics) {
        emit playbackMetricsChanged();
    }
}

void MediaSession::failWithoutWorker(
        const QUrl &url,
        const QString &message) {
    advanceGeneration();
    const std::uint64_t generation =
        m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration)
        return;
    m_reopenAfterGraphicsRecovery = false;
    m_graphicsRecoverySeeking = false;
    m_hardwareImportFallbackConsumed = false;
    m_state = State::Error;
    m_mediaUrl = url;
    m_displayName = url.fileName();
    m_errorMessage = message;
    resetDiagnostics();
    resetPlayback();
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::handlePresentationFailure(
        const VideoFailure &failure) {
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(failure.isValid());
    if (m_state != State::Ready)
        return;
    if (failure.kind
            == VideoFailureKind::
                HardwareFrameImportUnavailable
            && !m_hardwareImportFallbackConsumed) {
        const QString path = m_mediaUrl.toLocalFile();
        if (!path.isEmpty()) {
            const std::int64_t restartPosition =
                mediaClockSnapshotAt(
                    std::chrono::steady_clock::now())
                    .positionMicroseconds;
            m_hardwareImportFallbackConsumed = true;
            restartAt(
                restartPosition,
                {
                    .device = {},
                    .unavailableReason =
                        tr("Hardware frame import failed; "
                           "using software decode: %1")
                            .arg(failure.reason),
                },
                false);
            return;
        }
    }

    advanceGeneration();
    const std::uint64_t generation =
        m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration)
        return;
    m_state = State::Error;
    m_errorMessage = failure.reason;
    resetPlayback();
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::shutdownWorker() {
    if (!m_worker.joinable())
        return;
    m_worker.request_stop();
    {
        std::lock_guard lock(m_workerMutex);
        m_operationStopSource.request_stop();
        m_pendingOpen.reset();
    }
    m_frameQueue.reset(0);
    m_workerWake.notify_all();
    m_worker.join();
}

void MediaSession::advanceGeneration() {
    ++m_playbackGeneration;
    if (m_playbackGeneration == 0)
        ++m_playbackGeneration;
}

void MediaSession::resetDiagnostics() {
    m_containerFormat.clear();
    m_decoderName.clear();
    m_decodePath.clear();
    m_hardwareFallbackReason.clear();
    m_videoSummary.clear();
    m_seekable = false;
    m_durationMicroseconds.reset();
    m_timelineOrigin.reset();
}

void MediaSession::resetPlayback(
        std::int64_t positionMicroseconds) {
    Q_ASSERT(positionMicroseconds >= 0);
    {
        std::lock_guard lock(m_playbackMetricsMutex);
        m_playbackMetricsGeneration =
            m_playbackGeneration;
        m_decodedFrameCount = 0;
    }
    m_decoderDrained = false;
    m_ended = false;
    m_seeking = false;
    m_clockAnchorTime.reset();
    m_clockAnchorMediaMicroseconds =
        positionMicroseconds;
    m_requestedPositionMicroseconds =
        positionMicroseconds;
    m_frameScheduler.reset();
    m_selectedFrameCount = 0;
    m_droppedFrameCount = 0;
    m_pendingPublicationGeneration = 0;
    m_sessionNotificationPending = false;
    m_playbackMetricsNotificationPending = false;
}

void MediaSession::publishSessionAndPlaybackMetrics(
        std::uint64_t generation) {
    if (generation != m_playbackGeneration)
        return;
    emit sessionChanged();
    emit timelineChanged();
    if (generation == m_playbackGeneration)
        emit playbackMetricsChanged();
}

void MediaSession::applyDiagnostics(
        const FfmpegVideoStreamDiagnostics &diagnostics) {
    m_containerFormat = diagnostics.containerFormat;
    m_decoderName = diagnostics.decoderName;
    m_decodePath = diagnostics.decodePath;
    m_hardwareFallbackReason =
        diagnostics.hardwareFallbackReason;
    m_durationMicroseconds =
        diagnostics.durationMicroseconds;
    m_timelineOrigin = diagnostics.timelineOrigin;
    m_seekable = diagnostics.seekable
        && m_durationMicroseconds
        && m_timelineOrigin;
}

MediaClockSnapshot MediaSession::mediaClockSnapshotAt(
        std::chrono::steady_clock::time_point now) const {
    if (!m_clockAnchorTime || !playing()) {
        return {
            .positionMicroseconds =
                m_clockAnchorMediaMicroseconds,
            .advancing = false,
        };
    }
    const auto elapsed =
        std::chrono::duration_cast<
            std::chrono::microseconds>(
                now - *m_clockAnchorTime)
            .count();
    return {
        .positionMicroseconds =
            m_clockAnchorMediaMicroseconds
            + std::max<std::int64_t>(0, elapsed),
        .advancing = true,
    };
}

std::shared_ptr<const DecodedVideoFrame>
MediaSession::selectFrameForPresentation(
        std::chrono::steady_clock::time_point now) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == State::Opening) {
        VideoFrameSelection selection =
            m_frameScheduler.selectFirst(
                m_frameQueue,
                m_playbackGeneration);
        if (!selection.frame)
            return {};
        QueuedVideoFrame first =
            std::move(*selection.frame);

        applyDiagnostics(first.diagnostics);
        const VideoFrameGeometry &geometry =
            first.frame->geometry();
        const VideoSignalDescription &signal =
            first.frame->signal();
        m_videoSummary = tr("%1×%2 · %3 · %4-bit")
            .arg(geometry.visibleSize.width())
            .arg(geometry.visibleSize.height())
            .arg(signal.pixelFormat)
            .arg(signal.componentDepth);
        m_clockAnchorMediaMicroseconds =
            std::max(
                m_requestedPositionMicroseconds,
                first.presentationTimeMicroseconds);
        m_clockAnchorTime = now;
        m_state = State::Ready;
        m_seeking = false;
        if (m_durationMicroseconds
                && m_requestedPositionMicroseconds
                    >= *m_durationMicroseconds) {
            m_clockAnchorMediaMicroseconds =
                *m_durationMicroseconds;
            m_ended = true;
            m_userWantsPlaying = false;
        }
        ++m_selectedFrameCount;
        m_pendingPublicationGeneration =
            m_playbackGeneration;
        m_sessionNotificationPending = true;
        m_playbackMetricsNotificationPending = true;
        return std::move(first.frame);
    }

    const MediaClockSnapshot clock =
        mediaClockSnapshotAt(now);
    VideoFrameSelection selection =
        m_frameScheduler.selectForPresentation(
            m_frameQueue,
            m_playbackGeneration,
            clock,
            m_decoderDrained,
            m_durationMicroseconds);
    if (selection.frame) {
        m_droppedFrameCount +=
            selection.droppedFrames;
        ++m_selectedFrameCount;
    }

    if (selection.reachedEnd) {
        Q_ASSERT(selection.mediaEndMicroseconds);
        m_clockAnchorMediaMicroseconds =
            *selection.mediaEndMicroseconds;
        m_clockAnchorTime = now;
        m_ended = true;
        m_userWantsPlaying = false;
    }

    if (selection.frame) {
        m_pendingPublicationGeneration =
            m_playbackGeneration;
        m_sessionNotificationPending =
            selection.reachedEnd;
        m_playbackMetricsNotificationPending = true;
    } else if (selection.reachedEnd) {
        emit sessionChanged();
        emit timelineChanged();
        emit playbackMetricsChanged();
    }

    return selection.frame
        ? std::move(selection.frame->frame)
        : std::shared_ptr<const DecodedVideoFrame>();
}

bool MediaSession::wantsContinuousVideoFrames() const {
    return playing();
}
