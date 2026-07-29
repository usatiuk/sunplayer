#pragma once

#include <QObject>
#include <QString>
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
