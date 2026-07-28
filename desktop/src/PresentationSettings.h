#pragma once

#include <QObject>
#include <QRectF>

class PresentationSettings final : public QObject {
    Q_OBJECT

    Q_PROPERTY(float sourcePeakHeadroom READ sourcePeakHeadroom
               WRITE setSourcePeakHeadroom NOTIFY settingsChanged)
    Q_PROPERTY(bool automaticTargetPeak READ automaticTargetPeak
               WRITE setAutomaticTargetPeak NOTIFY settingsChanged)
    Q_PROPERTY(float manualTargetHeadroom READ manualTargetHeadroom
               WRITE setManualTargetHeadroom NOTIFY settingsChanged)
    Q_PROPERTY(bool toneMappingEnabled READ toneMappingEnabled
               WRITE setToneMappingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool animatePattern READ animatePattern
               WRITE setAnimatePattern NOTIFY settingsChanged)
    Q_PROPERTY(QRectF canvasRect READ canvasRect
               WRITE setCanvasRect NOTIFY settingsChanged)

public:
    explicit PresentationSettings(QObject *parent = nullptr);

    float sourcePeakHeadroom() const;
    bool automaticTargetPeak() const;
    float manualTargetHeadroom() const;
    bool toneMappingEnabled() const;
    bool animatePattern() const;
    QRectF canvasRect() const;

    void setSourcePeakHeadroom(float value);
    void setAutomaticTargetPeak(bool value);
    void setManualTargetHeadroom(float value);
    void setToneMappingEnabled(bool value);
    void setAnimatePattern(bool value);
    void setCanvasRect(const QRectF &value);

signals:
    void settingsChanged();

private:
    float m_sourcePeakHeadroom = 12.5f;
    float m_manualTargetHeadroom = 7.5f;
    bool m_automaticTargetPeak = true;
    bool m_toneMappingEnabled = true;
    bool m_animatePattern = true;
    QRectF m_canvasRect{24.0, 112.0, 1.0, 1.0};
};
