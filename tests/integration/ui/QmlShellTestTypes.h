#pragma once

#include <utility>

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

class ShellTestMediaTrackModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        StreamIndexRole,
        AvailableRole,
    };

    ShellTestMediaTrackModel(QString firstLabel, int firstIndex, QString secondLabel, int secondIndex,
                             QObject* parent = nullptr)
        : QAbstractListModel(parent), m_labels{std::move(firstLabel), std::move(secondLabel)},
          m_streamIndexes{firstIndex, secondIndex} {}

    int rowCount(QModelIndex const& parent = {}) const override { return parent.isValid() ? 0 : 2; }

    QVariant data(QModelIndex const& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= 2) {
            return {};
        }
        switch (role) {
        case Qt::DisplayRole:
        case LabelRole:
            return m_labels[index.row()];
        case StreamIndexRole:
            return m_streamIndexes[index.row()];
        case AvailableRole:
            return true;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {LabelRole, QByteArrayLiteral("label")},
            {StreamIndexRole, QByteArrayLiteral("streamIndex")},
            {AvailableRole, QByteArrayLiteral("available")},
        };
    }

    bool contains(int streamIndex) const {
        return m_streamIndexes[0] == streamIndex || m_streamIndexes[1] == streamIndex;
    }

  private:
    QString m_labels[2];
    int m_streamIndexes[2];
};

class ShellTestSubtitleTrackModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        StreamIndexRole,
        SelectedRole,
        AvailableRole,
    };

    explicit ShellTestSubtitleTrackModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(QModelIndex const& parent = {}) const override { return parent.isValid() ? 0 : 3; }

    QVariant data(QModelIndex const& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= 3) {
            return {};
        }
        static constexpr int streamIndexes[] = {-1, 2, 3};
        static QString const labels[] = {
            QStringLiteral("Off"),
            QStringLiteral("English — Styled Ahem"),
            QStringLiteral("Czech — Plain Czech (SDH)"),
        };
        switch (role) {
        case Qt::DisplayRole:
        case LabelRole:
            return labels[index.row()];
        case StreamIndexRole:
            return streamIndexes[index.row()];
        case SelectedRole:
            return streamIndexes[index.row()] == m_selectedStreamIndex;
        case AvailableRole:
            return true;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {LabelRole, QByteArrayLiteral("label")},
            {StreamIndexRole, QByteArrayLiteral("streamIndex")},
            {SelectedRole, QByteArrayLiteral("selected")},
            {AvailableRole, QByteArrayLiteral("available")},
        };
    }

    void select(int streamIndex) {
        if (streamIndex == m_selectedStreamIndex) {
            return;
        }
        m_selectedStreamIndex = streamIndex;
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    }

  private:
    int m_selectedStreamIndex = -1;
};

class ShellTestWindowChromeController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY stateChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY stateChanged)
    Q_PROPERTY(bool maximized READ maximized NOTIFY stateChanged)

  public:
    explicit ShellTestWindowChromeController(QObject* parent) : QObject(parent) {}

    bool enabled() const { return m_enabled; }
    bool fullscreen() const { return m_fullscreen; }
    bool maximized() const { return m_maximized; }

    void setState(bool enabled, bool fullscreen, bool maximized) {
        if (enabled == m_enabled && fullscreen == m_fullscreen && maximized == m_maximized) {
            return;
        }
        m_enabled = enabled;
        m_fullscreen = fullscreen;
        m_maximized = maximized;
        emit stateChanged();
    }

    Q_INVOKABLE void minimize() {}
    Q_INVOKABLE void toggleMaximized() {}
    Q_INVOKABLE void close() {}
    Q_INVOKABLE bool beginSystemMove() { return false; }
    Q_INVOKABLE bool beginSystemResize(int) { return false; }

  signals:
    void stateChanged();

  private:
    bool m_enabled = false;
    bool m_fullscreen = false;
    bool m_maximized = false;
};

