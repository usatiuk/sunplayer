#pragma once

#include <QMetaType>

// Color state observed from the operating system for the selected display.
struct DisplayState {
    bool operator==(DisplayState const&) const = default;

    // valid describes the current output; zero white/max means unknown.
    // A zero minimum luminance is a valid measured value.
    bool valid = false;
    bool hdrActive = false;
    float sdrWhiteNits = 0.0f;
    float minLuminanceNits = 0.0f;
    float maxLuminanceNits = 0.0f;
    // Display-referred platforms can expose relative EDR headroom without
    // exposing the absolute luminance of SDR white.
    float currentHeadroom = 0.0f;
    float potentialHeadroom = 0.0f;
};

Q_DECLARE_METATYPE(DisplayState)
