#pragma once

#include <cstdint>
#include <optional>

#include <QMetaType>
#include <QString>

#include "platform/DisplayState.h"
#include "presentation/PresentationSurfaceContract.h"

struct WaylandColorManagementCapabilities {
    static constexpr std::uint32_t requiredProtocolVersion = 2;

    bool protocolAdvertised = false;
    std::uint32_t protocolVersion = 0;
    bool inventoryComplete = false;
    bool parametricDescriptions = false;
    bool perceptualIntent = false;
    bool namedSrgbPrimaries = false;
    bool namedBt2020Primaries = false;
    bool gamma22Transfer = false;
    bool pqTransfer = false;

    bool supportsManagedSdr() const;
    bool supportsManagedHdr10() const;
};

struct WaylandChromaticity {
    bool operator==(WaylandChromaticity const&) const = default;

    float x = 0.0f;
    float y = 0.0f;
};

struct WaylandColorPrimaries {
    bool operator==(WaylandColorPrimaries const&) const = default;

    WaylandChromaticity red;
    WaylandChromaticity green;
    WaylandChromaticity blue;
    WaylandChromaticity white;

    bool isValid() const;
};

enum class WaylandTransferFunction {
    Unknown,
    Gamma22,
    ExtendedLinear,
    Pq,
    Other,
};

struct WaylandPreferredDescription {
    bool operator==(WaylandPreferredDescription const&) const = default;

    bool parametric = false;
    bool primariesKnown = false;
    WaylandColorPrimaries primaries;
    WaylandTransferFunction transferFunction = WaylandTransferFunction::Unknown;
    bool luminancesKnown = false;
    float minimumLuminanceNits = 0.0f;
    float maximumLuminanceNits = 0.0f;
    float referenceWhiteNits = 0.0f;
    bool targetPrimariesKnown = false;
    WaylandColorPrimaries targetPrimaries;
    bool targetLuminanceKnown = false;
    float targetMinimumLuminanceNits = 0.0f;
    float targetMaximumLuminanceNits = 0.0f;

    bool isCompleteAndValid() const;
};

std::optional<DisplayState> displayStateFromWaylandDescription(WaylandPreferredDescription const& description);

enum class WaylandSdrSurfaceMode {
    UnmanagedSrgb,
    ManagedGamma22,
};

struct WaylandSurfaceSelection {
    WaylandSdrSurfaceMode mode = WaylandSdrSurfaceMode::UnmanagedSrgb;
    bool managedHdr10 = false;
    QString diagnostic;

    PresentationSurfaceContract presentationContract() const;
};

WaylandSurfaceSelection selectWaylandSurface(WaylandColorManagementCapabilities const& capabilities);

struct WaylandHdrRejection {
    std::uint64_t graphicsDeviceGeneration = 0;
};

PresentationSurfaceMode selectWaylandPresentationMode(WaylandSdrSurfaceMode startupMode,
                                                      WaylandColorManagementCapabilities const& capabilities,
                                                      std::uint64_t graphicsDeviceGeneration,
                                                      std::optional<WaylandHdrRejection> const& rejection);

Q_DECLARE_METATYPE(WaylandColorManagementCapabilities)
