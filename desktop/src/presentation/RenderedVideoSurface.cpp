#include "presentation/RenderedVideoSurface.h"

#include <cmath>

bool RenderedVideoSurfaceDescription::isValid() const {
    return !pixelSize.isEmpty()
        && pixelSize.width() > 0
        && pixelSize.height() > 0
        && pixelFormat == RenderedVideoPixelFormat::Rgba16Float
        && colorSpace == RenderedVideoColorSpace::LinearSrgb
        && luminance
            == RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative
        && alphaMode == RenderedVideoAlphaMode::Opaque
        && std::isfinite(referenceWhiteNits)
        && referenceWhiteNits > 0.0f
        && std::isfinite(targetPeakHeadroom)
        && targetPeakHeadroom >= 1.0f;
}

bool RenderedVideoSurfaceState::isValid() const {
    return description.isValid()
        && graphicsDeviceGeneration != 0
        && displayTargetRevision != 0
        && contentRevision != 0;
}

bool RenderedVideoSurfaceState::isReusableFor(
        const RenderedVideoSurfaceState &requested) const {
    return isValid() && requested.isValid() && *this == requested;
}
