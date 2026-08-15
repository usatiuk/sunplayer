#include "playback/MediaSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QStringList>
#include <QThread>

#include "audio/CubebAudioSink.h"
#include "diagnostics/LogCategories.h"
#include "media/DecodedVideoFrame.h"

namespace {
constexpr int playbackMonitorIntervalMilliseconds = 100;
constexpr auto sustainedAudioHoldGrace = std::chrono::milliseconds(500);
constexpr auto audioClockUnavailableGrace = std::chrono::seconds(1);

FfmpegMediaDecodeResult decodeMedia(FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
                                    FfmpegAudioOutputSink const& audioSink, FfmpegMediaStreamSink const& streamSink,
                                    FfmpegSubtitleOutputSink const& subtitleSink, std::stop_token stopToken) {
    return decodeMediaFrames(request, videoSink, audioSink, streamSink, subtitleSink, stopToken);
}

std::shared_ptr<AudioSink> createAudioSink() { return std::make_shared<CubebAudioSink>(); }

QString dynamicRangeName(VideoDynamicRange range) {
    switch (range) {
    case VideoDynamicRange::Sdr:
        return MediaSession::tr("SDR");
    case VideoDynamicRange::Pq:
        return MediaSession::tr("PQ HDR");
    case VideoDynamicRange::Hdr10:
        return MediaSession::tr("HDR10");
    case VideoDynamicRange::Hdr10Plus:
        return MediaSession::tr("HDR10+");
    case VideoDynamicRange::Hlg:
        return MediaSession::tr("HLG");
    case VideoDynamicRange::DolbyVision:
        return MediaSession::tr("Dolby Vision");
    case VideoDynamicRange::Unknown:
        return MediaSession::tr("Unknown");
    }
    return {};
}

bool isHdr(VideoDynamicRange range) {
    return range == VideoDynamicRange::Pq || range == VideoDynamicRange::Hdr10 ||
           range == VideoDynamicRange::Hdr10Plus || range == VideoDynamicRange::Hlg ||
           range == VideoDynamicRange::DolbyVision;
}

QString colorName(QString const& name) {
    if (name == QStringLiteral("bt709")) {
        return QStringLiteral("BT.709");
    }
    if (name == QStringLiteral("bt2020")) {
        return QStringLiteral("BT.2020");
    }
    if (name == QStringLiteral("smpte2084")) {
        return QStringLiteral("PQ");
    }
    if (name == QStringLiteral("arib-std-b67")) {
        return QStringLiteral("HLG");
    }
    if (name == QStringLiteral("iec61966-2-1")) {
        return QStringLiteral("sRGB");
    }
    return name;
}

QString rangeName(QString const& range) {
    if (range == QStringLiteral("tv")) {
        return MediaSession::tr("limited");
    }
    if (range == QStringLiteral("pc")) {
        return MediaSession::tr("full");
    }
    return range;
}

QString sampleRateName(int sampleRate) {
    if (sampleRate <= 0) {
        return {};
    }
    int const decimals = sampleRate % 1'000 == 0 ? 0 : 1;
    return MediaSession::tr("%1 kHz").arg(QLocale().toString(sampleRate / 1'000.0, 'f', decimals));
}

QString frameRateName(std::optional<std::int64_t> nominalFrameDurationMicroseconds) {
    if (!nominalFrameDurationMicroseconds || *nominalFrameDurationMicroseconds <= 0) {
        return {};
    }
    double const framesPerSecond = 1'000'000.0 / static_cast<double>(*nominalFrameDurationMicroseconds);
    QString rate = QLocale().toString(framesPerSecond, 'f', 3);
    QString const decimal = QLocale().decimalPoint();
    while (rate.endsWith(QLatin1Char('0'))) {
        rate.chop(1);
    }
    if (rate.endsWith(decimal)) {
        rate.chop(1);
    }
    return MediaSession::tr("%1 fps").arg(rate);
}
} // namespace

MediaSession::MediaSession(VideoTargetReadback readback, QObject* parent)
    : MediaSession(readback, decodeMedia, createAudioSink(), parent) {}

MediaSession::MediaSession(VideoTargetReadback readback, DecodeOperation decodeOperation, QObject* parent)
    : MediaSession(
          readback,
          [decodeOperation = std::move(decodeOperation)](
              FfmpegMediaDecodeRequest const& request, FfmpegVideoFrameSink const& videoSink,
              FfmpegAudioOutputSink const&, FfmpegMediaStreamSink const& streamSink, FfmpegSubtitleOutputSink const&,
              std::stop_token stopToken) {
              if (streamSink) {
                  streamSink({.audioStreamPresent = false});
              }
              FfmpegMediaDecodeResult result;
              result.video = decodeOperation(request.video, videoSink, stopToken);
              result.error = result.video.error;
              result.cancelled = result.video.cancelled;
              return result;
          },
          {}, parent) {}

MediaSession::MediaSession(VideoTargetReadback readback, MediaDecodeOperation decodeOperation,
                           std::shared_ptr<AudioSink> audioSink, QObject* parent)
    : QObject(parent), m_decodeOperation(std::move(decodeOperation)), m_audioSink(std::move(audioSink)),
      m_videoSource({}, readback) {
    Q_ASSERT(m_decodeOperation);
    applyAudioGain();
    m_videoSource.setFrameSelector(this);
    m_frameQueue.reset(m_playbackGeneration);
    connect(&m_videoSource, &DecodedVideoSource::presentationFailed, this, &MediaSession::handlePresentationFailure);
    connect(&m_videoSource, &DecodedVideoSource::frameChanged, this, &MediaSession::handleVideoFrameChanged);
    m_playbackMonitorTimer.setInterval(playbackMonitorIntervalMilliseconds);
    connect(&m_playbackMonitorTimer, &QTimer::timeout, this, &MediaSession::monitorPlayback);
    m_worker = std::jthread([this](std::stop_token stopToken) { workerLoop(stopToken); });
}

MediaSession::~MediaSession() {
    Q_ASSERT(QThread::currentThread() == thread());
    shutdownWorker();
    m_videoSource.setFrameSelector(nullptr);
}

MediaSession::State MediaSession::state() const { return m_state; }

QUrl MediaSession::mediaUrl() const { return m_mediaUrl; }

QString MediaSession::displayName() const { return m_displayName; }

QString MediaSession::errorMessage() const { return m_errorMessage; }

QString MediaSession::containerFormat() const { return m_containerFormat; }

QString MediaSession::decoderName() const { return m_decoderName; }

QString MediaSession::decodePath() const { return m_decodePath; }

QString MediaSession::hardwareFallbackReason() const { return m_hardwareFallbackReason; }

QString MediaSession::videoSummary() const { return m_videoSummary; }

QString MediaSession::selectedVideoTrackSummary() const {
    EmbeddedMediaStreamDescriptor const* const track = m_videoTracks.track(m_selectedVideoStreamIndex);
    return track ? track->label : QString{};
}

QString MediaSession::videoDynamicRange() const {
    std::shared_ptr<DecodedVideoFrame const> const frame = m_videoSource.currentFrame();
    return dynamicRangeName(frame ? frame->dynamicRange() : VideoDynamicRange::Unknown);
}

bool MediaSession::videoHdr() const {
    std::shared_ptr<DecodedVideoFrame const> const frame = m_videoSource.currentFrame();
    return frame && isHdr(frame->dynamicRange());
}

QString MediaSession::videoSignalSummary() const {
    std::shared_ptr<DecodedVideoFrame const> const frame = m_videoSource.currentFrame();
    if (!frame) {
        return {};
    }
    VideoSignalDescription const& signal = frame->signal();
    return tr("%1 primaries · %2 transfer · %3 range")
        .arg(colorName(signal.colorPrimaries), colorName(signal.transferFunction), rangeName(signal.colorRange));
}

QString MediaSession::selectedAudioTrackSummary() const {
    EmbeddedMediaStreamDescriptor const* const track = m_audioTracks.track(m_selectedAudioStreamIndex);
    if (!track) {
        return {};
    }
    QString const sampleRate = sampleRateName(track->sampleRate);
    return sampleRate.isEmpty() ? track->label : tr("%1 · %2").arg(track->label, sampleRate);
}

QString MediaSession::selectedSubtitleTrackSummary() const {
    if (m_selectedSubtitleStreamIndex < 0) {
        return tr("Off");
    }
    EmbeddedMediaStreamDescriptor const* const track = m_subtitleTracks.track(m_selectedSubtitleStreamIndex);
    if (!track) {
        return {};
    }
    QString kind;
    switch (track->subtitleKind) {
    case SubtitleStreamKind::Text:
        kind = tr("text");
        break;
    case SubtitleStreamKind::Bitmap:
        kind = tr("bitmap");
        break;
    case SubtitleStreamKind::Unknown:
        kind = tr("unknown type");
        break;
    }
    return tr("%1 · %2 · %3").arg(track->label, track->codec, kind);
}

bool MediaSession::hasFrame() const { return static_cast<bool>(m_videoSource.currentFrame()); }

bool MediaSession::playing() const {
    return m_state == State::Ready && m_userWantsPlaying && m_playbackInterruption == PlaybackInterruption::None &&
           !m_ended;
}

bool MediaSession::playRequested() const { return m_userWantsPlaying && !m_ended; }

bool MediaSession::ended() const { return m_ended; }

bool MediaSession::seekable() const { return m_seekable; }

bool MediaSession::seeking() const { return m_seeking; }

qlonglong MediaSession::positionMilliseconds() const {
    std::int64_t const position = mediaClockSnapshotAt(std::chrono::steady_clock::now()).positionMicroseconds;
    std::int64_t const clamped = m_durationMicroseconds ? std::clamp<std::int64_t>(position, 0, *m_durationMicroseconds)
                                                        : std::max<std::int64_t>(0, position);
    return clamped / 1'000;
}

qlonglong MediaSession::durationMilliseconds() const {
    return m_durationMicroseconds ? *m_durationMicroseconds / 1'000 : -1;
}

std::uint64_t MediaSession::playbackGeneration() const { return m_playbackGeneration; }

std::uint64_t MediaSession::decodedFrameCount() const {
    std::lock_guard lock(m_playbackMetricsMutex);
    return m_playbackMetricsGeneration == m_playbackGeneration ? m_decodedFrameCount : 0;
}

std::uint64_t MediaSession::selectedFrameCount() const { return m_selectedFrameCount; }

std::uint64_t MediaSession::droppedFrameCount() const { return m_droppedFrameCount; }

std::size_t MediaSession::queuedFrameCount() const { return m_frameQueue.size(m_playbackGeneration); }

std::size_t MediaSession::maximumQueuedFrameCount() const { return m_frameQueue.maximumObservedSize(); }

int MediaSession::queuedVideoFrames() const { return static_cast<int>(queuedFrameCount()); }

qreal MediaSession::volume() const { return m_volume; }

bool MediaSession::muted() const { return m_muted; }

MediaSession::PlaybackInterruption MediaSession::playbackInterruption() const { return m_playbackInterruption; }

bool MediaSession::hasAudioOutput() const { return m_audioSinkDiagnostics.streamOpen; }

QString MediaSession::audioBackend() const { return QString::fromStdString(m_audioSinkDiagnostics.backendName); }

MediaSession::MediaClockSource MediaSession::mediaClockSource() const { return m_mediaClockSource; }

bool MediaSession::audioClockReliable() const { return m_audioSinkDiagnostics.clockReliable; }

int MediaSession::audioQueuedMilliseconds() const {
    int const sampleRate = m_audioSinkDiagnostics.format.sampleRate;
    if (sampleRate <= 0) {
        return -1;
    }
    std::uint64_t const milliseconds = static_cast<std::uint64_t>(m_audioSinkDiagnostics.queuedFrames) * 1'000ULL /
                                       static_cast<std::uint64_t>(sampleRate);
    return static_cast<int>(
        std::min<std::uint64_t>(milliseconds, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
}

qulonglong MediaSession::audioSubmittedFrames() const { return m_audioSinkDiagnostics.mediaFramesSubmitted; }

qulonglong MediaSession::audioPresentedFrames() const { return m_audioSinkDiagnostics.mediaFramesPresented; }

qulonglong MediaSession::audioUnderrunFrames() const { return m_audioSinkDiagnostics.underrunFrames; }

std::optional<AudioPresentationSnapshot> MediaSession::currentAudioPresentation() const {
    if (!currentGenerationUsesAudioClock()) {
        return std::nullopt;
    }
    AudioPresentationSnapshot snapshot = m_audioSink->snapshot();
    if (snapshot.playbackGeneration != m_playbackGeneration) {
        return std::nullopt;
    }
    return snapshot;
}

QAbstractItemModel* MediaSession::videoTracks() { return &m_videoTracks; }

QAbstractItemModel* MediaSession::audioTracks() { return &m_audioTracks; }

int MediaSession::selectedVideoStreamIndex() const { return m_selectedVideoStreamIndex; }

int MediaSession::selectedAudioStreamIndex() const { return m_selectedAudioStreamIndex; }

QAbstractItemModel* MediaSession::subtitleTracks() { return &m_subtitleTracks; }

int MediaSession::selectedSubtitleStreamIndex() const { return m_selectedSubtitleStreamIndex; }

QString MediaSession::subtitleError() const { return m_subtitleError; }

SubtitlePresentationSnapshot
MediaSession::subtitlePresentationSnapshot(std::chrono::steady_clock::time_point now) const {
    return {
        .state = m_subtitleSource.snapshot(),
        .mediaTimeMicroseconds = mediaClockSnapshotAt(now).positionMicroseconds,
    };
}

DecodedVideoSource& MediaSession::videoSource() { return m_videoSource; }

DecodedVideoSource const& MediaSession::videoSource() const { return m_videoSource; }

void MediaSession::invalidateGraphicsDevice() {
    Q_ASSERT(QThread::currentThread() == thread());
    bool const recoverySeeking = m_seeking;
    std::int64_t const recoveryPosition =
        m_state == State::Opening ? m_requestedPositionMicroseconds
                                  : mediaClockSnapshotAt(std::chrono::steady_clock::now()).positionMicroseconds;
    m_videoDecodeCapability = {
        .device = {},
        .unavailableReason = QStringLiteral("The graphics device is being recreated"),
    };
    m_activeVideoDecodeCapability = m_videoDecodeCapability;
    if (m_state != State::Opening && m_state != State::Ready) {
        return;
    }
    QString const path = m_mediaUrl.toLocalFile();
    if (path.isEmpty()) {
        return;
    }

    advanceGeneration();
    std::uint64_t const generation = m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration) {
        return;
    }
    m_state = State::Opening;
    m_errorMessage.clear();
    resetPlayback(recoveryPosition);
    m_reopenAfterGraphicsRecovery = true;
    m_graphicsRecoveryPositionMicroseconds = recoveryPosition;
    m_graphicsRecoverySeeking = recoverySeeking;
    m_seeking = recoverySeeking;
    m_hardwareImportFallbackConsumed = false;
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::setVideoDecodeCapability(VideoHardwareDecodeCapability capability) {
    Q_ASSERT(QThread::currentThread() == thread());
    m_videoDecodeCapability = std::move(capability);
    if (!m_reopenAfterGraphicsRecovery) {
        m_activeVideoDecodeCapability = m_videoDecodeCapability;
        return;
    }

    m_reopenAfterGraphicsRecovery = false;
    std::int64_t const recoveryPosition = std::exchange(m_graphicsRecoveryPositionMicroseconds, 0);
    bool const seeking = std::exchange(m_graphicsRecoverySeeking, false);
    QString const path = m_mediaUrl.toLocalFile();
    if (!path.isEmpty()) {
        restartAt(recoveryPosition, m_videoDecodeCapability, seeking);
    }
}

void MediaSession::openMedia(QUrl const& url) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!url.isValid() || !url.isLocalFile()) {
        failWithoutWorker(url, tr("Only local media files are supported right now."));
        return;
    }

    QString const path = url.toLocalFile();
    if (path.isEmpty()) {
        failWithoutWorker(url, tr("The selected media path is empty."));
        return;
    }
    m_userWantsPlaying = true;
    m_audioPlayIntent.store(true, std::memory_order_release);
    m_reopenAfterGraphicsRecovery = false;
    m_graphicsRecoveryPositionMicroseconds = 0;
    m_graphicsRecoverySeeking = false;
    m_hardwareImportFallbackConsumed = false;
    startOpen(url, path, m_videoDecodeCapability);
}

void MediaSession::cancel() {
    Q_ASSERT(QThread::currentThread() == thread());
    advanceGeneration();
    std::uint64_t const generation = m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration) {
        return;
    }
    m_reopenAfterGraphicsRecovery = false;
    m_graphicsRecoveryPositionMicroseconds = 0;
    m_graphicsRecoverySeeking = false;
    m_hardwareImportFallbackConsumed = false;
    m_userWantsPlaying = true;
    m_audioPlayIntent.store(true, std::memory_order_release);
    m_state = State::Empty;
    m_mediaUrl = QUrl{};
    m_displayName.clear();
    m_errorMessage.clear();
    resetMediaTracks();
    m_selectedSubtitleStreamIndex = -1;
    m_subtitleTracks.setTracks({}, -1);
    m_subtitleError.clear();
    resetDiagnostics();
    resetPlayback();
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::retry() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_mediaUrl.isValid()) {
        return;
    }
    openMedia(m_mediaUrl);
}

