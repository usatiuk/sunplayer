#include "app/PresentationSettings.h"

#include <cmath>

PresentationSettings::PresentationSettings(QObject* parent) : QObject(parent) {}

bool PresentationSettings::automaticTargetPeak() const { return m_automaticTargetPeak; }
float PresentationSettings::manualTargetHeadroom() const { return m_manualTargetHeadroom; }

void PresentationSettings::setAutomaticTargetPeak(bool value) {
    if (value == m_automaticTargetPeak) {
        return;
    }
    m_automaticTargetPeak = value;
    emit settingsChanged();
}

void PresentationSettings::setManualTargetHeadroom(float value) {
    Q_ASSERT(std::isfinite(value) && value >= 1.0f);
    if (qFuzzyCompare(value, m_manualTargetHeadroom)) {
        return;
    }
    m_manualTargetHeadroom = value;
    emit settingsChanged();
}
