#pragma once

#include <cmath>

// Platform-neutral CIE 1931 xy color-volume data. Native display providers
// populate this contract; presentation and rendering consume it without
// depending on platform APIs.
struct ColorChromaticity {
    bool operator==(ColorChromaticity const&) const = default;

    float x = 0.0f;
    float y = 0.0f;

    bool isValid() const {
        return std::isfinite(x) && std::isfinite(y) && x >= 0.0f && y > 0.0f && x <= 1.0f && y <= 1.0f &&
               x + y <= 1.0f;
    }
};

struct ColorPrimaries {
    bool operator==(ColorPrimaries const&) const = default;

    ColorChromaticity red;
    ColorChromaticity green;
    ColorChromaticity blue;
    ColorChromaticity white;

    bool isValid() const {
        if (!red.isValid() || !green.isValid() || !blue.isValid() || !white.isValid()) {
            return false;
        }

        auto const signedDistance = [](ColorChromaticity const& point, ColorChromaticity const& first,
                                       ColorChromaticity const& second) {
            return (point.x - second.x) * (first.y - second.y) -
                   (first.x - second.x) * (point.y - second.y);
        };
        float const area = signedDistance(red, green, blue);
        if (std::abs(area) <= 1.0e-6f) {
            return false;
        }

        float const redGreen = signedDistance(white, red, green);
        float const greenBlue = signedDistance(white, green, blue);
        float const blueRed = signedDistance(white, blue, red);
        bool const hasNegative = redGreen < -1.0e-6f || greenBlue < -1.0e-6f || blueRed < -1.0e-6f;
        bool const hasPositive = redGreen > 1.0e-6f || greenBlue > 1.0e-6f || blueRed > 1.0e-6f;
        return !(hasNegative && hasPositive);
    }
};