void MediaSession::play() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != State::Ready) {
        return;
    }
    if (m_ended) {
        QString const path = m_mediaUrl.toLocalFile();
        if (!path.isEmpty()) {
            m_userWantsPlaying = true;
            m_hardwareImportFallbackConsumed = false;
            restartAt(0, m_videoDecodeCapability, false);
        }
        return;
    }
    if (m_userWantsPlaying) {
        return;
    }
    m_userWantsPlaying = true;
    m_audioPlayIntent.store(true, std::memory_order_release);
    if (!m_audioTailClockActive) {
        std::lock_guard lock(m_audioSinkLifecycleMutex);
        if (m_audioSink && m_audioSinkGeneration.load(std::memory_order_acquire) == m_playbackGeneration) {
            m_audioSink->start();
            m_playbackMonitorTimer.start();
        }
    }
    auto const now = std::chrono::steady_clock::now();
    m_clockAnchorTime = now;
    std::optional<AudioPresentationSnapshot> audio;
    if (!observeAudioOutput(now, audio)) {
        return;
    }
    emit sessionChanged();
    emit timelineChanged();
    m_playbackMonitorTimer.start();
    m_videoSource.requestFrameSelection();
}

void MediaSession::pause() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != State::Ready || !m_userWantsPlaying || m_ended) {
        return;
    }
    auto const now = std::chrono::steady_clock::now();
    // Record intent before observing a fallible device boundary. A recovery
    // transition triggered by the observation must not lose a concurrent
    // user pause or resume automatically afterward.
    m_userWantsPlaying = false;
    m_audioPlayIntent.store(false, std::memory_order_release);
    std::optional<AudioPresentationSnapshot> audio;
    if (!observeAudioOutput(now, audio)) {
        return;
    }
    m_clockAnchorMediaMicroseconds = mediaClockSnapshotAt(now, audio ? &*audio : nullptr).positionMicroseconds;
    m_clockAnchorTime = now;
    m_audioHoldSince.reset();
    m_audioClockUnavailableSince.reset();
    if (!m_audioTailClockActive) {
        std::lock_guard lock(m_audioSinkLifecycleMutex);
        if (m_audioSink && m_audioSinkGeneration.load(std::memory_order_acquire) == m_playbackGeneration) {
            m_audioSink->pause();
        }
    }
    m_playbackMonitorTimer.stop();
    emit sessionChanged();
    emit timelineChanged();
    m_videoSource.requestFrameSelection();
}