class ShellTestWindowCommands final : public QWindow {
    Q_OBJECT
    QML_NAMED_ELEMENT(WindowCommands)
    QML_UNCREATABLE("Test commands are supplied by the component harness")
    Q_PROPERTY(bool cursorHidden READ cursorHidden WRITE setCursorHidden)
    Q_PROPERTY(bool windowShortcutsBlocked READ windowShortcutsBlocked WRITE setWindowShortcutsBlocked)
    Q_PROPERTY(bool otherDisplayBlankingAvailable READ otherDisplayBlankingAvailable NOTIFY
                   otherDisplayBlankingAvailableChanged)
    Q_PROPERTY(bool blankOtherDisplaysInFullscreen READ blankOtherDisplaysInFullscreen WRITE
                   setBlankOtherDisplaysInFullscreen NOTIFY blankOtherDisplaysInFullscreenChanged)
    Q_PROPERTY(QObject* windowChrome READ windowChrome CONSTANT)

  public:
    explicit ShellTestWindowCommands(QWindow* parent) : QWindow(parent), m_windowChrome(this) {}

    int toggleCount() const { return m_toggleCount; }
    int openCount() const { return m_openCount; }
    int restartCount() const { return m_restartCount; }
    int quitCount() const { return m_quitCount; }
    QUrl lastOpenedUrl() const { return m_lastOpenedUrl; }
    bool cursorHidden() const { return m_cursorHidden; }
    void setCursorHidden(bool hidden) { m_cursorHidden = hidden; }
    bool windowShortcutsBlocked() const { return m_windowShortcutsBlocked; }
    void setWindowShortcutsBlocked(bool blocked) { m_windowShortcutsBlocked = blocked; }
    bool otherDisplayBlankingAvailable() const { return m_otherDisplayBlankingAvailable; }
    void setOtherDisplayBlankingAvailable(bool available) {
        if (available == m_otherDisplayBlankingAvailable) {
            return;
        }
        m_otherDisplayBlankingAvailable = available;
        emit otherDisplayBlankingAvailableChanged();
    }
    bool blankOtherDisplaysInFullscreen() const { return m_blankOtherDisplaysInFullscreen; }
    void setBlankOtherDisplaysInFullscreen(bool enabled) {
        if (enabled == m_blankOtherDisplaysInFullscreen) {
            return;
        }
        m_blankOtherDisplaysInFullscreen = enabled;
        emit blankOtherDisplaysInFullscreenChanged();
    }

    void resetToggleCount() { m_toggleCount = 0; }

    ShellTestWindowChromeController& windowChromeController() { return m_windowChrome; }

    Q_INVOKABLE void toggleFullscreen() { ++m_toggleCount; }
    Q_INVOKABLE void restartApplication() { ++m_restartCount; }
    Q_INVOKABLE void quitApplication() { ++m_quitCount; }
    Q_INVOKABLE void openMedia(QUrl const& url) {
        ++m_openCount;
        m_lastOpenedUrl = url;
        emit mediaOpenRequested();
    }
    QObject* windowChrome() { return &m_windowChrome; }

  signals:
    void otherDisplayBlankingAvailableChanged();
    void blankOtherDisplaysInFullscreenChanged();
    void relativeSeekRequested(qlonglong milliseconds);
    void mediaOpenRequested();

  private:
    ShellTestWindowChromeController m_windowChrome;
    int m_toggleCount = 0;
    int m_openCount = 0;
    int m_restartCount = 0;
    int m_quitCount = 0;
    QUrl m_lastOpenedUrl;
    bool m_cursorHidden = false;
    bool m_windowShortcutsBlocked = false;
    bool m_otherDisplayBlankingAvailable = true;
    bool m_blankOtherDisplaysInFullscreen = false;
};

