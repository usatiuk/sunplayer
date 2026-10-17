#include "presentation/PresentationTarget.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float scRgbReferenceWhiteNits = 80.0f;
}

PresentationTarget calculatePresentationTarget(DisplayState const& display, PresentationBackendState const& backend) {
    PresentationTarget target;
    if (!backend.hdrPresentationActive && !backend.managedSdrPresentationActive) {
        return target;
    }

    target.hdrPresentationActive = backend.hdrPresentationActive;

    bool const colorTargetActive = display.colorMode != DisplayColorMode::StandardDynamicRange;
    bool const displayTargetUsable =
        display.valid &&
        (colorTargetActive || backend.useSdrDisplayTargetForHdrPresentation || backend.managedSdrPresentationActive);
    bool const displayLuminanceAuthority =
        displayTargetUsable && display.luminanceBehavior != DisplayLuminanceBehavior::Unknown;
    target.sceneReferred = displayLuminanceAuthority
                               ? display.luminanceBehavior == DisplayLuminanceBehavior::SceneReferred
                               : backend.sceneReferred;
    bool const backendLuminanceCompatible = !displayLuminanceAuthority || target.sceneReferred == backend.sceneReferred;
    bool const displayWhiteKnown = displayTargetUsable && display.sdrWhiteKnown && display.sdrWhiteNits > 0.0f;
    bool const displayLuminanceKnown = displayTargetUsable && display.luminanceKnown && display.maxLuminanceNits > 0.0f;
    bool const displayHeadroomKnown = displayTargetUsable && display.currentHeadroom > 0.0f;

    target.targetPrimariesKnown =
        displayTargetUsable && display.targetPrimariesKnown && display.targetPrimaries.isValid();
    if (target.targetPrimariesKnown) {
        target.targetPrimaries = display.targetPrimaries;
    }

    target.sdrWhiteKnown = displayWhiteKnown || (backendLuminanceCompatible && backend.sdrWhiteKnown);
    if (target.sdrWhiteKnown) {
        target.sdrWhiteNits =
            displayWhiteKnown ? display.sdrWhiteNits : (backend.sdrWhiteKnown ? backend.sdrWhiteNits : 0.0f);
    }

    target.luminanceKnown = displayLuminanceKnown || (backendLuminanceCompatible && backend.luminanceKnown);
    if (target.luminanceKnown) {
        target.minLuminanceNits = displayLuminanceKnown ? display.minLuminanceNits
                                                        : (backend.luminanceKnown ? backend.minLuminanceNits : 0.0f);
        target.maxLuminanceNits = displayLuminanceKnown ? display.maxLuminanceNits
                                                        : (backend.luminanceKnown ? backend.maxLuminanceNits : 0.0f);
    }

    float const effectiveSdrWhiteNits = target.sdrWhiteNits > 0.0f ? target.sdrWhiteNits : scRgbReferenceWhiteNits;
    target.sdrScale = target.sceneReferred ? effectiveSdrWhiteNits / scRgbReferenceWhiteNits : 1.0f;
    Q_ASSERT(std::isfinite(target.sdrScale) && target.sdrScale > 0.0f);

    float const backendCurrentHeadroom = backendLuminanceCompatible ? backend.currentHeadroom : 1.0f;
    float const backendPotentialHeadroom = backendLuminanceCompatible ? backend.potentialHeadroom : 1.0f;
    target.currentHeadroom =
        displayHeadroomKnown
            ? std::max(1.0f, display.currentHeadroom)
            : (target.maxLuminanceNits > 0.0f ? std::max(1.0f, target.maxLuminanceNits / scRgbReferenceWhiteNits)
                                              : std::max(1.0f, backendCurrentHeadroom));
    target.potentialHeadroom = std::max(target.currentHeadroom, displayTargetUsable && display.potentialHeadroom > 0.0f
                                                                    ? display.potentialHeadroom
                                                                    : backendPotentialHeadroom);
    target.effectiveTargetHeadroom = std::max(1.0f, target.currentHeadroom / target.sdrScale);
    if (backend.managedSdrPresentationActive) {
        // This surface is relative, zero-black SDR. The compositor owns the
        // physical display floor/white conversion; do not bake its black twice.
        target.sceneReferred = false;
        target.sdrScale = 1.0f;
        target.luminanceKnown = true;
        target.minLuminanceNits = 0.0f;
        target.maxLuminanceNits = target.sdrWhiteKnown ? target.sdrWhiteNits : scRgbReferenceWhiteNits;
        target.currentHeadroom = target.potentialHeadroom = target.effectiveTargetHeadroom = 1.0f;
    }
    target.absolutePqAvailable = backend.absoluteLuminanceSupported && target.hdrPresentationActive &&
                                 (!display.valid || display.colorMode == DisplayColorMode::HighDynamicRange) &&
                                 target.sceneReferred && target.sdrWhiteKnown && std::isfinite(target.sdrWhiteNits) &&
                                 target.sdrWhiteNits > 0.0f && target.luminanceKnown &&
                                 isValidDisplayLuminanceRange(target.minLuminanceNits, target.maxLuminanceNits) &&
                                 target.maxLuminanceNits >= target.sdrWhiteNits;
    return target;
}