void MediaSession::setVolume(qreal volume) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!std::isfinite(volume)) {
        return;
    }
    qreal const normalized = std::clamp(volume, 0.0, 1.0);
    if (m_volume == normalized) {
        return;
    }
    m_volume = normalized;
    applyAudioGain();
    emit volumeChanged();
}

void MediaSession::setMuted(bool muted) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_muted == muted) {
        return;
    }
    m_muted = muted;
    applyAudioGain();
    emit mutedChanged();
}

void MediaSession::seekToMilliseconds(qlonglong positionMilliseconds) {
    Q_ASSERT(QThread::currentThread() == thread());
    if ((!m_seekable && !m_seeking) || !m_durationMicroseconds || (m_state != State::Ready && !m_seeking)) {
        return;
    }

    std::int64_t const duration = *m_durationMicroseconds;
    std::int64_t const requested = positionMilliseconds <= 0 ? 0
                                   : positionMilliseconds >= duration / 1'000
                                       ? duration
                                       : static_cast<std::int64_t>(positionMilliseconds) * 1'000;
    if (m_reopenAfterGraphicsRecovery) {
        advanceGeneration();
        std::uint64_t const generation = m_playbackGeneration;
        cancelPipeline();
        m_frameQueue.reset(generation);
        m_videoSource.clearFrame();
        if (generation != m_playbackGeneration) {
            return;
        }
        m_state = State::Opening;
        m_errorMessage.clear();
        resetPlayback(requested);
        m_seeking = true;
        m_graphicsRecoveryPositionMicroseconds = requested;
        m_graphicsRecoverySeeking = true;
        publishSessionAndPlaybackMetrics(generation);
        return;
    }
    restartAt(requested, m_activeVideoDecodeCapability, true);
}

void MediaSession::selectSubtitleStream(int streamIndex) {
    Q_ASSERT(QThread::currentThread() == thread());
    if ((m_state != State::Opening && m_state != State::Ready) || streamIndex == m_selectedSubtitleStreamIndex ||
        !m_subtitleTracks.canSelect(streamIndex)) {
        return;
    }
    std::int64_t const position = m_state == State::Opening
                                      ? m_requestedPositionMicroseconds
                                      : mediaClockSnapshotAt(std::chrono::steady_clock::now()).positionMicroseconds;
    qCInfo(sunplayerLogPlayback).noquote() << "event=playback.subtitle_selected"
                                         << "from=" + QString::number(m_selectedSubtitleStreamIndex)
                                         << "to=" + QString::number(streamIndex)
                                         << "positionUs=" + QString::number(position);
    m_selectedSubtitleStreamIndex = streamIndex;
    m_subtitleTracks.setSelectedStreamIndex(streamIndex);
    m_subtitleError.clear();
    emit subtitleChanged();
    restartAt(position, m_activeVideoDecodeCapability, false);
}

void MediaSession::selectVideoStream(int streamIndex) {
    Q_ASSERT(QThread::currentThread() == thread());
    if ((m_state != State::Opening && m_state != State::Ready) || streamIndex == m_selectedVideoStreamIndex ||
        !m_videoTracks.canSelect(streamIndex)) {
        return;
    }
    std::int64_t position = m_state == State::Opening
                                ? m_requestedPositionMicroseconds
                                : mediaClockSnapshotAt(std::chrono::steady_clock::now()).positionMicroseconds;
    if (std::optional<std::int64_t> const end = m_videoTracks.endMicroseconds(streamIndex); end && position >= *end) {
        position = std::max<std::int64_t>(0, *end - 1);
    }
    qCInfo(sunplayerLogPlayback).noquote() << "event=playback.video_track_selected"
                                         << "from=" + QString::number(m_selectedVideoStreamIndex)
                                         << "to=" + QString::number(streamIndex)
                                         << "positionUs=" + QString::number(position);
    m_selectedVideoStreamIndex = streamIndex;
    emit mediaTracksChanged();
    restartAt(position, m_activeVideoDecodeCapability, false);
}

void MediaSession::selectAudioStream(int streamIndex) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_audioSink || (m_state != State::Opening && m_state != State::Ready) ||
        streamIndex == m_selectedAudioStreamIndex || !m_audioTracks.canSelect(streamIndex)) {
        return;
    }
    std::int64_t const position = m_state == State::Opening
                                      ? m_requestedPositionMicroseconds
                                      : mediaClockSnapshotAt(std::chrono::steady_clock::now()).positionMicroseconds;
    qCInfo(sunplayerLogPlayback).noquote() << "event=playback.audio_track_selected"
                                         << "from=" + QString::number(m_selectedAudioStreamIndex)
                                         << "to=" + QString::number(streamIndex)
                                         << "positionUs=" + QString::number(position);
    m_selectedAudioStreamIndex = streamIndex;
    emit mediaTracksChanged();
    restartAt(position, m_activeVideoDecodeCapability, false);
}

void MediaSession::startOpen(QUrl const& url, QString const& path, VideoHardwareDecodeCapability hardwareDecode) {
    startDecode(url, path, std::move(hardwareDecode), 0, true, false);
}