class ShellTestPresentationOutputState final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PresentationOutputState)
    QML_UNCREATABLE("Test state is supplied by the component harness")

    Q_PROPERTY(QString screenName MEMBER m_screenName CONSTANT)
    Q_PROPERTY(QString graphicsApi MEMBER m_graphicsApi CONSTANT)
    Q_PROPERTY(QString graphicsAdapter MEMBER m_graphicsAdapter CONSTANT)
    Q_PROPERTY(QString swapChainFormat MEMBER m_swapChainFormat CONSTANT)
    Q_PROPERTY(QString videoSurfaceFormat MEMBER m_videoSurfaceFormat CONSTANT)
    Q_PROPERTY(QString videoSurfaceProducer MEMBER m_videoSurfaceProducer CONSTANT)
    Q_PROPERTY(QString videoColorPolicy MEMBER m_videoColorPolicy CONSTANT)
    Q_PROPERTY(QString videoInputPath MEMBER m_videoInputPath CONSTANT)
    Q_PROPERTY(QString videoOutputPath MEMBER m_videoOutputPath CONSTANT)
    Q_PROPERTY(QString videoSynchronization MEMBER m_videoSynchronization CONSTANT)
    Q_PROPERTY(QString videoCopySummary MEMBER m_videoCopySummary CONSTANT)
    Q_PROPERTY(QString videoFallbackReason MEMBER m_videoFallbackReason CONSTANT)
    Q_PROPERTY(qreal devicePixelRatio MEMBER m_devicePixelRatio CONSTANT)
    Q_PROPERTY(qreal refreshRate MEMBER m_refreshRate CONSTANT)
    Q_PROPERTY(QString displayColorMode MEMBER m_displayColorMode CONSTANT)
    Q_PROPERTY(QString targetGamut MEMBER m_targetGamut CONSTANT)
    Q_PROPERTY(bool displayHdrEnabled MEMBER m_displayHdrEnabled CONSTANT)
    Q_PROPERTY(bool hdrPresentationActive MEMBER m_hdrPresentationActive CONSTANT)
    Q_PROPERTY(bool sceneReferred MEMBER m_sceneReferred CONSTANT)
    Q_PROPERTY(bool sdrWhiteKnown MEMBER m_sdrWhiteKnown CONSTANT)
    Q_PROPERTY(bool luminanceKnown MEMBER m_luminanceKnown CONSTANT)
    Q_PROPERTY(float sdrWhiteNits MEMBER m_sdrWhiteNits CONSTANT)
    Q_PROPERTY(float minLuminanceNits MEMBER m_minLuminanceNits CONSTANT)
    Q_PROPERTY(float maxLuminanceNits MEMBER m_maxLuminanceNits CONSTANT)
    Q_PROPERTY(float currentHeadroom MEMBER m_currentHeadroom CONSTANT)
    Q_PROPERTY(float potentialHeadroom MEMBER m_potentialHeadroom CONSTANT)
    Q_PROPERTY(float effectiveTargetHeadroom MEMBER m_effectiveTargetHeadroom CONSTANT)
    Q_PROPERTY(float sdrScale MEMBER m_sdrScale CONSTANT)

  public:
    explicit ShellTestPresentationOutputState(QObject* parent) : QObject(parent) {}

    Q_INVOKABLE void reprobePresentation() {}

    void setPresentation(QString swapChainFormat, bool displayHdrEnabled, bool hdrPresentationActive) {
        m_swapChainFormat = std::move(swapChainFormat);
        m_displayHdrEnabled = displayHdrEnabled;
        m_hdrPresentationActive = hdrPresentationActive;
    }

  private:
    QString m_screenName = QStringLiteral("Test display");
    QString m_graphicsApi = QStringLiteral("Test RHI");
    QString m_graphicsAdapter = QStringLiteral("Test adapter");
    QString m_swapChainFormat = QStringLiteral("Test swapchain");
    QString m_videoSurfaceFormat = QStringLiteral("Test surface");
    QString m_videoSurfaceProducer = QStringLiteral("Test producer");
    QString m_videoColorPolicy = QStringLiteral(
        "libplacebo spline tone mapping · perceptual gamut mapping · no inverse mapping · peak detection off · "
        "dithering off");
    QString m_videoInputPath = QStringLiteral("Fixed-size persistent software input");
    QString m_videoOutputPath = QStringLiteral("Direct wrapped RGBA16F target");
    QString m_videoSynchronization = QStringLiteral("QRhi external command synchronization");
    QString m_videoCopySummary = QStringLiteral(
        "0 input CPU transfers per input frame · 0 input GPU copies per input frame · 0 output GPU copies · 0 "
        "output CPU transfers per render");
    QString m_videoFallbackReason;
    qreal m_devicePixelRatio = 1.0;
    qreal m_refreshRate = 60.0;
    QString m_displayColorMode = QStringLiteral("Standard dynamic range");
    QString m_targetGamut =
        QStringLiteral("Target gamut xy · R 0.6800,0.3200 · G 0.2650,0.6900 · B 0.1500,0.0600 · W 0.3127,0.3290");
    bool m_displayHdrEnabled = false;
    bool m_hdrPresentationActive = false;
    bool m_sceneReferred = false;
    bool m_sdrWhiteKnown = true;
    bool m_luminanceKnown = true;
    float m_sdrWhiteNits = 80.0f;
    float m_minLuminanceNits = 0.0f;
    float m_maxLuminanceNits = 100.0f;
    float m_currentHeadroom = 1.0f;
    float m_potentialHeadroom = 1.0f;
    float m_effectiveTargetHeadroom = 1.0f;
    float m_sdrScale = 1.0f;
};

