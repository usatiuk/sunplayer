#pragma once

#include <cstdint>

#include <QtGlobal>

class QWindow;

enum class PresentationOutputEncoding {
    Srgb = 0,
    Gamma22Srgb = 1,
    LinearSrgb = 2,
    Bt2020Pq = 3,
};

enum class PresentationSurfaceMode {
    // Existing Windows/macOS policy: choose extended linear when the native
    // platform and active display allow it, otherwise emit piecewise sRGB.
    AdaptiveExtendedLinear,
    UnmanagedSrgb,
    ManagedGamma22Sdr,
    ManagedHdr10Pq,
};

struct PresentationSurfaceContract {
    static constexpr float pqReferenceWhiteNits = 203.0f;
    static constexpr float pqPeakNits = 10'000.0f;
    static constexpr float pqMaximumHeadroom =
        pqPeakNits / pqReferenceWhiteNits;

    PresentationSurfaceMode mode =
        PresentationSurfaceMode::AdaptiveExtendedLinear;

    PresentationOutputEncoding outputEncoding(
            bool extendedLinearActive) const {
        switch (mode) {
        case PresentationSurfaceMode::AdaptiveExtendedLinear:
            return extendedLinearActive
                ? PresentationOutputEncoding::LinearSrgb
                : PresentationOutputEncoding::Srgb;
        case PresentationSurfaceMode::UnmanagedSrgb:
            return PresentationOutputEncoding::Srgb;
        case PresentationSurfaceMode::ManagedGamma22Sdr:
            return PresentationOutputEncoding::Gamma22Srgb;
        case PresentationSurfaceMode::ManagedHdr10Pq:
            return PresentationOutputEncoding::Bt2020Pq;
        }
        Q_UNREACHABLE_RETURN(PresentationOutputEncoding::Srgb);
    }

    bool hdr10Required() const {
        return mode == PresentationSurfaceMode::ManagedHdr10Pq;
    }

    float constrainTargetHeadroom(float requestedHeadroom) const {
        switch (mode) {
        case PresentationSurfaceMode::AdaptiveExtendedLinear:
            return requestedHeadroom;
        case PresentationSurfaceMode::UnmanagedSrgb:
        case PresentationSurfaceMode::ManagedGamma22Sdr:
            return 1.0f;
        case PresentationSurfaceMode::ManagedHdr10Pq:
            return requestedHeadroom > pqMaximumHeadroom
                ? pqMaximumHeadroom
                : requestedHeadroom;
        }
        Q_UNREACHABLE_RETURN(1.0f);
    }
};

// Optional platform boundary for presentation modes with a mutable native
// color declaration. The engine owns rendering-resource reconciliation; the
// controller owns only platform selection and declaration changes.
class PresentationSurfaceController {
public:
    virtual ~PresentationSurfaceController() = default;

    virtual PresentationSurfaceMode desiredMode(
        std::uint64_t graphicsDeviceGeneration) = 0;
    virtual void applyMode(
        QWindow &window,
        PresentationSurfaceMode mode) = 0;
    virtual void rejectHdrTarget(
        std::uint64_t graphicsDeviceGeneration,
        const char *reason) = 0;
};
