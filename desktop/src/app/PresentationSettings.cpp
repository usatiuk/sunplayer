#include "app/PresentationSettings.h"

#include <cmath>

PresentationSettings::PresentationSettings(QObject *parent) : QObject(parent) {}

bool PresentationSettings::automaticTargetPeak() const { return m_automaticTargetPeak; }
float PresentationSettings::manualTargetHeadroom() const { return m_manualTargetHeadroom; }
QRectF PresentationSettings::canvasRect() const { return m_canvasRect; }

void PresentationSettings::setAutomaticTargetPeak(bool value) {
    if (value == m_automaticTargetPeak)
        return;
    m_automaticTargetPeak = value;
    emit settingsChanged();
}

void PresentationSettings::setManualTargetHeadroom(float value) {
    Q_ASSERT(std::isfinite(value) && value >= 1.0f);
    if (qFuzzyCompare(value, m_manualTargetHeadroom))
        return;
    m_manualTargetHeadroom = value;
    emit settingsChanged();
}

void PresentationSettings::setCanvasRect(const QRectF &value) {
    Q_ASSERT(std::isfinite(value.x()));
    Q_ASSERT(std::isfinite(value.y()));
    Q_ASSERT(std::isfinite(value.width()) && value.width() >= 1.0);
    Q_ASSERT(std::isfinite(value.height()) && value.height() >= 1.0);
    if (value == m_canvasRect)
        return;
    m_canvasRect = value;
    emit settingsChanged();
}
