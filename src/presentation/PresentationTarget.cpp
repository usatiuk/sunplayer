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
    bool const displayTargetUsable = display.valid && (colorTargetActive || backend.managedSdrPresentationActive);
    bool const displayLuminanceAuthority =
        displayTargetUsable && display.luminanceBehavior != DisplayLuminanceBehavior::Unknown;
    target.sceneReferred = displayLuminanceAuthority
                               ? display.luminanceBehavior == DisplayLuminanceBehavior::SceneReferred
                               : backend.sceneReferred;
    bool const backendLuminanceCompatible = !displayLuminanceAuthority || target.sceneReferred == backend.sceneReferred;
    bool const displayWhiteKnown = displayTargetUsable && display.sdrWhiteKnown && display.sdrWhiteNits > 0.0f;
    bool const displayLuminanceKnown = displayTargetUsable && display.luminanceKnown && display.maxLuminanceNits > 0.0f;
    bool const displayHeadroomKnown = displayTargetUsable && display.currentHeadroom > 0.0f;

    target.color.targetPrimariesKnown =
        displayTargetUsable && display.targetPrimariesKnown && display.targetPrimaries.isValid();
    if (target.color.targetPrimariesKnown) {
        target.color.targetPrimaries = display.targetPrimaries;
    }

    target.sdrWhiteKnown = displayWhiteKnown || (backendLuminanceCompatible && backend.sdrWhiteKnown);
    if (target.sdrWhiteKnown) {
        target.color.referenceWhiteNits =
            displayWhiteKnown ? display.sdrWhiteNits : (backend.sdrWhiteKnown ? backend.sdrWhiteNits : 0.0f);
    }

    target.color.targetMinimumLuminanceKnown =
        displayLuminanceKnown || (backendLuminanceCompatible && backend.luminanceKnown);
    if (target.color.targetMinimumLuminanceKnown) {
        target.color.targetMinimumLuminanceNits = displayLuminanceKnown
                                                      ? display.minLuminanceNits
                                                      : (backend.luminanceKnown ? backend.minLuminanceNits : 0.0f);
        target.maxLuminanceNits = displayLuminanceKnown ? display.maxLuminanceNits
                                                        : (backend.luminanceKnown ? backend.maxLuminanceNits : 0.0f);
    }

    // Reject an incoherent observed black/reference-white pair once, before
    // any consumer constructs the common black-relative working coordinates.
    if (target.color.targetMinimumLuminanceKnown &&
        (!isValidDisplayLuminanceRange(target.color.targetMinimumLuminanceNits, target.maxLuminanceNits) ||
         target.color.targetMinimumLuminanceNits >= target.color.referenceWhiteNits)) {
        target.color.targetMinimumLuminanceKnown = false;
        target.color.targetMinimumLuminanceNits = 0.0f;
        target.maxLuminanceNits = 0.0f;
    }

    float const effectiveSdrWhiteNits = target.color.referenceWhiteNits;
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
    target.color.targetPeakHeadroom = std::max(1.0f, target.currentHeadroom / target.sdrScale);
    if (backend.managedSdrPresentationActive) {
        target.color.targetPrimariesKnown = true;
        target.color.targetPrimaries = backend.sdrOutputPrimaries;
        target.sceneReferred = false;
        target.sdrScale = 1.0f;
        target.maxLuminanceNits = target.sdrWhiteKnown ? target.color.referenceWhiteNits : scRgbReferenceWhiteNits;
        target.currentHeadroom = target.potentialHeadroom = target.color.targetPeakHeadroom = 1.0f;
    }
    target.color.absolutePqAvailable =
        backend.absoluteLuminanceSupported && target.hdrPresentationActive &&
        (!display.valid || display.colorMode == DisplayColorMode::HighDynamicRange) && target.sceneReferred &&
        target.sdrWhiteKnown && std::isfinite(target.color.referenceWhiteNits) &&
        target.color.referenceWhiteNits > 0.0f && target.color.targetMinimumLuminanceKnown &&
        isValidDisplayLuminanceRange(target.color.targetMinimumLuminanceNits, target.maxLuminanceNits) &&
        target.maxLuminanceNits >= target.color.referenceWhiteNits;
    return target;
}