void MediaSession::restartAt(std::int64_t positionMicroseconds, VideoHardwareDecodeCapability hardwareDecode,
                             bool seeking) {
    QString const path = m_mediaUrl.toLocalFile();
    if (path.isEmpty()) {
        return;
    }
    startDecode(m_mediaUrl, path, std::move(hardwareDecode), positionMicroseconds, false, seeking);
}

void MediaSession::startDecode(QUrl const& url, QString const& path, VideoHardwareDecodeCapability hardwareDecode,
                               std::int64_t requestedPositionMicroseconds, bool newMedia, bool seeking) {
    Q_ASSERT(requestedPositionMicroseconds >= 0);
    advanceGeneration();
    std::uint64_t const generation = m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration) {
        return;
    }
    m_state = State::Opening;
    m_errorMessage.clear();
    if (newMedia) {
        m_mediaUrl = url;
        m_displayName = QFileInfo(path).fileName();
        resetMediaTracks();
        m_selectedSubtitleStreamIndex = -1;
        m_subtitleTracks.setTracks({}, -1);
        m_subtitleError.clear();
        resetDiagnostics();
    }
    m_activeVideoDecodeCapability = hardwareDecode;
    resetPlayback(requestedPositionMicroseconds);
    m_audioPlayIntent.store(m_userWantsPlaying, std::memory_order_release);
    m_seeking = seeking;

    qCInfo(sunplayerLogPlayback).noquote() << (newMedia  ? "event=playback.open_start"
                                             : seeking ? "event=playback.seek_start"
                                                       : "event=playback.restart_start")
                                         << "generation=" + QString::number(generation)
                                         << "positionUs=" + QString::number(requestedPositionMicroseconds);
    qCDebug(sunplayerLogPlayback).noquote() << "event=playback.decode_request"
                                          << "generation=" + QString::number(generation) << "path=" + path;

    std::int64_t decodePosition = requestedPositionMicroseconds;
    if (m_durationMicroseconds && decodePosition >= *m_durationMicroseconds) {
        decodePosition = std::max<std::int64_t>(0, *m_durationMicroseconds - 1);
    }
    bool const seekToTarget = !newMedia && m_timelineOrigin.has_value();

    VideoFrameIdentity const identity{
        .playbackGeneration = generation,
        .decoderRevision = 1,
        .frameId = 1,
    };
    submitOpen({
        .generation = generation,
        .decode =
            {
                .video =
                    {
                        .path = path,
                        .firstFrameIdentity = identity,
                        .hardwareDecode = std::move(hardwareDecode),
                        .extraHardwareFrames = static_cast<int>(VideoFrameQueue::capacity + 2),
                        .start =
                            {
                                .targetPositionMicroseconds =
                                    seekToTarget ? std::optional<std::int64_t>(decodePosition) : std::nullopt,
                                .timelineOrigin = m_timelineOrigin,
                                .performDemuxSeek = seekToTarget && (decodePosition > 0 || m_seekable),
                            },
                    },
                .decodeSelectedAudio = static_cast<bool>(m_audioSink),
                .selectedVideoStreamIndex = m_selectedVideoStreamIndex,
                .selectedAudioStreamIndex = m_selectedAudioStreamIndex,
                .selectedSubtitleStreamIndex = m_selectedSubtitleStreamIndex,
            },
    });
    if (m_userWantsPlaying) {
        m_playbackMonitorTimer.start();
    }
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
    cancelAudioOutput();
    m_workerWake.notify_one();
}

void MediaSession::cancelAudioOutput() {
    m_playbackMonitorTimer.stop();
    m_audioHoldSince.reset();
    m_audioClockUnavailableSince.reset();
    std::lock_guard lock(m_audioSinkLifecycleMutex);
    std::uint64_t const generation = m_audioSinkGeneration.exchange(0, std::memory_order_acq_rel);
    if (m_audioSink && generation != 0) {
        m_audioSink->cancel(generation);
    }
}

