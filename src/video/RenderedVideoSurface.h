#pragma once

#include <cstdint>

#include <QSize>

#include "platform/ColorPrimaries.h"

// One resolved mapping target, reused by presentation and video production.
struct VideoColorTarget {
    bool operator==(VideoColorTarget const&) const = default;
    float referenceWhiteNits = 80.0f;
    bool targetMinimumLuminanceKnown = false;
    float targetMinimumLuminanceNits = 0.0f;
    float targetPeakHeadroom = 1.0f;
    bool absolutePqAvailable = false;
    bool targetPrimariesKnown = false;
    ColorPrimaries targetPrimaries;

    bool isValid() const;
};

// Opaque RGBA16F, signed linear BT.709/D65, top-left coordinates. Mapping is
// complete; black is zero and reference white is one before UI composition.
struct RenderedVideoSurfaceDescription {
    bool operator==(RenderedVideoSurfaceDescription const&) const = default;
    QSize pixelSize;
    VideoColorTarget color;
    bool isValid() const;
};

struct RenderedVideoSurfaceState {
    bool operator==(RenderedVideoSurfaceState const&) const = default;

    RenderedVideoSurfaceDescription description;
    // Both identities are nonzero and monotonic within the application process.
    // Swapchain identity is deliberately absent: the texture is device-owned.
    std::uint64_t graphicsDeviceGeneration = 0;
    std::uint64_t contentRevision = 0;

    bool isValid() const;
    bool isReusableFor(RenderedVideoSurfaceState const& requested) const;
};
