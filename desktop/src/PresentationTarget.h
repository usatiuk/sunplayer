#pragma once

#include <QMetaType>
#include <QString>

#include "DisplayState.h"

struct PresentationBackendState {
    bool operator==(const PresentationBackendState &) const = default;

    QString graphicsApi = QStringLiteral("Unavailable");
    QString swapChainFormat = QStringLiteral("Unavailable");
    bool extendedLinearActive = false;
    bool sceneReferred = false;
    bool sdrWhiteKnown = false;
    bool luminanceKnown = false;
    float sdrWhiteNits = 80.0f;
    float minLuminanceNits = 0.0f;
    float maxLuminanceNits = 0.0f;
    float currentHeadroom = 1.0f;
    float potentialHeadroom = 1.0f;
};

struct PresentationTarget {
    bool operator==(const PresentationTarget &) const = default;

    bool extendedLinearActive = false;
    bool sceneReferred = false;
    bool sdrWhiteKnown = false;
    bool luminanceKnown = false;
    float sdrWhiteNits = 0.0f;
    float minLuminanceNits = 0.0f;
    float maxLuminanceNits = 0.0f;
    float currentHeadroom = 1.0f;
    float potentialHeadroom = 1.0f;
    float effectiveTargetHeadroom = 1.0f;
    float sdrScale = 1.0f;
};

PresentationTarget calculatePresentationTarget(
    const DisplayState &display,
    const PresentationBackendState &backend);

Q_DECLARE_METATYPE(PresentationBackendState)
Q_DECLARE_METATYPE(PresentationTarget)
