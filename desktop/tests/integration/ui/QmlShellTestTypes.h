#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

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
    Q_PROPERTY(QString videoInputPath MEMBER m_videoInputPath CONSTANT)
    Q_PROPERTY(QString videoOutputPath MEMBER m_videoOutputPath CONSTANT)
    Q_PROPERTY(QString videoSynchronization MEMBER m_videoSynchronization CONSTANT)
    Q_PROPERTY(QString videoCopySummary MEMBER m_videoCopySummary CONSTANT)
    Q_PROPERTY(QString videoFallbackReason MEMBER m_videoFallbackReason CONSTANT)
    Q_PROPERTY(qreal devicePixelRatio MEMBER m_devicePixelRatio CONSTANT)
    Q_PROPERTY(qreal refreshRate MEMBER m_refreshRate CONSTANT)
    Q_PROPERTY(bool displayHdrEnabled MEMBER m_displayHdrEnabled CONSTANT)
    Q_PROPERTY(bool extendedLinearActive MEMBER m_extendedLinearActive CONSTANT)
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
    explicit ShellTestPresentationOutputState(QObject *parent)
        : QObject(parent) {}

    Q_INVOKABLE void reprobePresentation() {}

private:
    QString m_screenName = QStringLiteral("Test display");
    QString m_graphicsApi = QStringLiteral("Test RHI");
    QString m_graphicsAdapter = QStringLiteral("Test adapter");
    QString m_swapChainFormat = QStringLiteral("Test swapchain");
    QString m_videoSurfaceFormat = QStringLiteral("Test surface");
    QString m_videoSurfaceProducer = QStringLiteral("Test producer");
    QString m_videoInputPath = QStringLiteral("Test input");
    QString m_videoOutputPath = QStringLiteral("Test path");
    QString m_videoSynchronization = QStringLiteral("Test synchronization");
    QString m_videoCopySummary = QStringLiteral("No copies");
    QString m_videoFallbackReason;
    qreal m_devicePixelRatio = 1.0;
    qreal m_refreshRate = 60.0;
    bool m_displayHdrEnabled = false;
    bool m_extendedLinearActive = false;
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

    Q_PROPERTY(bool automaticTargetPeak MEMBER m_automaticTargetPeak
               NOTIFY settingsChanged)
    Q_PROPERTY(float manualTargetHeadroom MEMBER m_manualTargetHeadroom
               NOTIFY settingsChanged)

public:
    explicit ShellTestPresentationSettings(QObject *parent)
        : QObject(parent) {}

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

    Q_PROPERTY(float sourcePeakHeadroom MEMBER m_sourcePeakHeadroom
               NOTIFY settingsChanged)
    Q_PROPERTY(bool toneMappingEnabled MEMBER m_toneMappingEnabled
               NOTIFY settingsChanged)
    Q_PROPERTY(bool animatePattern MEMBER m_animatePattern
               NOTIFY settingsChanged)
    Q_PROPERTY(bool useLibplacebo READ useLibplacebo
               WRITE setUseLibplacebo NOTIFY rendererChanged)

public:
    explicit ShellTestDiagnosticVideoSource(QObject *parent)
        : QObject(parent) {}

    bool useLibplacebo() const {
        return m_useLibplacebo;
    }

    void setUseLibplacebo(bool value) {
        if (value == m_useLibplacebo)
            return;
        m_useLibplacebo = value;
        emit rendererChanged();
    }

signals:
    void settingsChanged();
    void rendererChanged();

private:
    float m_sourcePeakHeadroom = 1.0f;
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
    Q_PROPERTY(QString containerFormat MEMBER m_containerFormat
               NOTIFY sessionChanged)
    Q_PROPERTY(QString decoderName MEMBER m_decoderName NOTIFY sessionChanged)
    Q_PROPERTY(QString decodePath MEMBER m_decodePath NOTIFY sessionChanged)
    Q_PROPERTY(QString hardwareFallbackReason MEMBER m_hardwareFallbackReason
               NOTIFY sessionChanged)
    Q_PROPERTY(QString videoSummary MEMBER m_videoSummary NOTIFY sessionChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY sessionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY sessionChanged)
    Q_PROPERTY(bool ended READ ended NOTIFY sessionChanged)
    Q_PROPERTY(qulonglong decodedVideoFrames MEMBER m_decodedVideoFrames
               NOTIFY playbackMetricsChanged)
    Q_PROPERTY(qulonglong selectedVideoFrames MEMBER m_selectedVideoFrames
               NOTIFY playbackMetricsChanged)
    Q_PROPERTY(qulonglong droppedVideoFrames MEMBER m_droppedVideoFrames
               NOTIFY playbackMetricsChanged)
    Q_PROPERTY(int queuedVideoFrames MEMBER m_queuedVideoFrames
               NOTIFY playbackMetricsChanged)

public:
    enum class State {
        Empty,
        Opening,
        Ready,
        Error,
    };
    Q_ENUM(State)

    explicit ShellTestMediaSession(QObject *parent)
        : QObject(parent) {}

    State state() const { return m_state; }
    bool hasFrame() const { return m_hasFrame; }
    bool playing() const { return m_playing; }
    bool ended() const { return m_ended; }
    int openCount() const { return m_openCount; }
    int cancelCount() const { return m_cancelCount; }
    int retryCount() const { return m_retryCount; }

    void setState(State state, bool hasFrame = false) {
        m_state = state;
        m_hasFrame = hasFrame;
        m_playing = state == State::Ready && hasFrame;
        m_ended = false;
        if (state == State::Ready) {
            m_mediaUrl = QUrl::fromLocalFile(
                QStringLiteral("test-video.mkv"));
            m_displayName =
                QStringLiteral("test-video.mkv");
            m_decoderName = QStringLiteral("ffv1");
            m_decodePath = QStringLiteral("Software");
            m_videoSummary =
                QStringLiteral("96×64 · yuv420p · 8-bit");
        }
        emit sessionChanged();
    }

    Q_INVOKABLE void openMedia(const QUrl &) {
        ++m_openCount;
    }
    Q_INVOKABLE void cancel() {
        ++m_cancelCount;
        setState(State::Empty);
    }
    Q_INVOKABLE void retry() {
        ++m_retryCount;
    }
    Q_INVOKABLE void play() {
        m_playing = true;
        m_ended = false;
        emit sessionChanged();
    }
    Q_INVOKABLE void pause() {
        m_playing = false;
        emit sessionChanged();
    }

signals:
    void sessionChanged();
    void playbackMetricsChanged();

private:
    State m_state = State::Empty;
    bool m_hasFrame = false;
    bool m_playing = false;
    bool m_ended = false;
    qulonglong m_decodedVideoFrames = 3;
    qulonglong m_selectedVideoFrames = 1;
    qulonglong m_droppedVideoFrames = 0;
    int m_queuedVideoFrames = 2;
    QUrl m_mediaUrl;
    QString m_displayName;
    QString m_errorMessage;
    QString m_containerFormat;
    QString m_decoderName;
    QString m_decodePath;
    QString m_hardwareFallbackReason;
    QString m_videoSummary;
    int m_openCount = 0;
    int m_cancelCount = 0;
    int m_retryCount = 0;
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

    explicit ShellTestActiveVideoSource(QObject *parent)
        : QObject(parent) {}

    Route route() const { return m_route; }
    void setRoute(Route route) {
        if (route == m_route)
            return;
        m_route = route;
        emit routeChanged();
    }

signals:
    void routeChanged();

private:
    Route m_route = Route::Player;
};
