#include "presentation/PresentationTarget.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float scRgbReferenceWhiteNits = 80.0f;
}

PresentationTarget calculatePresentationTarget(DisplayState const& display, PresentationBackendState const& backend) {
    PresentationTarget target;
    if (!backend.hdrPresentationActive) {
        return target;
    }

    target.hdrPresentationActive = true;
    target.sceneReferred = backend.sceneReferred;

    bool const displayTargetUsable =
        display.valid && (display.hdrActive || backend.useSdrDisplayTargetForHdrPresentation);
    bool const displayWhiteKnown = displayTargetUsable && display.sdrWhiteNits > 0.0f;
    bool const displayLuminanceKnown = displayTargetUsable && display.maxLuminanceNits > 0.0f;
    bool const displayHeadroomKnown = display.valid && display.currentHeadroom > 0.0f;

    target.sdrWhiteKnown = displayWhiteKnown || backend.sdrWhiteKnown;
    target.sdrWhiteNits =
        displayWhiteKnown ? display.sdrWhiteNits : (backend.sdrWhiteKnown ? backend.sdrWhiteNits : 0.0f);

    target.luminanceKnown = displayLuminanceKnown || backend.luminanceKnown;
    target.minLuminanceNits =
        displayLuminanceKnown ? display.minLuminanceNits : (backend.luminanceKnown ? backend.minLuminanceNits : 0.0f);
    target.maxLuminanceNits =
        displayLuminanceKnown ? display.maxLuminanceNits : (backend.luminanceKnown ? backend.maxLuminanceNits : 0.0f);

    float const effectiveSdrWhiteNits = target.sdrWhiteNits > 0.0f ? target.sdrWhiteNits : scRgbReferenceWhiteNits;
    target.sdrScale = target.sceneReferred ? effectiveSdrWhiteNits / scRgbReferenceWhiteNits : 1.0f;
    Q_ASSERT(std::isfinite(target.sdrScale) && target.sdrScale > 0.0f);

    target.currentHeadroom =
        displayHeadroomKnown
            ? std::max(1.0f, display.currentHeadroom)
            : (target.maxLuminanceNits > 0.0f ? std::max(1.0f, target.maxLuminanceNits / scRgbReferenceWhiteNits)
                                              : std::max(1.0f, backend.currentHeadroom));
    target.potentialHeadroom =
        std::max(target.currentHeadroom, display.valid && display.potentialHeadroom > 0.0f ? display.potentialHeadroom
                                                                                           : backend.potentialHeadroom);
    target.effectiveTargetHeadroom = std::max(1.0f, target.currentHeadroom / target.sdrScale);
    return target;
}