class ShellTestPresentationSettings final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PresentationSettings)
    QML_UNCREATABLE("Test settings are supplied by the component harness")

    Q_PROPERTY(bool automaticTargetPeak MEMBER m_automaticTargetPeak NOTIFY settingsChanged)
    Q_PROPERTY(float manualTargetHeadroom MEMBER m_manualTargetHeadroom NOTIFY settingsChanged)

  public:
    explicit ShellTestPresentationSettings(QObject* parent) : QObject(parent) {}

  signals:
    void settingsChanged();

  private:
    bool m_automaticTargetPeak = true;
    float m_manualTargetHeadroom = 1.0f;
};

class ShellTestDiagnosticVideoSource final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DiagnosticVideoSource)
    QML_UNCREATABLE("Test source is supplied by the component harness")

    Q_PROPERTY(float sourcePeakHeadroom MEMBER m_sourcePeakHeadroom NOTIFY settingsChanged)
    Q_PROPERTY(bool toneMappingEnabled MEMBER m_toneMappingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool animatePattern MEMBER m_animatePattern NOTIFY settingsChanged)
    Q_PROPERTY(bool useLibplacebo READ useLibplacebo WRITE setUseLibplacebo NOTIFY rendererChanged)

  public:
    explicit ShellTestDiagnosticVideoSource(QObject* parent) : QObject(parent) {}

    bool useLibplacebo() const { return m_useLibplacebo; }

    void setUseLibplacebo(bool value) {
        if (value == m_useLibplacebo) {
            return;
        }
        m_useLibplacebo = value;
        emit rendererChanged();
    }

  signals:
    void settingsChanged();
    void rendererChanged();

  private:
    float m_sourcePeakHeadroom = 2.0f;
    bool m_toneMappingEnabled = true;
    bool m_animatePattern = false;
    bool m_useLibplacebo = true;
};

