#include "platform/linux/WaylandColorManagement.h"

#include <algorithm>
#include <cmath>

#include <QStringList>
#include <libplacebo/colorspace.h>

bool WaylandColorManagementCapabilities::supportsManagedSdr() const {
    return protocolAdvertised && protocolVersion >= requiredProtocolVersion && inventoryComplete &&
           parametricDescriptions && perceptualIntent && namedSrgbPrimaries && gamma22Transfer;
}

bool WaylandColorManagementCapabilities::supportsManagedHdr10() const {
    return supportsManagedSdr() && namedBt2020Primaries && pqTransfer && masteringDisplayPrimaries;
}

WaylandCompositionVolume waylandCompositionVolume(float headroom, ColorPrimaries const& videoPrimaries) {
    Q_ASSERT(std::isfinite(headroom) && headroom >= 1.0f && headroom <= PresentationSurfaceContract::pqMaximumHeadroom);
    constexpr ColorPrimaries srgb{{0.64f, 0.33f}, {0.30f, 0.60f}, {0.15f, 0.06f}, {0.3127f, 0.3290f}};
    constexpr ColorPrimaries bt2020{{0.708f, 0.292f}, {0.170f, 0.797f}, {0.131f, 0.046f}, {0.3127f, 0.3290f}};
    auto const contains = [](ColorPrimaries gamut, ColorPrimaries const& inner) {
        for (auto const point : {inner.red, inner.green, inner.blue}) {
            gamut.white = point;
            if (!gamut.isValid()) {
                return false;
            }
        }
        return true;
    };
    ColorPrimaries const video = videoPrimaries.isValid() ? videoPrimaries : srgb;
    // A single triangle cannot describe every gamut union. Keep the transport
    // volume for unusual targets rather than exclude UI colors or change pixels.
    // A different white can also produce transport components above H.
    bool const commonWhite =
        std::abs(video.white.x - srgb.white.x) < 0.00001f && std::abs(video.white.y - srgb.white.y) < 0.00001f;
    bool const tightVolume = commonWhite && contains(video, srgb) && contains(bt2020, video);
    float maximumNits = 10000.0f;
    if (commonWhite) {
        float maximumComponent = headroom;
        if (!tightVolume) {
            pl_raw_primaries const source{
                .red = {video.red.x, video.red.y},
                .green = {video.green.x, video.green.y},
                .blue = {video.blue.x, video.blue.y},
                .white = {video.white.x, video.white.y},
            };
            auto const matrix = pl_get_color_mapping_matrix(&source, pl_raw_primaries_get(PL_COLOR_PRIM_BT_2020),
                                                            PL_INTENT_RELATIVE_COLORIMETRIC);
            // P3 slightly exceeds the BT.2020 triangle. Bound its encoded RGB
            // cube with the library's transform, rather than claim containment.
            for (auto const& row : matrix.m) {
                float const bound =
                    headroom * (std::max(0.0f, row[0]) + std::max(0.0f, row[1]) + std::max(0.0f, row[2]));
                maximumComponent = std::max(maximumComponent, bound);
            }
        }
        maximumNits = std::min(10000.0f, std::ceil(203.0f * maximumComponent));
    }
    return {
        .primaries = tightVolume ? video : bt2020,
        .maximumNits = static_cast<std::uint32_t>(maximumNits),
    };
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
    state.colorMode = headroom > 1.0f ? DisplayColorMode::HighDynamicRange : DisplayColorMode::StandardDynamicRange;
    state.luminanceBehavior = DisplayLuminanceBehavior::DisplayReferred;
    state.targetPrimariesKnown = true;
    state.targetPrimaries = description.targetPrimariesKnown ? description.targetPrimaries : description.primaries;
    state.sdrWhiteKnown = true;
    state.luminanceKnown = true;
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
            .diagnostic =
                managedHdr10
                    ? QStringLiteral("SunPlayer-owned color-management-v1 · BT.2020 PQ · managed sRGB fallback")
                    : QStringLiteral("SunPlayer-owned color-management-v1 · sRGB primaries · gamma 2.2"),
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
