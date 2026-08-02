#pragma once

enum class PresentationOutputTransfer {
    PiecewiseSrgb = 0,
    Gamma22 = 1,
    ExtendedLinear = 2,
};

struct PresentationSurfaceContract {
    PresentationOutputTransfer sdrTransfer =
        PresentationOutputTransfer::PiecewiseSrgb;
    bool extendedLinearAllowed = true;

    bool isValid() const {
        return sdrTransfer
            != PresentationOutputTransfer::ExtendedLinear;
    }
};
