#pragma once

#include <memory>

#include <QObject>
#include <QPointer>

#include "DisplayState.h"
#include "PresentationTarget.h"

class QScreen;
class QWindow;
class DisplayStateProvider;

class PresentationOutputState final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString screenName READ screenName NOTIFY stateChanged)
    Q_PROPERTY(QString graphicsApi READ graphicsApi NOTIFY stateChanged)
    Q_PROPERTY(QString swapChainFormat READ swapChainFormat NOTIFY stateChanged)
    Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY stateChanged)
    Q_PROPERTY(qreal refreshRate READ refreshRate NOTIFY stateChanged)
    Q_PROPERTY(bool displayHdrEnabled READ displayHdrEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool extendedLinearActive READ extendedLinearActive NOTIFY stateChanged)
    Q_PROPERTY(bool sceneReferred READ sceneReferred NOTIFY stateChanged)
    Q_PROPERTY(bool sdrWhiteKnown READ sdrWhiteKnown NOTIFY stateChanged)
    Q_PROPERTY(bool luminanceKnown READ luminanceKnown NOTIFY stateChanged)
    Q_PROPERTY(float sdrWhiteNits READ sdrWhiteNits NOTIFY stateChanged)
    Q_PROPERTY(float minLuminanceNits READ minLuminanceNits NOTIFY stateChanged)
    Q_PROPERTY(float maxLuminanceNits READ maxLuminanceNits NOTIFY stateChanged)
    Q_PROPERTY(float currentHeadroom READ currentHeadroom NOTIFY stateChanged)
    Q_PROPERTY(float potentialHeadroom READ potentialHeadroom NOTIFY stateChanged)
    Q_PROPERTY(float effectiveTargetHeadroom READ effectiveTargetHeadroom
               NOTIFY stateChanged)
    Q_PROPERTY(float sdrScale READ sdrScale NOTIFY stateChanged)

public:
    explicit PresentationOutputState(QObject *parent = nullptr);
    ~PresentationOutputState() override;

    void attach(QWindow &window);

    QString screenName() const;
    QString graphicsApi() const;
    QString swapChainFormat() const;
    qreal devicePixelRatio() const;
    qreal refreshRate() const;
    bool displayHdrEnabled() const;
    bool extendedLinearActive() const;
    bool sceneReferred() const;
    bool sdrWhiteKnown() const;
    bool luminanceKnown() const;
    float sdrWhiteNits() const;
    float minLuminanceNits() const;
    float maxLuminanceNits() const;
    float currentHeadroom() const;
    float potentialHeadroom() const;
    float effectiveTargetHeadroom() const;
    float sdrScale() const;

    Q_INVOKABLE void reprobePresentation();
    void setBackendState(const PresentationBackendState &state);

signals:
    void stateChanged();
    void outputCharacteristicsChanged();

private:
    void attachScreen(QScreen *screen);
    void updateMetrics();
    void applyDisplayState(const DisplayState &state);
    PresentationTarget presentationTarget() const;

    QPointer<QWindow> m_window;
    QPointer<QScreen> m_screen;
    QList<QMetaObject::Connection> m_connections;
    std::unique_ptr<DisplayStateProvider> m_provider;
    DisplayState m_state;
    PresentationBackendState m_backendState;
    QString m_screenName = QStringLiteral("Unavailable");
    qreal m_dpr = 1.0;
    qreal m_refreshRate = 0.0;
};
