#include "playback/MediaSession.h"

#include <utility>

#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include "media/DecodedVideoFrame.h"

namespace {
FfmpegFirstFrameResult decodeFirstFrame(
        const QString &path,
        const VideoFrameIdentity &identity,
        const VideoHardwareDecodeCapability &hardwareDecode,
        std::stop_token stopToken) {
    return decodeFirstVideoFrame(
        path, identity, hardwareDecode, stopToken);
}
}

MediaSession::MediaSession(
        VideoTargetReadback readback,
        QObject *parent)
    : MediaSession(
        readback, decodeFirstFrame, parent) {}

MediaSession::MediaSession(
        VideoTargetReadback readback,
        DecodeOperation decodeOperation,
        QObject *parent)
    : QObject(parent),
      m_decodeOperation(std::move(decodeOperation)),
      m_videoSource({}, readback) {
    Q_ASSERT(m_decodeOperation);
    connect(
        &m_videoSource,
        &DecodedVideoSource::presentationFailed,
        this,
        &MediaSession::handlePresentationFailure);
    m_worker = std::jthread(
        [this](std::stop_token stopToken) {
            workerLoop(stopToken);
        });
}

MediaSession::~MediaSession() {
    Q_ASSERT(QThread::currentThread() == thread());
    shutdownWorker();
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

std::uint64_t MediaSession::playbackGeneration() const {
    return m_playbackGeneration;
}

DecodedVideoSource &MediaSession::videoSource() {
    return m_videoSource;
}

const DecodedVideoSource &MediaSession::videoSource() const {
    return m_videoSource;
}

void MediaSession::invalidateGraphicsDevice() {
    Q_ASSERT(QThread::currentThread() == thread());
    m_videoDecodeCapability = {
        .device = {},
        .unavailableReason = QStringLiteral(
            "The graphics device is being recreated"),
    };
    if (m_state != State::Opening
            && m_state != State::Ready) {
        return;
    }
    if (m_state == State::Ready) {
        const std::shared_ptr<const DecodedVideoFrame> frame =
            m_videoSource.currentFrame();
        if (frame && !frame->storage().isHardware())
            return;
    }

    const QString path = m_mediaUrl.toLocalFile();
    if (path.isEmpty())
        return;

    advanceGeneration();
    cancelOpen();
    m_videoSource.clearFrame();
    m_state = State::Opening;
    m_errorMessage.clear();
    resetDiagnostics();
    m_reopenAfterGraphicsRecovery = true;
    m_hardwareImportFallbackConsumed = false;
    emit sessionChanged();
}

void MediaSession::setVideoDecodeCapability(
        VideoHardwareDecodeCapability capability) {
    Q_ASSERT(QThread::currentThread() == thread());
    m_videoDecodeCapability = std::move(capability);
    if (!m_reopenAfterGraphicsRecovery)
        return;

    m_reopenAfterGraphicsRecovery = false;
    const QString path = m_mediaUrl.toLocalFile();
    if (!path.isEmpty()) {
        startOpen(
            m_mediaUrl,
            path,
            m_videoDecodeCapability);
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
    m_reopenAfterGraphicsRecovery = false;
    m_hardwareImportFallbackConsumed = false;
    startOpen(url, path, m_videoDecodeCapability);
}

void MediaSession::cancel() {
    Q_ASSERT(QThread::currentThread() == thread());
    advanceGeneration();
    cancelOpen();
    m_videoSource.clearFrame();
    m_reopenAfterGraphicsRecovery = false;
    m_hardwareImportFallbackConsumed = false;
    m_state = State::Empty;
    m_mediaUrl = {};
    m_displayName.clear();
    m_errorMessage.clear();
    resetDiagnostics();
    emit sessionChanged();
}

void MediaSession::retry() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_mediaUrl.isValid())
        return;
    openMedia(m_mediaUrl);
}

void MediaSession::startOpen(
        const QUrl &url,
        const QString &path,
        VideoHardwareDecodeCapability hardwareDecode) {
    advanceGeneration();
    m_videoSource.clearFrame();
    m_state = State::Opening;
    m_mediaUrl = url;
    m_displayName = QFileInfo(path).fileName();
    m_errorMessage.clear();
    resetDiagnostics();
    emit sessionChanged();

    const std::uint64_t generation =
        m_playbackGeneration;
    const VideoFrameIdentity identity{
        .playbackGeneration = generation,
        .decoderRevision = 1,
        .frameId = 1,
    };
    submitOpen({
        .generation = generation,
        .path = path,
        .identity = identity,
        .hardwareDecode = std::move(hardwareDecode),
    });
}

void MediaSession::submitOpen(OpenRequest request) {
    {
        std::lock_guard lock(m_workerMutex);
        m_operationStopSource.request_stop();
        m_pendingOpen = std::move(request);
    }
    m_workerWake.notify_one();
}

void MediaSession::cancelOpen() {
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

        FfmpegFirstFrameResult result =
            m_decodeOperation(
                request.path,
                request.identity,
                request.hardwareDecode,
                operationStopToken);
        if (workerStopToken.stop_requested())
            return;

        QMetaObject::invokeMethod(
            this,
            [this,
             generation = request.generation,
             result = std::move(result)]() mutable {
                completeOpen(
                    generation, std::move(result));
            },
            Qt::QueuedConnection);
    }
}

void MediaSession::completeOpen(
        std::uint64_t generation,
        FfmpegFirstFrameResult result) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation != m_playbackGeneration
            || m_state != State::Opening) {
        return;
    }

    if (result.isCancelled()) {
        m_state = State::Empty;
        m_errorMessage.clear();
        emit sessionChanged();
        return;
    }
    if (!result.isSuccess()) {
        m_state = State::Error;
        m_errorMessage = result.error.isEmpty()
            ? tr("The selected media could not be opened.")
            : result.error;
        emit sessionChanged();
        return;
    }

    m_containerFormat =
        result.diagnostics.containerFormat;
    m_decoderName = result.diagnostics.decoderName;
    m_decodePath = result.diagnostics.decodePath;
    m_hardwareFallbackReason =
        result.diagnostics.hardwareFallbackReason;
    const VideoFrameGeometry &geometry =
        result.frame->geometry();
    const VideoSignalDescription &signal =
        result.frame->signal();
    m_videoSummary = tr("%1×%2 · %3 · %4-bit")
        .arg(geometry.visibleSize.width())
        .arg(geometry.visibleSize.height())
        .arg(signal.pixelFormat)
        .arg(signal.componentDepth);
    m_videoSource.setFrame(std::move(result.frame));
    m_state = State::Ready;
    emit sessionChanged();
}

void MediaSession::failWithoutWorker(
        const QUrl &url,
        const QString &message) {
    advanceGeneration();
    cancelOpen();
    m_videoSource.clearFrame();
    m_reopenAfterGraphicsRecovery = false;
    m_hardwareImportFallbackConsumed = false;
    m_state = State::Error;
    m_mediaUrl = url;
    m_displayName = url.fileName();
    m_errorMessage = message;
    resetDiagnostics();
    emit sessionChanged();
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
            m_hardwareImportFallbackConsumed = true;
            startOpen(
                m_mediaUrl,
                path,
                {
                    .device = {},
                    .unavailableReason =
                        tr("Hardware frame import failed; "
                           "using software decode: %1")
                            .arg(failure.reason),
                });
            return;
        }
    }

    m_videoSource.clearFrame();
    m_state = State::Error;
    m_errorMessage = failure.reason;
    emit sessionChanged();
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
}