void MediaSession::workerLoop(std::stop_token workerStopToken) {
    while (!workerStopToken.stop_requested()) {
        OpenRequest request;
        std::stop_token operationStopToken;
        {
            std::unique_lock lock(m_workerMutex);
            m_workerWake.wait(lock, workerStopToken, [this] { return m_pendingOpen.has_value(); });
            if (workerStopToken.stop_requested()) {
                return;
            }

            request = std::move(*m_pendingOpen);
            m_pendingOpen.reset();
            m_operationStopSource = std::stop_source();
            operationStopToken = m_operationStopSource.get_token();
        }

        VideoFrameTimeline timeline(request.decode.video.start.timelineOrigin);
        VideoSeekPrerollGate preroll(request.decode.video.start.targetPositionMicroseconds);
        auto const operationStarted = std::chrono::steady_clock::now();
        std::uint64_t seekPrerollFrames = 0;
        std::optional<std::int64_t> firstSeekTimelineMicroseconds;
        bool seekAdmissionLogged = false;
        bool audioOutputInitialized = false;
        FfmpegMediaDecodeResult result = m_decodeOperation(
            request.decode,
            [this, generation = request.generation,
             targetPosition = request.decode.video.start.targetPositionMicroseconds, &timeline, &preroll,
             &seekPrerollFrames, &firstSeekTimelineMicroseconds, &seekAdmissionLogged, operationStarted,
             operationStopToken](std::shared_ptr<DecodedVideoFrame const> frame,
                                 FfmpegVideoStreamDiagnostics const& diagnostics) {
                if (!recordDecodedFrame(generation)) {
                    return false;
                }
                postPlaybackMetricsChanged(generation);
                QueuedVideoFrame queued = timeline.schedule(std::move(frame), diagnostics);
                if (targetPosition && !seekAdmissionLogged) {
                    ++seekPrerollFrames;
                    if (!firstSeekTimelineMicroseconds) {
                        firstSeekTimelineMicroseconds = queued.timelineTimeMicroseconds;
                        qCDebug(sunplayerLogPlayback).noquote()
                            << "event=playback.seek_first_frame"
                            << "generation=" + QString::number(generation)
                            << "targetUs=" + QString::number(*targetPosition)
                            << "timelineUs=" + QString::number(queued.timelineTimeMicroseconds)
                            << "rawPts=" + (queued.frame->timing().pts ? QString::number(*queued.frame->timing().pts)
                                                                       : QStringLiteral("none"));
                    }
                }
                VideoSeekPrerollAdmission admitted = preroll.admit(std::move(queued));
                if (targetPosition && !seekAdmissionLogged && (admitted.first || admitted.second)) {
                    seekAdmissionLogged = true;
                    QueuedVideoFrame const& firstAdmitted = admitted.first ? *admitted.first : *admitted.second;
                    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - operationStarted)
                                             .count();
                    qCInfo(sunplayerLogPlayback).noquote()
                        << "event=playback.seek_preroll_complete"
                        << "generation=" + QString::number(generation) << "targetUs=" + QString::number(*targetPosition)
                        << "firstTimelineUs=" + QString::number(firstSeekTimelineMicroseconds.value_or(0))
                        << "admittedTimelineUs=" + QString::number(firstAdmitted.timelineTimeMicroseconds)
                        << "decodedFrames=" + QString::number(seekPrerollFrames)
                        << "elapsedMs=" + QString::number(elapsed);
                } else if (targetPosition && !seekAdmissionLogged && seekPrerollFrames % 64 == 0) {
                    qCDebug(sunplayerLogPlayback).noquote()
                        << "event=playback.seek_preroll_progress"
                        << "generation=" + QString::number(generation) << "targetUs=" + QString::number(*targetPosition)
                        << "decodedFrames=" + QString::number(seekPrerollFrames);
                }
                auto const publish = [this, generation, operationStopToken](std::optional<QueuedVideoFrame>& ready) {
                    if (!ready) {
                        return true;
                    }
                    if (!m_frameQueue.push(generation, std::move(*ready), operationStopToken)) {
                        return false;
                    }
                    postFramesAvailable(generation);
                    return true;
                };
                return publish(admitted.first) && publish(admitted.second);
            },
            FfmpegAudioOutputSink{
                .submit =
                    [this, generation = request.generation, &audioOutputInitialized](
                        PcmAudioBlock block, FfmpegAudioStreamDiagnostics const&, std::stop_token stopToken) {
                        if (!m_audioSink || block.playbackGeneration != generation || stopToken.stop_requested()) {
                            return false;
                        }
                        if (!audioOutputInitialized) {
                            std::lock_guard lock(m_audioSinkLifecycleMutex);
                            if (stopToken.stop_requested()) {
                                return false;
                            }
                            m_audioSink->reset(generation, block.format);
                            m_audioSinkGeneration.store(generation, std::memory_order_release);
                            audioOutputInitialized = true;
                            qCInfo(sunplayerLogPlayback).noquote()
                                << "event=playback.audio_output_ready"
                                << "generation=" + QString::number(generation)
                                << "sampleRate=" + QString::number(block.format.sampleRate)
                                << "channels=" + QString::number(block.format.channelCount);
                            if (m_audioPlayIntent.load(std::memory_order_acquire)) {
                                m_audioSink->start();
                            } else if (!m_audioPlayIntent.load(std::memory_order_acquire)) {
                                m_audioSink->pause();
                            }
                        }
                        bool const accepted = m_audioSink->submit(std::move(block), stopToken);
                        if (accepted) {
                            postFramesAvailable(generation);
                        }
                        return accepted;
                    },
                .endOfStream =
                    [this](std::uint64_t generation) {
                        bool outputEpochExists = false;
                        {
                            std::lock_guard lock(m_audioSinkLifecycleMutex);
                            outputEpochExists =
                                m_audioSink && m_audioSinkGeneration.load(std::memory_order_acquire) == generation;
                            if (outputEpochExists) {
                                m_audioSink->finish(generation);
                            }
                        }
                        if (!outputEpochExists) {
                            std::lock_guard lock(m_streamDiscoveryMutex);
                            if (m_streamDiscoveryGeneration == generation) {
                                m_audioOutputEndedWithoutFrames = true;
                            }
                        }
                        qCInfo(sunplayerLogPlayback).noquote() << "event=playback.audio_decode_drained"
                                                             << "generation=" + QString::number(generation);
                        postFramesAvailable(generation);
                    },
            },
            [this, generation = request.generation](FfmpegMediaStreamSelection const& selection) {
                if (selection.subtitleConfiguration) {
                    m_subtitleSource.configure(*selection.subtitleConfiguration);
                }
                {
                    std::lock_guard lock(m_streamDiscoveryMutex);
                    if (m_streamDiscoveryGeneration == generation) {
                        m_audioOutputExpected = selection.audioOutputExpected;
                        m_audioOutputEndedWithoutFrames = false;
                        m_initialVideoDiagnostics = selection.videoDiagnostics;
                    }
                }
                qCInfo(sunplayerLogPlayback).noquote()
                    << "event=playback.streams_selected"
                    << "generation=" + QString::number(generation)
                    << "audio=" +
                           QString(selection.audioStreamPresent ? QStringLiteral("true") : QStringLiteral("false"))
                    << "audioOutputExpected=" +
                           QString(selection.audioOutputExpected ? QStringLiteral("true") : QStringLiteral("false"))
                    << "videoTracks=" + QString::number(selection.videoTracks.size())
                    << "audioTracks=" + QString::number(selection.audioTracks.size())
                    << "selectedVideo=" + QString::number(selection.selectedVideoStreamIndex)
                    << "selectedAudio=" + QString::number(selection.selectedAudioStreamIndex)
                    << "subtitleTracks=" + QString::number(selection.subtitleTracks.size())
                    << "selectedSubtitle=" + QString::number(selection.subtitleConfiguration
                                                                 ? selection.subtitleConfiguration->streamIndex
                                                                 : -1)
                    << "subtitleCodec=" + QString(selection.subtitleConfiguration
                                                      ? selection.subtitleConfiguration->codec
                                                      : QStringLiteral("none"));
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, configured = selection.subtitleConfiguration.has_value(),
                     selectedVideo = selection.selectedVideoStreamIndex,
                     selectedAudio = selection.selectedAudioStreamIndex, videoTracks = selection.videoTracks,
                     audioTracks = selection.audioTracks, subtitleTracks = selection.subtitleTracks]() mutable {
                        if (generation != m_playbackGeneration) {
                            return;
                        }
                        m_selectedVideoStreamIndex = selectedVideo;
                        m_videoTracks.setTracks(std::move(videoTracks));
                        if (m_audioSink) {
                            m_selectedAudioStreamIndex = selectedAudio;
                            m_audioTracks.setTracks(std::move(audioTracks));
                        } else {
                            m_selectedAudioStreamIndex = -1;
                            m_audioTracks.setTracks({});
                        }
                        if (configured) {
                            m_subtitleError.clear();
                        }
                        m_subtitleTracks.setTracks(std::move(subtitleTracks), m_selectedSubtitleStreamIndex);
                        emit mediaTracksChanged();
                        emit subtitleChanged();
                        {
                            std::lock_guard lock(m_streamDiscoveryMutex);
                            if (m_streamDiscoveryGeneration != generation) {
                                return;
                            }
                            m_streamDiscoveryComplete = true;
                        }
                        postFramesAvailable(generation);
                    },
                    Qt::QueuedConnection);
            },
            FfmpegSubtitleOutputSink{
                .submit =
                    [this, generation = request.generation](SubtitleEvent event, std::stop_token stopToken) {
                        if (stopToken.stop_requested() || event.playbackGeneration != generation) {
                            return false;
                        }
                        bool const accepted = m_subtitleSource.append(std::move(event));
                        if (accepted) {
                            postFramesAvailable(generation);
                        }
                        return accepted;
                    },
                .failed =
                    [this, generation = request.generation](QString error) {
                        m_subtitleSource.fail(generation, error);
                        QMetaObject::invokeMethod(
                            this,
                            [this, generation, error = std::move(error)]() mutable {
                                if (generation != m_playbackGeneration) {
                                    return;
                                }
                                SubtitleStateSnapshot const state = m_subtitleSource.snapshot();
                                m_subtitleError = state.error.isEmpty() ? std::move(error) : state.error;
                                emit subtitleChanged();
                            },
                            Qt::QueuedConnection);
                    },
            },
            operationStopToken);
        auto const operationElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - operationStarted)
                .count();
        if (result.isSuccess()) {
            std::optional<QueuedVideoFrame> endFallback = preroll.finish();
            if (endFallback) {
                std::int64_t const fallbackTimeline = endFallback->timelineTimeMicroseconds;
                if (m_frameQueue.push(request.generation, std::move(*endFallback), operationStopToken)) {
                    postFramesAvailable(request.generation);
                    if (request.decode.video.start.targetPositionMicroseconds && !seekAdmissionLogged) {
                        seekAdmissionLogged = true;
                        qCInfo(sunplayerLogPlayback).noquote()
                            << "event=playback.seek_preroll_fallback_complete"
                            << "generation=" + QString::number(request.generation)
                            << "targetUs=" + QString::number(*request.decode.video.start.targetPositionMicroseconds)
                            << "fallbackTimelineUs=" + QString::number(fallbackTimeline)
                            << "decodedFrames=" + QString::number(seekPrerollFrames)
                            << "elapsedMs=" + QString::number(operationElapsed);
                    }
                }
            }
        }
        if (request.decode.video.start.targetPositionMicroseconds && !seekAdmissionLogged && result.isSuccess() &&
            !operationStopToken.stop_requested()) {
            qCWarning(sunplayerLogPlayback).noquote()
                << "event=playback.seek_no_admitted_frame"
                << "generation=" + QString::number(request.generation)
                << "targetUs=" + QString::number(*request.decode.video.start.targetPositionMicroseconds)
                << "firstTimelineUs=" + (firstSeekTimelineMicroseconds ? QString::number(*firstSeekTimelineMicroseconds)
                                                                       : QStringLiteral("none"))
                << "decodedFrames=" + QString::number(seekPrerollFrames)
                << "elapsedMs=" + QString::number(operationElapsed);
        }
        if (result.isCancelled()) {
            qCDebug(sunplayerLogPlayback).noquote() << "event=playback.decode_cancelled"
                                                  << "generation=" + QString::number(request.generation)
                                                  << "frames=" + QString::number(result.video.framesDecoded)
                                                  << "audioFrames=" + QString::number(result.outputAudioFrames)
                                                  << "elapsedMs=" + QString::number(operationElapsed);
        } else if (result.isStopped()) {
            qCDebug(sunplayerLogPlayback).noquote() << "event=playback.decode_stopped"
                                                  << "generation=" + QString::number(request.generation)
                                                  << "frames=" + QString::number(result.video.framesDecoded)
                                                  << "audioFrames=" + QString::number(result.outputAudioFrames)
                                                  << "elapsedMs=" + QString::number(operationElapsed);
        } else if (result.isSuccess()) {
            qCInfo(sunplayerLogPlayback).noquote()
                << "event=playback.decode_complete"
                << "generation=" + QString::number(request.generation)
                << "frames=" + QString::number(result.video.framesDecoded)
                << "elapsedMs=" + QString::number(operationElapsed)
                << "endOfStream=" + QString(result.video.endOfStream ? QStringLiteral("true") : QStringLiteral("false"))
                << "stopped=" + QString(result.video.stopped ? QStringLiteral("true") : QStringLiteral("false"));
        } else {
            qCWarning(sunplayerLogPlayback).noquote()
                << "event=playback.decode_failed"
                << "generation=" + QString::number(request.generation)
                << "frames=" + QString::number(result.video.framesDecoded)
                << "elapsedMs=" + QString::number(operationElapsed) << "error=" + result.error;
        }
        if (workerStopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, generation = request.generation, result = std::move(result)]() mutable {
                completeDecode(generation, std::move(result));
            },
            Qt::QueuedConnection);
    }
}

