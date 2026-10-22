#include "video/RenderedVideoSurface.h"

#include <cmath>

bool VideoColorTarget::isValid() const {
    return std::isfinite(referenceWhiteNits) && referenceWhiteNits > 0.0f &&
           std::isfinite(targetMinimumLuminanceNits) && std::isfinite(targetPeakHeadroom) &&
           targetPeakHeadroom >= 1.0f &&
           (targetMinimumLuminanceKnown
                ? targetMinimumLuminanceNits >= 0.0f && targetMinimumLuminanceNits < referenceWhiteNits
                : targetMinimumLuminanceNits == 0.0f) &&
           (targetPrimariesKnown ? targetPrimaries.isValid() : targetPrimaries == ColorPrimaries{});
}

bool RenderedVideoSurfaceDescription::isValid() const {
    return pixelSize.width() > 0 && pixelSize.height() > 0 && color.isValid();
}

bool RenderedVideoSurfaceState::isValid() const {
    return description.isValid() && graphicsDeviceGeneration != 0 && contentRevision != 0;
}

bool RenderedVideoSurfaceState::isReusableFor(RenderedVideoSurfaceState const& requested) const {
    return isValid() && requested.isValid() && *this == requested;
}
