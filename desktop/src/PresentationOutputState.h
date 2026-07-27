#pragma once

#include <memory>

#include <QObject>
#include <QPointer>

#include "WindowsDisplayMonitor.h"

class QScreen;
class QWindow;

class PresentationOutputState final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString screenName READ screenName NOTIFY stateChanged)
    Q_PROPERTY(QString graphicsApi READ graphicsApi CONSTANT)
    Q_PROPERTY(QString swapChainFormat READ swapChainFormat CONSTANT)
    Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY stateChanged)
    Q_PROPERTY(qreal logicalDotsPerInch READ logicalDotsPerInch NOTIFY stateChanged)
    Q_PROPERTY(qreal refreshRate READ refreshRate NOTIFY stateChanged)
    Q_PROPERTY(bool queryValid READ queryValid NOTIFY stateChanged)
    Q_PROPERTY(bool hdrActive READ hdrActive NOTIFY stateChanged)
    Q_PROPERTY(bool scRgbSupported READ scRgbSupported CONSTANT)
    Q_PROPERTY(bool sceneReferred READ sceneReferred NOTIFY stateChanged)
    Q_PROPERTY(bool absoluteLuminanceKnown READ absoluteLuminanceKnown NOTIFY stateChanged)
    Q_PROPERTY(bool sdrWhiteKnown READ sdrWhiteKnown NOTIFY stateChanged)
    Q_PROPERTY(float sdrWhiteNits READ sdrWhiteNits NOTIFY stateChanged)
    Q_PROPERTY(float minLuminanceNits READ minLuminanceNits NOTIFY stateChanged)
    Q_PROPERTY(float maxLuminanceNits READ maxLuminanceNits NOTIFY stateChanged)
    Q_PROPERTY(float currentHeadroom READ currentHeadroom NOTIFY stateChanged)
    Q_PROPERTY(float potentialHeadroom READ potentialHeadroom NOTIFY stateChanged)
    Q_PROPERTY(float referenceWhiteNits READ referenceWhiteNits NOTIFY stateChanged)
    Q_PROPERTY(float displayPeakNits READ displayPeakNits NOTIFY stateChanged)
    Q_PROPERTY(float sdrScale READ sdrScale NOTIFY stateChanged)

public:
    explicit PresentationOutputState(QWindow *window, QObject *parent = nullptr);
    ~PresentationOutputState() override;

    QString screenName() const;
    QString graphicsApi() const;
    QString swapChainFormat() const;
    qreal devicePixelRatio() const;
    qreal logicalDotsPerInch() const;
    qreal refreshRate() const;
    bool queryValid() const;
    bool hdrActive() const;
    bool scRgbSupported() const;
    bool sceneReferred() const;
    bool absoluteLuminanceKnown() const;
    bool sdrWhiteKnown() const;
    float sdrWhiteNits() const;
    float minLuminanceNits() const;
    float maxLuminanceNits() const;
    float currentHeadroom() const;
    float potentialHeadroom() const;
    float referenceWhiteNits() const;
    float displayPeakNits() const;
    float sdrScale() const;

    Q_INVOKABLE void refresh();

signals:
    void stateChanged();

private:
    void attachScreen(QScreen *screen);
    void updateMetrics();
    void applyWindowsState(const WindowsAdvancedColorState &state);

    QPointer<QWindow> m_window;
    QPointer<QScreen> m_screen;
    QList<QMetaObject::Connection> m_connections;
    std::unique_ptr<WindowsDisplayMonitor> m_monitor;
    WindowsAdvancedColorState m_state;
    QString m_screenName = QStringLiteral("Unavailable");
    qreal m_dpr = 1.0;
    qreal m_logicalDpi = 96.0;
    qreal m_refreshRate = 0.0;
};
