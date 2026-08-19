#pragma once

#include <cmath>

#include <QMetaType>

#include "platform/ColorPrimaries.h"

enum class DisplayColorMode {
    StandardDynamicRange,
    WideColorGamut,
    HighDynamicRange,
};

enum class DisplayLuminanceBehavior {
    Unknown,
    DisplayReferred,
    SceneReferred,
};

inline bool isValidDisplayLuminanceRange(float minimumNits, float maximumNits) {
    return std::isfinite(minimumNits) && minimumNits >= 0.0f && std::isfinite(maximumNits) && maximumNits > 0.0f &&
           minimumNits <= maximumNits;
}

// Color state observed from the operating system for the selected display.
struct DisplayState {
    bool operator==(DisplayState const&) const = default;

    // valid describes the current output. The known flags identify values that
    // can drive presentation; providers may retain other raw observations for
    // diagnostics. A zero minimum luminance is a valid measured value.
    bool valid = false;
    DisplayColorMode colorMode = DisplayColorMode::StandardDynamicRange;
    DisplayLuminanceBehavior luminanceBehavior = DisplayLuminanceBehavior::Unknown;
    bool targetPrimariesKnown = false;
    ColorPrimaries targetPrimaries;
    bool sdrWhiteKnown = false;
    bool luminanceKnown = false;
    float sdrWhiteNits = 0.0f;
    float minLuminanceNits = 0.0f;
    float maxLuminanceNits = 0.0f;
    // Display-referred platforms can expose relative EDR headroom without
    // exposing the absolute luminance of SDR white.
    float currentHeadroom = 0.0f;
    float potentialHeadroom = 0.0f;
};

Q_DECLARE_METATYPE(DisplayState)
