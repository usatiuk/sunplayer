#pragma once

#include <QObject>
#include <QRectF>

// Transient controls for the diagnostic application shell.
class PresentationSettings final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool automaticTargetPeak READ automaticTargetPeak
               WRITE setAutomaticTargetPeak NOTIFY settingsChanged)
    Q_PROPERTY(float manualTargetHeadroom READ manualTargetHeadroom
               WRITE setManualTargetHeadroom NOTIFY settingsChanged)
    Q_PROPERTY(QRectF canvasRect READ canvasRect
               WRITE setCanvasRect NOTIFY settingsChanged)

public:
    explicit PresentationSettings(QObject *parent = nullptr);

    bool automaticTargetPeak() const;
    float manualTargetHeadroom() const;
    QRectF canvasRect() const;

    void setAutomaticTargetPeak(bool value);
    void setManualTargetHeadroom(float value);
    void setCanvasRect(const QRectF &value);

signals:
    void settingsChanged();

private:
    float m_manualTargetHeadroom = 7.5f;
    bool m_automaticTargetPeak = true;
    QRectF m_canvasRect{24.0, 112.0, 1.0, 1.0};
};