void MediaSession::completeDecode(std::uint64_t generation, FfmpegMediaDecodeResult result) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation != m_playbackGeneration || (m_state != State::Opening && m_state != State::Ready)) {
        return;
    }
    if (result.isCancelled()) {
        return;
    }
    if (!result.subtitleError.isEmpty()) {
        m_subtitleSource.fail(generation, result.subtitleError);
        SubtitleStateSnapshot const state = m_subtitleSource.snapshot();
        m_subtitleError = state.error.isEmpty() ? result.subtitleError : state.error;
        emit subtitleChanged();
    }
    if (!result.isSuccess()) {
        std::string audioFailure;
        if (m_audioSink) {
            audioFailure = m_audioSink->failureReason();
        }
        cancelAudioOutput();
        m_frameQueue.reset(m_playbackGeneration);
        m_videoSource.clearFrame();
        if (generation != m_playbackGeneration) {
            return;
        }
        m_state = State::Error;
        QString failure = result.error;
        if (failure.isEmpty() && !audioFailure.empty()) {
            failure = QString::fromStdString(audioFailure);
        }
        m_errorMessage = failure.isEmpty() ? tr("The selected media could not be decoded or played.") : failure;
        resetPlayback();
        publishSessionAndPlaybackMetrics(generation);
        return;
    }

    applyDiagnostics(result.video.diagnostics);
    m_decoderDrained = result.video.endOfStream;
    handleFramesAvailable(generation);
}

void MediaSession::postFramesAvailable(std::uint64_t generation) {
    if (!m_frameWake.request(generation)) {
        return;
    }
    QMetaObject::invokeMethod(
        this, [this] { m_frameWake.drain([this](std::uint64_t generation) { handleFramesAvailable(generation); }); },
        Qt::QueuedConnection);
}

bool MediaSession::recordDecodedFrame(std::uint64_t generation) {
    std::lock_guard lock(m_playbackMetricsMutex);
    if (generation != m_playbackMetricsGeneration) {
        return false;
    }
    ++m_decodedFrameCount;
    return true;
}

void MediaSession::postPlaybackMetricsChanged(std::uint64_t generation) {
    if (!m_metricsWake.request(generation)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_metricsWake.drain([this](std::uint64_t generation) {
                if (generation == m_playbackGeneration) {
                    emit playbackMetricsChanged();
                }
            });
        },
        Qt::QueuedConnection);
}

void MediaSession::handleFramesAvailable(std::uint64_t generation) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation != m_playbackGeneration || (m_state != State::Opening && m_state != State::Ready)) {
        return;
    }
    if (m_state == State::Opening) {
        std::shared_ptr<DecodedVideoFrame const> selected =
            selectFrameForPresentation(std::chrono::steady_clock::now());
        if (selected) {
            m_videoSource.setFrame(std::move(selected));
        }
    } else {
        m_videoSource.requestFrameSelection();
    }
    if (generation != m_playbackGeneration) {
        return;
    }
    emit playbackMetricsChanged();
}

void MediaSession::handleVideoFrameChanged() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pendingPublicationGeneration != m_playbackGeneration) {
        return;
    }

    std::uint64_t const generation = m_pendingPublicationGeneration;
    bool const notifySession = std::exchange(m_sessionNotificationPending, false);
    bool const notifyMetrics = std::exchange(m_playbackMetricsNotificationPending, false);
    m_pendingPublicationGeneration = 0;

    if (notifySession) {
        emit sessionChanged();
    }
    if (generation == m_playbackGeneration) {
        emit timelineChanged();
    }
    if (generation == m_playbackGeneration && notifyMetrics) {
        emit playbackMetricsChanged();
    }
}

void MediaSession::failWithoutWorker(QUrl const& url, QString const& message) {
    advanceGeneration();
    std::uint64_t const generation = m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration) {
        return;
    }
    m_reopenAfterGraphicsRecovery = false;
    m_graphicsRecoverySeeking = false;
    m_hardwareImportFallbackConsumed = false;
    m_state = State::Error;
    m_mediaUrl = url;
    m_displayName = url.fileName();
    m_errorMessage = message;
    resetMediaTracks();
    m_selectedSubtitleStreamIndex = -1;
    m_subtitleTracks.setTracks({}, -1);
    m_subtitleError.clear();
    resetDiagnostics();
    resetPlayback();
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::handlePresentationFailure(VideoFailure const& failure) {
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(failure.isValid());
    if (m_state != State::Ready) {
        return;
    }
    if (failure.kind == VideoFailureKind::HardwareFrameImportUnavailable && !m_hardwareImportFallbackConsumed) {
        QString const path = m_mediaUrl.toLocalFile();
        if (!path.isEmpty()) {
            std::int64_t const restartPosition =
                mediaClockSnapshotAt(std::chrono::steady_clock::now()).positionMicroseconds;
            m_hardwareImportFallbackConsumed = true;
            restartAt(restartPosition,
                      {
                          .device = {},
                          .unavailableReason = tr("Hardware frame import failed; "
                                                  "using software decode: %1")
                                                   .arg(failure.reason),
                      },
                      false);
            return;
        }
    }

    advanceGeneration();
    std::uint64_t const generation = m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(m_playbackGeneration);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration) {
        return;
    }
    m_state = State::Error;
    m_errorMessage = failure.reason;
    resetPlayback();
    publishSessionAndPlaybackMetrics(generation);
}

void MediaSession::shutdownWorker() {
    if (!m_worker.joinable()) {
        return;
    }
    m_worker.request_stop();
    {
        std::lock_guard lock(m_workerMutex);
        m_operationStopSource.request_stop();
        m_pendingOpen.reset();
    }
    cancelAudioOutput();
    m_frameQueue.reset(0);
    m_workerWake.notify_all();
    m_worker.join();
}

