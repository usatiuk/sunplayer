#pragma once

#include <cstdint>

#include <QSize>

// Pure contract shared by a rendered-video producer and the final compositor.
enum class RenderedVideoPixelFormat {
    Unknown,
    Rgba16Float,
};

enum class RenderedVideoColorSpace {
    Unknown,
    LinearSrgb,
};

enum class RenderedVideoLuminance {
    Unknown,
    DisplayTargetedSdrWhiteRelative,
};

enum class RenderedVideoAlphaMode {
    Unknown,
    Opaque,
};

struct RenderedVideoSurfaceDescription {
    bool operator==(RenderedVideoSurfaceDescription const&) const = default;

    // The entire texture is valid and uses canonical top-left coordinates.
    // LinearSrgb permits extended float values outside [0, 1].
    // DisplayTargetedSdrWhiteRelative means source color processing and tone
    // mapping are complete, while RGB 1.0 still represents reference white.
    QSize pixelSize;
    RenderedVideoPixelFormat pixelFormat = RenderedVideoPixelFormat::Unknown;
    RenderedVideoColorSpace colorSpace = RenderedVideoColorSpace::Unknown;
    RenderedVideoLuminance luminance = RenderedVideoLuminance::Unknown;
    RenderedVideoAlphaMode alphaMode = RenderedVideoAlphaMode::Unknown;
    float referenceWhiteNits = 0.0f;
    // A measured zero is valid and differs from unavailable metadata.
    bool targetMinimumLuminanceKnown = false;
    float targetMinimumLuminanceNits = 0.0f;
    float targetPeakHeadroom = 0.0f;

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
