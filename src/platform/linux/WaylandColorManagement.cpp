#include "platform/linux/WaylandColorManagement.h"

#include <algorithm>
#include <cmath>

#include <QStringList>

namespace {
bool validChromaticity(WaylandChromaticity const& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0.0f && point.y > 0.0f && point.x <= 1.0f &&
           point.y <= 1.0f && point.x + point.y <= 1.0f;
}

float triangleAreaTwice(WaylandColorPrimaries const& primaries) {
    return (primaries.green.x - primaries.red.x) * (primaries.blue.y - primaries.red.y) -
           (primaries.green.y - primaries.red.y) * (primaries.blue.x - primaries.red.x);
}
} // namespace

bool WaylandColorManagementCapabilities::supportsManagedSdr() const {
    return protocolAdvertised && protocolVersion >= requiredProtocolVersion && inventoryComplete &&
           parametricDescriptions && perceptualIntent && namedSrgbPrimaries && gamma22Transfer;
}

bool WaylandColorManagementCapabilities::supportsManagedHdr10() const {
    return supportsManagedSdr() && namedBt2020Primaries && pqTransfer;
}

bool WaylandColorPrimaries::isValid() const {
    return validChromaticity(red) && validChromaticity(green) && validChromaticity(blue) && validChromaticity(white) &&
           std::abs(triangleAreaTwice(*this)) > 1.0e-6f;
}

bool WaylandPreferredDescription::isCompleteAndValid() const {
    return parametric && primariesKnown && primaries.isValid() &&
           transferFunction != WaylandTransferFunction::Unknown && luminancesKnown &&
           std::isfinite(minimumLuminanceNits) && minimumLuminanceNits >= 0.0f && std::isfinite(maximumLuminanceNits) &&
           maximumLuminanceNits > minimumLuminanceNits && std::isfinite(referenceWhiteNits) &&
           referenceWhiteNits > minimumLuminanceNits && targetLuminanceKnown &&
           std::isfinite(targetMinimumLuminanceNits) && targetMinimumLuminanceNits >= 0.0f &&
           std::isfinite(targetMaximumLuminanceNits) && targetMaximumLuminanceNits > targetMinimumLuminanceNits &&
           (!targetPrimariesKnown || targetPrimaries.isValid());
}

std::optional<DisplayState> displayStateFromWaylandDescription(WaylandPreferredDescription const& description) {
    if (!description.isCompleteAndValid()) {
        return std::nullopt;
    }

    float const headroom = std::max(1.0f, description.targetMaximumLuminanceNits / description.referenceWhiteNits);
    DisplayState state;
    state.valid = true;
    state.hdrActive = headroom > 1.0f;
    state.sdrWhiteNits = description.referenceWhiteNits;
    state.minLuminanceNits = description.targetMinimumLuminanceNits;
    state.maxLuminanceNits = description.targetMaximumLuminanceNits;
    state.currentHeadroom = headroom;
    state.potentialHeadroom = headroom;
    return state;
}

PresentationSurfaceContract WaylandSurfaceSelection::presentationContract() const {
    switch (mode) {
    case WaylandSdrSurfaceMode::UnmanagedSrgb:
        return {
            .mode = PresentationSurfaceMode::UnmanagedSrgb,
        };
    case WaylandSdrSurfaceMode::ManagedGamma22:
        return {
            .mode = managedHdr10 ? PresentationSurfaceMode::ManagedHdr10Pq : PresentationSurfaceMode::ManagedGamma22Sdr,
        };
    }
    Q_UNREACHABLE_RETURN(PresentationSurfaceContract{});
}

WaylandSurfaceSelection selectWaylandSurface(WaylandColorManagementCapabilities const& capabilities) {
    if (capabilities.supportsManagedSdr()) {
        bool const managedHdr10 = capabilities.supportsManagedHdr10();
        return {
            .mode = WaylandSdrSurfaceMode::ManagedGamma22,
            .managedHdr10 = managedHdr10,
            .diagnostic = managedHdr10
                              ? QStringLiteral("Sunroom-owned color-management-v1 · BT.2020 PQ · managed sRGB fallback")
                              : QStringLiteral("Sunroom-owned color-management-v1 · sRGB primaries · gamma 2.2"),
        };
    }

    QStringList missing;
    if (!capabilities.protocolAdvertised) {
        missing.append(QStringLiteral("color-management-v1 unavailable"));
    } else if (capabilities.protocolVersion < WaylandColorManagementCapabilities::requiredProtocolVersion) {
        missing.append(QStringLiteral("color-management-v1 version 2"));
    } else if (!capabilities.inventoryComplete) {
        missing.append(QStringLiteral("capability inventory incomplete"));
    } else {
        if (!capabilities.parametricDescriptions) {
            missing.append(QStringLiteral("parametric descriptions"));
        }
        if (!capabilities.perceptualIntent) {
            missing.append(QStringLiteral("perceptual intent"));
        }
        if (!capabilities.namedSrgbPrimaries) {
            missing.append(QStringLiteral("named sRGB primaries"));
        }
        if (!capabilities.gamma22Transfer) {
            missing.append(QStringLiteral("gamma22 transfer"));
        }
    }

    return {
        .mode = WaylandSdrSurfaceMode::UnmanagedSrgb,
        .diagnostic = QStringLiteral("Unmanaged assumed-sRGB SDR · missing %1").arg(missing.join(QStringLiteral(", "))),
    };
}

PresentationSurfaceMode selectWaylandPresentationMode(WaylandSdrSurfaceMode startupMode,
                                                      WaylandColorManagementCapabilities const& capabilities,
                                                      std::uint64_t graphicsDeviceGeneration,
                                                      std::optional<WaylandHdrRejection> const& rejection) {
    if (startupMode == WaylandSdrSurfaceMode::UnmanagedSrgb) {
        return PresentationSurfaceMode::UnmanagedSrgb;
    }

    bool const rejected = rejection && rejection->graphicsDeviceGeneration == graphicsDeviceGeneration;
    if (capabilities.supportsManagedHdr10() && !rejected) {
        return PresentationSurfaceMode::ManagedHdr10Pq;
    }
    return PresentationSurfaceMode::ManagedGamma22Sdr;
}