void MediaSession::advanceGeneration() {
    ++m_playbackGeneration;
    if (m_playbackGeneration == 0) {
        ++m_playbackGeneration;
    }
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

void MediaSession::resetMediaTracks() {
    m_selectedVideoStreamIndex = -1;
    m_selectedAudioStreamIndex = -1;
    m_videoTracks.setTracks({});
    m_audioTracks.setTracks({});
    emit mediaTracksChanged();
}

void MediaSession::resetPlayback(std::int64_t positionMicroseconds) {
    Q_ASSERT(positionMicroseconds >= 0);
    m_subtitleSource.reset(m_playbackGeneration);
    emit subtitleChanged();
    {
        std::lock_guard lock(m_playbackMetricsMutex);
        m_playbackMetricsGeneration = m_playbackGeneration;
        m_decodedFrameCount = 0;
    }
    m_decoderDrained = false;
    {
        std::lock_guard lock(m_streamDiscoveryMutex);
        m_streamDiscoveryGeneration = m_playbackGeneration;
        m_streamDiscoveryComplete = false;
        m_audioOutputExpected = false;
        m_audioOutputEndedWithoutFrames = false;
        m_initialVideoDiagnostics.reset();
    }
    m_audioClockUnavailableSince.reset();
    m_playbackMonitorTimer.stop();
    m_ended = false;
    m_seeking = false;
    m_clockAnchorTime.reset();
    m_clockAnchorMediaMicroseconds = positionMicroseconds;
    m_audioClockEstablished = false;
    m_audioTailClockActive = false;
    resetAudioDiagnostics();
    m_requestedPositionMicroseconds = positionMicroseconds;
    m_frameScheduler.reset();
    m_selectedFrameCount = 0;
    m_droppedFrameCount = 0;
    m_pendingPublicationGeneration = 0;
    m_sessionNotificationPending = false;
    m_playbackMetricsNotificationPending = false;
}

void MediaSession::applyAudioGain() {
    if (m_audioSink) {
        m_audioSink->setGain(m_muted ? 0.0F : static_cast<float>(m_volume));
    }
}

void MediaSession::resetAudioDiagnostics() {
    m_audioSinkDiagnostics = {};
    m_audioOutputEpoch = 0;
    m_mediaClockSource = MediaClockSource::Monotonic;
    m_playbackInterruption = PlaybackInterruption::None;
    m_audioHoldSince.reset();
    m_audioClockUnavailableSince.reset();
}

void MediaSession::setPlaybackInterruption(PlaybackInterruption interruption) {
    if (m_playbackInterruption == interruption) {
        return;
    }
    m_playbackInterruption = interruption;

    QString state;
    switch (interruption) {
    case PlaybackInterruption::None:
        state = QStringLiteral("none");
        break;
    case PlaybackInterruption::Buffering:
        state = QStringLiteral("buffering");
        break;
    }
    qCInfo(sunplayerLogPlayback).noquote() << "event=playback.interruption_changed"
                                         << "generation=" + QString::number(m_playbackGeneration) << "state=" + state;
    emit sessionChanged();
    emit audioDiagnosticsChanged();
    m_videoSource.requestFrameSelection();
}

void MediaSession::updateAudioClockDiagnostics(AudioPresentationSnapshot const& presentation) {
    if (presentation.playbackGeneration != m_playbackGeneration) {
        return;
    }
    MediaClockSource clockSource;
    if (m_audioTailClockActive) {
        clockSource = MediaClockSource::PostAudioMonotonic;
    } else if (!m_audioClockEstablished) {
        clockSource = MediaClockSource::ProvisionalMonotonic;
    } else if (presentation.holding || m_playbackInterruption != PlaybackInterruption::None) {
        clockSource = MediaClockSource::FrozenAudio;
    } else if (presentation.valid || (presentation.drained && presentation.terminalPositionValid)) {
        clockSource = MediaClockSource::PresentedAudio;
    } else {
        clockSource = MediaClockSource::FrozenAudio;
    }
    if (m_mediaClockSource == clockSource) {
        return;
    }
    m_mediaClockSource = clockSource;
    emit audioDiagnosticsChanged();
}

void MediaSession::sampleAudioSinkDiagnostics() {
    if (!currentGenerationUsesAudioClock()) {
        return;
    }
    AudioSinkDiagnostics sink = m_audioSink->diagnostics();
    if (m_audioSinkDiagnostics == sink) {
        return;
    }
    m_audioSinkDiagnostics = std::move(sink);
    emit audioDiagnosticsChanged();
}

void MediaSession::publishSessionAndPlaybackMetrics(std::uint64_t generation) {
    if (generation != m_playbackGeneration) {
        return;
    }
    emit sessionChanged();
    emit timelineChanged();
    emit audioDiagnosticsChanged();
    if (generation == m_playbackGeneration) {
        emit playbackMetricsChanged();
    }
}

void MediaSession::applyDiagnostics(FfmpegVideoStreamDiagnostics const& diagnostics) {
    m_containerFormat = diagnostics.containerFormat;
    m_decoderName = diagnostics.decoderName;
    m_decodePath = diagnostics.decodePath;
    m_hardwareFallbackReason = diagnostics.hardwareFallbackReason;
    m_durationMicroseconds = diagnostics.durationMicroseconds;
    m_timelineOrigin = diagnostics.timelineOrigin;
    m_seekable = diagnostics.seekable && m_durationMicroseconds && m_timelineOrigin;
}

bool MediaSession::observeAudioOutput(std::chrono::steady_clock::time_point now,
                                      std::optional<AudioPresentationSnapshot>& observation) {
    observation.reset();
    if (!currentGenerationUsesAudioClock()) {
        return true;
    }
    observation = m_audioSink->snapshot();
    bool const current = updateAudioOutputState(now, *observation);
    if (current) {
        updateAudioClockDiagnostics(*observation);
    }
    return current;
}

bool MediaSession::updateAudioOutputState(std::chrono::steady_clock::time_point now,
                                          AudioPresentationSnapshot const& snapshot) {
    if (snapshot.playbackGeneration != m_playbackGeneration) {
        return true;
    }
    if (snapshot.audioOutputEpoch != 0) {
        if (m_audioOutputEpoch == 0) {
            m_audioOutputEpoch = snapshot.audioOutputEpoch;
        } else if (snapshot.audioOutputEpoch != m_audioOutputEpoch) {
            failCurrentAudioOutput(snapshot, tr("The audio output clock changed without being re-anchored."));
            return false;
        }
    }
    if (snapshot.failed) {
        failCurrentAudioOutput(snapshot);
        return false;
    }

    bool const positionAvailable = snapshot.valid || (snapshot.drained && snapshot.terminalPositionValid);
    if (positionAvailable && !m_audioTailClockActive) {
        m_audioClockEstablished = true;
        m_clockAnchorMediaMicroseconds = snapshot.mediaPositionMicroseconds;
        m_clockAnchorTime = now;
    }

    bool const activePlayback = m_state == State::Ready && m_userWantsPlaying && !m_ended;
    bool const holdingMedia = activePlayback && m_audioClockEstablished && snapshot.holding && !snapshot.drained;
    if (holdingMedia) {
        if (!m_audioHoldSince) {
            m_audioHoldSince = now;
        } else if (now - *m_audioHoldSince >= sustainedAudioHoldGrace) {
            setPlaybackInterruption(PlaybackInterruption::Buffering);
        }
    } else {
        m_audioHoldSince.reset();
        if (activePlayback && positionAvailable && m_playbackInterruption == PlaybackInterruption::Buffering) {
            setPlaybackInterruption(PlaybackInterruption::None);
        }
    }

    if (positionAvailable) {
        m_audioClockUnavailableSince.reset();
    } else if (!activePlayback || m_state == State::Opening || !m_audioClockEstablished) {
        m_audioClockUnavailableSince.reset();
    } else if (!m_audioClockUnavailableSince) {
        m_audioClockUnavailableSince = now;
    } else if (now - *m_audioClockUnavailableSince >= audioClockUnavailableGrace) {
        failCurrentAudioOutput(snapshot, tr("The audio presentation clock became unavailable."));
        return false;
    }

    if (snapshot.drained && snapshot.terminalPositionValid && !m_audioTailClockActive) {
        m_clockAnchorMediaMicroseconds = snapshot.mediaPositionMicroseconds;
        m_clockAnchorTime = now;
        m_audioTailClockActive = true;
        m_audioHoldSince.reset();
        m_audioClockUnavailableSince.reset();
        setPlaybackInterruption(PlaybackInterruption::None);
        qCInfo(sunplayerLogPlayback).noquote() << "event=playback.audio_tail_clock_started"
                                             << "generation=" + QString::number(m_playbackGeneration)
                                             << "positionUs=" + QString::number(m_clockAnchorMediaMicroseconds);
    }
    return true;
}

bool MediaSession::failCurrentAudioOutput(AudioPresentationSnapshot const& snapshot, QString const& fallbackReason) {
    if ((!snapshot.failed && fallbackReason.isEmpty()) || snapshot.playbackGeneration != m_playbackGeneration) {
        return false;
    }

    QString reason;
    if (m_audioSink) {
        reason = QString::fromStdString(m_audioSink->failureReason());
    }
    if (reason.isEmpty()) {
        reason = fallbackReason.isEmpty() ? tr("The audio output device failed.") : fallbackReason;
    }
    qCWarning(sunplayerLogPlayback).noquote()
        << "event=playback.audio_output_failed"
        << "generation=" + QString::number(m_playbackGeneration) << "error=" + reason;

    std::uint64_t const generation = m_playbackGeneration;
    cancelPipeline();
    m_frameQueue.reset(generation);
    m_videoSource.clearFrame();
    if (generation != m_playbackGeneration) {
        return true;
    }
    m_state = State::Error;
    m_errorMessage = reason;
    m_userWantsPlaying = false;
    m_audioPlayIntent.store(false, std::memory_order_release);
    resetPlayback();
    publishSessionAndPlaybackMetrics(generation);
    return true;
}

MediaClockSnapshot MediaSession::mediaClockSnapshotAt(std::chrono::steady_clock::time_point now,
                                                      AudioPresentationSnapshot const* audio) const {
    if (!m_audioTailClockActive && currentGenerationUsesAudioClock()) {
        AudioPresentationSnapshot const sampled = audio ? *audio : m_audioSink->snapshot();
        if (sampled.playbackGeneration == m_playbackGeneration && sampled.valid) {
            return {
                .positionMicroseconds = sampled.mediaPositionMicroseconds,
                .advancing = playing() && sampled.advancing,
                .terminal = sampled.drained,
            };
        }
        if (sampled.playbackGeneration == m_playbackGeneration && sampled.drained && sampled.terminalPositionValid) {
            return {
                .positionMicroseconds = sampled.mediaPositionMicroseconds,
                .advancing = false,
                .terminal = true,
            };
        }
        if (m_audioClockEstablished) {
            return {
                .positionMicroseconds = m_clockAnchorMediaMicroseconds,
                .advancing = false,
            };
        }
        // Opening a sink epoch does not establish its presentation clock.
        // Keep the provisional clock until the backend reports one; after
        // establishment, a missing observation freezes at the last anchor.
    }
    if (!m_clockAnchorTime || !playing()) {
        return {
            .positionMicroseconds = m_clockAnchorMediaMicroseconds,
            .advancing = false,
        };
    }
    auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - *m_clockAnchorTime).count();
    return {
        .positionMicroseconds = m_clockAnchorMediaMicroseconds + std::max<std::int64_t>(0, elapsed),
        .advancing = true,
    };
}