class ShellTestMediaSession final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(MediaSession)
    QML_UNCREATABLE("Test state is supplied by the component harness")

    Q_PROPERTY(State state READ state NOTIFY sessionChanged)
    Q_PROPERTY(QUrl mediaUrl MEMBER m_mediaUrl NOTIFY sessionChanged)
    Q_PROPERTY(QString displayName MEMBER m_displayName NOTIFY sessionChanged)
    Q_PROPERTY(QString errorMessage MEMBER m_errorMessage NOTIFY sessionChanged)
    Q_PROPERTY(QString containerFormat MEMBER m_containerFormat NOTIFY sessionChanged)
    Q_PROPERTY(QString decoderName MEMBER m_decoderName NOTIFY sessionChanged)
    Q_PROPERTY(QString decodePath MEMBER m_decodePath NOTIFY sessionChanged)
    Q_PROPERTY(QString hardwareFallbackReason MEMBER m_hardwareFallbackReason NOTIFY sessionChanged)
    Q_PROPERTY(QString videoSummary MEMBER m_videoSummary NOTIFY sessionChanged)
    Q_PROPERTY(QString selectedVideoTrackSummary READ selectedVideoTrackSummary NOTIFY mediaTracksChanged)
    Q_PROPERTY(QString videoDynamicRange MEMBER m_videoDynamicRange NOTIFY sessionChanged)
    Q_PROPERTY(bool videoHdr MEMBER m_videoHdr NOTIFY sessionChanged)
    Q_PROPERTY(QString videoSignalSummary MEMBER m_videoSignalSummary NOTIFY sessionChanged)
    Q_PROPERTY(QString selectedAudioTrackSummary READ selectedAudioTrackSummary NOTIFY mediaTracksChanged)
    Q_PROPERTY(QString selectedSubtitleTrackSummary READ selectedSubtitleTrackSummary NOTIFY subtitleChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY sessionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY sessionChanged)
    Q_PROPERTY(bool playRequested READ playRequested NOTIFY sessionChanged)
    Q_PROPERTY(bool ended READ ended NOTIFY sessionChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY timelineChanged)
    Q_PROPERTY(bool seeking READ seeking NOTIFY timelineChanged)
    Q_PROPERTY(qlonglong positionMilliseconds READ positionMilliseconds NOTIFY timelineChanged)
    Q_PROPERTY(qlonglong durationMilliseconds READ durationMilliseconds NOTIFY timelineChanged)
    Q_PROPERTY(qulonglong decodedVideoFrames MEMBER m_decodedVideoFrames NOTIFY playbackMetricsChanged)
    Q_PROPERTY(qulonglong selectedVideoFrames MEMBER m_selectedVideoFrames NOTIFY playbackMetricsChanged)
    Q_PROPERTY(qulonglong droppedVideoFrames MEMBER m_droppedVideoFrames NOTIFY playbackMetricsChanged)
    Q_PROPERTY(int queuedVideoFrames MEMBER m_queuedVideoFrames NOTIFY playbackMetricsChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(PlaybackInterruption playbackInterruption MEMBER m_playbackInterruption NOTIFY sessionChanged)
    Q_PROPERTY(bool hasAudioOutput MEMBER m_hasAudioOutput NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QString audioBackend MEMBER m_audioBackend NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(MediaClockSource mediaClockSource MEMBER m_mediaClockSource NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(bool audioClockReliable MEMBER m_audioClockReliable NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(int audioQueuedMilliseconds MEMBER m_audioQueuedMilliseconds NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(qulonglong audioSubmittedFrames MEMBER m_audioSubmittedFrames NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(qulonglong audioPresentedFrames MEMBER m_audioPresentedFrames NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(qulonglong audioUnderrunFrames MEMBER m_audioUnderrunFrames NOTIFY audioDiagnosticsChanged)
    Q_PROPERTY(QAbstractItemModel* videoTracks READ videoTracks CONSTANT)
    Q_PROPERTY(QAbstractItemModel* audioTracks READ audioTracks CONSTANT)
    Q_PROPERTY(int selectedVideoStreamIndex READ selectedVideoStreamIndex NOTIFY mediaTracksChanged)
    Q_PROPERTY(int selectedAudioStreamIndex READ selectedAudioStreamIndex NOTIFY mediaTracksChanged)
    Q_PROPERTY(QAbstractItemModel* subtitleTracks READ subtitleTracks CONSTANT)
    Q_PROPERTY(int selectedSubtitleStreamIndex READ selectedSubtitleStreamIndex NOTIFY subtitleChanged)
    Q_PROPERTY(QString subtitleError MEMBER m_subtitleError NOTIFY subtitleChanged)

  public:
    enum class State {
        Empty,
        Opening,
        Ready,
        Error,
    };
    Q_ENUM(State)

    enum class MediaClockSource {
        Monotonic,
        ProvisionalMonotonic,
        PresentedAudio,
        FrozenAudio,
        PostAudioMonotonic,
    };
    Q_ENUM(MediaClockSource)

    enum class PlaybackInterruption {
        None,
        Buffering,
    };
    Q_ENUM(PlaybackInterruption)

    explicit ShellTestMediaSession(QObject* parent)
        : QObject(parent), m_videoTracks(QStringLiteral("English - Dark (ffv1, 96x64)"), 0,
                                         QStringLiteral("Czech - Light (ffv1, 96x64, Default)"), 2, this),
          m_audioTracks(QStringLiteral("English - Positive (flac, mono)"), 1,
                        QStringLiteral("Czech - Negative (flac, mono, Default)"), 3, this) {}

    State state() const { return m_state; }
    bool hasFrame() const { return m_hasFrame; }
    bool playing() const {
        return m_state == State::Ready && m_playRequested && m_playbackInterruption == PlaybackInterruption::None &&
               !m_ended;
    }
    bool playRequested() const { return m_playRequested && !m_ended; }
    bool ended() const { return m_ended; }
    bool seekable() const { return m_seekable; }
    bool seeking() const { return m_seeking; }
    qlonglong positionMilliseconds() const { return m_positionMilliseconds; }
    qlonglong durationMilliseconds() const { return m_durationMilliseconds; }
    qreal volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QAbstractItemModel* videoTracks() { return &m_videoTracks; }
    QAbstractItemModel* audioTracks() { return &m_audioTracks; }
    int selectedVideoStreamIndex() const { return m_selectedVideoStreamIndex; }
    int selectedAudioStreamIndex() const { return m_selectedAudioStreamIndex; }
    QString selectedVideoTrackSummary() const {
        return m_selectedVideoStreamIndex == 0 ? QStringLiteral("English - Dark (ffv1, 96x64)")
                                               : QStringLiteral("Czech - Light (ffv1, 96x64, Default)");
    }
    QString selectedAudioTrackSummary() const {
        return m_selectedAudioStreamIndex == 1 ? QStringLiteral("English - Positive (flac, mono) · 48 kHz")
                                               : QStringLiteral("Czech - Negative (flac, mono, Default) · 48 kHz");
    }
    QAbstractItemModel* subtitleTracks() { return &m_subtitleTracks; }
    int selectedSubtitleStreamIndex() const { return m_selectedSubtitleStreamIndex; }
    QString selectedSubtitleTrackSummary() const {
        if (m_selectedSubtitleStreamIndex == 2) {
            return QStringLiteral("English — Styled Ahem · ass · text");
        }
        if (m_selectedSubtitleStreamIndex == 3) {
            return QStringLiteral("Czech — Plain Czech (SDH) · subrip · text");
        }
        return QStringLiteral("Off");
    }
    int openCount() const { return m_openCount; }
    int cancelCount() const { return m_cancelCount; }
    int retryCount() const { return m_retryCount; }
    int seekCount() const { return m_seekCount; }
    qlonglong lastSeekMilliseconds() const { return m_lastSeekMilliseconds; }

    void setVideoHdr(bool videoHdr) { m_videoHdr = videoHdr; }

    void setState(State state, bool hasFrame = false) {
        m_state = state;
        m_hasFrame = hasFrame;
        if (state == State::Ready) {
            m_playRequested = true;
        } else if (state == State::Empty || state == State::Error) {
            m_playRequested = false;
        }
        m_ended = false;
        m_seeking = false;
        if (state == State::Ready) {
            m_mediaUrl = QUrl::fromLocalFile(QStringLiteral("test-video.mkv"));
            m_displayName = QStringLiteral("test-video.mkv");
            m_decoderName = QStringLiteral("ffv1");
            m_decodePath = QStringLiteral("Software");
            m_videoSummary = QStringLiteral("96×64 · 4 fps · yuv420p · 8-bit");
            m_videoDynamicRange = QStringLiteral("SDR");
            m_videoSignalSummary = QStringLiteral("BT.709 primaries · BT.709 transfer · limited range");
            m_seekable = true;
            m_durationMilliseconds = 65'000;
        }
        emit sessionChanged();
        emit timelineChanged();
    }

    void setTimeline(qlonglong positionMilliseconds, qlonglong durationMilliseconds, bool seekable,
                     bool seeking = false) {
        m_positionMilliseconds = positionMilliseconds;
        m_durationMilliseconds = durationMilliseconds;
        m_seekable = seekable;
        m_seeking = seeking;
        emit timelineChanged();
    }

    void setVolume(qreal volume) {
        if (m_volume == volume) {
            return;
        }
        m_volume = volume;
        emit volumeChanged();
    }

    void setMuted(bool muted) {
        if (m_muted == muted) {
            return;
        }
        m_muted = muted;
        emit mutedChanged();
    }

    void setHasAudioOutput(bool hasAudioOutput) {
        if (m_hasAudioOutput == hasAudioOutput) {
            return;
        }
        m_hasAudioOutput = hasAudioOutput;
        emit audioDiagnosticsChanged();
    }

    void setPlaybackInterruption(PlaybackInterruption interruption) {
        if (m_playbackInterruption == interruption) {
            return;
        }
        m_playbackInterruption = interruption;
        emit sessionChanged();
    }

    Q_INVOKABLE void openMedia(QUrl const&) { ++m_openCount; }
    Q_INVOKABLE void cancel() {
        ++m_cancelCount;
        setState(State::Empty);
    }
    Q_INVOKABLE void retry() { ++m_retryCount; }
    Q_INVOKABLE void play() {
        m_playRequested = true;
        m_ended = false;
        emit sessionChanged();
    }
    Q_INVOKABLE void pause() {
        m_playRequested = false;
        emit sessionChanged();
    }
    Q_INVOKABLE void seekToMilliseconds(qlonglong positionMilliseconds) {
        ++m_seekCount;
        m_lastSeekMilliseconds = positionMilliseconds;
        m_positionMilliseconds = positionMilliseconds;
        emit timelineChanged();
    }
    Q_INVOKABLE void selectVideoStream(int streamIndex) {
        if (!m_videoTracks.contains(streamIndex)) {
            return;
        }
        m_selectedVideoStreamIndex = streamIndex;
        emit mediaTracksChanged();
    }
    Q_INVOKABLE void selectAudioStream(int streamIndex) {
        if (!m_audioTracks.contains(streamIndex)) {
            return;
        }
        m_selectedAudioStreamIndex = streamIndex;
        emit mediaTracksChanged();
    }
    Q_INVOKABLE void selectSubtitleStream(int streamIndex) {
        if (streamIndex != -1 && streamIndex != 2 && streamIndex != 3) {
            return;
        }
        m_selectedSubtitleStreamIndex = streamIndex;
        m_subtitleTracks.select(streamIndex);
        emit subtitleChanged();
    }

  signals:
    void sessionChanged();
    void playbackMetricsChanged();
    void timelineChanged();
    void volumeChanged();
    void mutedChanged();
    void audioDiagnosticsChanged();
    void mediaTracksChanged();
    void subtitleChanged();

  private:
    State m_state = State::Empty;
    bool m_hasFrame = false;
    bool m_playRequested = false;
    bool m_ended = false;
    bool m_seekable = false;
    bool m_seeking = false;
    qlonglong m_positionMilliseconds = 0;
    qlonglong m_durationMilliseconds = -1;
    qulonglong m_decodedVideoFrames = 3;
    qulonglong m_selectedVideoFrames = 1;
    qulonglong m_droppedVideoFrames = 0;
    int m_queuedVideoFrames = 2;
    qreal m_volume = 0.75;
    bool m_muted = false;
    bool m_hasAudioOutput = true;
    QString m_audioBackend = QStringLiteral("controlled");
    MediaClockSource m_mediaClockSource = MediaClockSource::PresentedAudio;
    PlaybackInterruption m_playbackInterruption = PlaybackInterruption::None;
    bool m_audioClockReliable = true;
    int m_audioQueuedMilliseconds = 80;
    qulonglong m_audioSubmittedFrames = 4'800;
    qulonglong m_audioPresentedFrames = 960;
    qulonglong m_audioUnderrunFrames = 0;
    ShellTestMediaTrackModel m_videoTracks;
    ShellTestMediaTrackModel m_audioTracks;
    int m_selectedVideoStreamIndex = 2;
    int m_selectedAudioStreamIndex = 3;
    ShellTestSubtitleTrackModel m_subtitleTracks;
    int m_selectedSubtitleStreamIndex = -1;
    QString m_subtitleError;
    QUrl m_mediaUrl;
    QString m_displayName;
    QString m_errorMessage;
    QString m_containerFormat;
    QString m_decoderName;
    QString m_decodePath;
    QString m_hardwareFallbackReason;
    QString m_videoSummary;
    QString m_videoDynamicRange = QStringLiteral("Unknown");
    bool m_videoHdr = false;
    QString m_videoSignalSummary;
    int m_openCount = 0;
    int m_cancelCount = 0;
    int m_retryCount = 0;
    int m_seekCount = 0;
    qlonglong m_lastSeekMilliseconds = -1;
};

class ShellTestSupportController final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(SupportController)
    QML_UNCREATABLE("Test support controller is supplied by the component harness")

  public:
    explicit ShellTestSupportController(QObject* parent) : QObject(parent) {}

    int reportCount() const { return m_reportCount; }
    int aboutCount() const { return m_aboutCount; }

    Q_INVOKABLE void reportBug() { ++m_reportCount; }
    Q_INVOKABLE void showAbout() { ++m_aboutCount; }

  private:
    int m_reportCount = 0;
    int m_aboutCount = 0;
};

class ShellTestActiveVideoSource final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ActiveVideoSource)
    QML_UNCREATABLE("Test state is supplied by the component harness")

    Q_PROPERTY(Route route READ route WRITE setRoute NOTIFY routeChanged)

  public:
    enum class Route {
        Player,
        Diagnostics,
    };
    Q_ENUM(Route)

    explicit ShellTestActiveVideoSource(QObject* parent) : QObject(parent) {}

    Route route() const { return m_route; }
    void setRoute(Route route) {
        if (route == m_route) {
            return;
        }
        m_route = route;
        emit routeChanged();
    }

  signals:
    void routeChanged();

  private:
    Route m_route = Route::Player;
};