bool MediaSession::currentGenerationUsesAudioClock() const {
    return m_audioSink && m_audioSinkGeneration.load(std::memory_order_acquire) == m_playbackGeneration;
}

bool MediaSession::needsPlaybackMonitoring() const {
    return (m_state == State::Opening || m_state == State::Ready) && m_userWantsPlaying && !m_ended;
}

bool MediaSession::currentGenerationStreamsDiscovered() const {
    std::lock_guard lock(m_streamDiscoveryMutex);
    return m_streamDiscoveryGeneration == m_playbackGeneration && m_streamDiscoveryComplete;
}

std::optional<FfmpegVideoStreamDiagnostics> MediaSession::currentGenerationInitialVideoDiagnostics() const {
    std::lock_guard lock(m_streamDiscoveryMutex);
    if (m_streamDiscoveryGeneration != m_playbackGeneration || !m_streamDiscoveryComplete ||
        !m_initialVideoDiagnostics || !m_initialVideoDiagnostics->isValid()) {
        return std::nullopt;
    }
    return m_initialVideoDiagnostics;
}

bool MediaSession::enterReady(std::chrono::steady_clock::time_point now) {
    if (m_state != State::Opening || !currentGenerationStreamsDiscovered()) {
        return false;
    }

    std::optional<FfmpegVideoStreamDiagnostics> diagnostics = currentGenerationInitialVideoDiagnostics();
    if (!diagnostics) {
        std::optional<QueuedVideoFrame> const first = m_frameQueue.front(m_playbackGeneration);
        if (first && first->diagnostics.isValid()) {
            diagnostics = first->diagnostics;
        }
    }
    if (!diagnostics) {
        return false;
    }

    applyDiagnostics(*diagnostics);
    m_clockAnchorMediaMicroseconds = m_requestedPositionMicroseconds;
    m_clockAnchorTime = now;
    m_state = State::Ready;
    m_seeking = false;
    if (m_durationMicroseconds && m_requestedPositionMicroseconds >= *m_durationMicroseconds) {
        m_clockAnchorMediaMicroseconds = *m_durationMicroseconds;
        m_ended = true;
        m_userWantsPlaying = false;
        m_audioPlayIntent.store(false, std::memory_order_release);
    }
    if (needsPlaybackMonitoring()) {
        m_playbackMonitorTimer.start();
    }
    return true;
}

bool MediaSession::updateVideoSummary(QueuedVideoFrame const& frame) {
    // Per-frame diagnostics may carry the provisional open-time duration.
    // Decode-path details can change after hardware negotiation, but the
    // finalized session timeline belongs to completeDecode()/enterReady().
    bool const diagnosticsChanged = m_containerFormat != frame.diagnostics.containerFormat ||
                                    m_decoderName != frame.diagnostics.decoderName ||
                                    m_decodePath != frame.diagnostics.decodePath ||
                                    m_hardwareFallbackReason != frame.diagnostics.hardwareFallbackReason;
    VideoFrameGeometry const& geometry = frame.frame->geometry();
    VideoSignalDescription const& signal = frame.frame->signal();
    QStringList summaryParts{
        tr("%1×%2").arg(geometry.visibleSize.width()).arg(geometry.visibleSize.height()),
    };
    QString const frameRate = frameRateName(frame.diagnostics.nominalFrameDurationMicroseconds);
    if (!frameRate.isEmpty()) {
        summaryParts.push_back(frameRate);
    }
    summaryParts.push_back(signal.pixelFormat);
    summaryParts.push_back(tr("%1-bit").arg(signal.componentDepth));
    QString const summary = summaryParts.join(QStringLiteral(" · "));
    std::shared_ptr<DecodedVideoFrame const> const current = m_videoSource.currentFrame();
    bool const changed = diagnosticsChanged || summary != m_videoSummary || !current || current->signal() != signal ||
                         current->dynamicRange() != frame.frame->dynamicRange();
    m_containerFormat = frame.diagnostics.containerFormat;
    m_decoderName = frame.diagnostics.decoderName;
    m_decodePath = frame.diagnostics.decodePath;
    m_hardwareFallbackReason = frame.diagnostics.hardwareFallbackReason;
    m_videoSummary = summary;
    return changed;
}

void MediaSession::monitorPlayback() {
    if (m_state != State::Opening && m_state != State::Ready) {
        m_playbackMonitorTimer.stop();
        return;
    }

    m_videoSource.prepareForPresentation(std::chrono::steady_clock::now());
    sampleAudioSinkDiagnostics();
    if (m_state != State::Ready) {
        return;
    }

    emit timelineChanged();
    if (!needsPlaybackMonitoring()) {
        m_playbackMonitorTimer.stop();
    }
}

std::shared_ptr<DecodedVideoFrame const>
MediaSession::selectFrameForPresentation(std::chrono::steady_clock::time_point now) {
    Q_ASSERT(QThread::currentThread() == thread());
    std::optional<AudioPresentationSnapshot> audio;
    if (!observeAudioOutput(now, audio)) {
        return {};
    }
    bool enteredReady = false;
    if (m_state == State::Opening) {
        enteredReady = enterReady(now);
        if (!enteredReady) {
            return {};
        }
    }

    MediaClockSnapshot const clock = mediaClockSnapshotAt(now, audio ? &*audio : nullptr);
    bool const playbackInputDrained =
        m_decoderDrained && (!currentGenerationUsesAudioClock() || m_audioTailClockActive);
    VideoFrameSelection selection = m_frameScheduler.selectForPresentation(
        m_frameQueue, m_playbackGeneration, clock, playbackInputDrained, m_durationMicroseconds);
    bool const firstPublishedFrame = selection.frame && !m_videoSource.currentFrame();
    bool videoDetailsChanged = false;
    if (selection.frame) {
        videoDetailsChanged = updateVideoSummary(*selection.frame);
        m_droppedFrameCount += selection.droppedFrames;
        ++m_selectedFrameCount;
    }

    if (selection.reachedEnd) {
        Q_ASSERT(selection.mediaEndMicroseconds);
        m_clockAnchorMediaMicroseconds = *selection.mediaEndMicroseconds;
        m_clockAnchorTime = now;
        m_ended = true;
        m_userWantsPlaying = false;
        m_audioPlayIntent.store(false, std::memory_order_release);
    }

    if (selection.frame) {
        m_pendingPublicationGeneration = m_playbackGeneration;
        m_sessionNotificationPending =
            enteredReady || firstPublishedFrame || selection.reachedEnd || videoDetailsChanged;
        m_playbackMetricsNotificationPending = true;
    } else if (selection.reachedEnd || enteredReady) {
        emit sessionChanged();
        emit timelineChanged();
        emit playbackMetricsChanged();
    }

    return selection.frame ? std::move(selection.frame->frame) : std::shared_ptr<DecodedVideoFrame const>();
}

bool MediaSession::wantsContinuousVideoFrames() const { return playing(); }
