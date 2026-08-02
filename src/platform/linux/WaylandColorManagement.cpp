#include "platform/linux/WaylandColorManagement.h"

#include <QStringList>

bool WaylandColorManagementCapabilities::supportsQtManagedSdr() const {
    return protocolAdvertised
        && inventoryComplete
        && parametricDescriptions
        && perceptualIntent
        && namedSrgbPrimaries
        && gamma22Transfer;
}

bool WaylandColorManagementCapabilities::supportsManagedHdrObservation()
        const {
    return supportsQtManagedSdr() && extendedLinearTransfer;
}

PresentationSurfaceContract
WaylandSdrSurfaceSelection::presentationContract() const {
    switch (mode) {
    case WaylandSdrSurfaceMode::UnmanagedSrgb:
        return {
            .sdrTransfer = PresentationOutputTransfer::PiecewiseSrgb,
            .extendedLinearAllowed = false,
        };
    case WaylandSdrSurfaceMode::ManagedGamma22:
        return {
            .sdrTransfer = PresentationOutputTransfer::Gamma22,
            .extendedLinearAllowed = false,
        };
    }
    Q_UNREACHABLE_RETURN(PresentationSurfaceContract{});
}

WaylandSdrSurfaceSelection selectWaylandSdrSurface(
        const WaylandColorManagementCapabilities &capabilities) {
    if (capabilities.supportsQtManagedSdr()) {
        return {
            .mode = WaylandSdrSurfaceMode::ManagedGamma22,
            .diagnostic = QStringLiteral(
                "Qt-owned color-management-v1 · sRGB primaries · gamma 2.2"),
        };
    }

    QStringList missing;
    if (!capabilities.protocolAdvertised) {
        missing.append(QStringLiteral("color-management-v1 unavailable"));
    } else if (!capabilities.inventoryComplete) {
        missing.append(QStringLiteral("capability inventory incomplete"));
    } else {
        if (!capabilities.parametricDescriptions)
            missing.append(QStringLiteral("parametric descriptions"));
        if (!capabilities.perceptualIntent)
            missing.append(QStringLiteral("perceptual intent"));
        if (!capabilities.namedSrgbPrimaries)
            missing.append(QStringLiteral("named sRGB primaries"));
        if (!capabilities.gamma22Transfer)
            missing.append(QStringLiteral("gamma22 transfer"));
    }

    return {
        .mode = WaylandSdrSurfaceMode::UnmanagedSrgb,
        .diagnostic = QStringLiteral(
            "Unmanaged assumed-sRGB SDR · missing %1")
                .arg(missing.join(QStringLiteral(", "))),
    };
}

QString waylandSdrSurfaceModeName(WaylandSdrSurfaceMode mode) {
    switch (mode) {
    case WaylandSdrSurfaceMode::UnmanagedSrgb:
        return QStringLiteral("unmanaged-srgb");
    case WaylandSdrSurfaceMode::ManagedGamma22:
        return QStringLiteral("managed-gamma22-sdr");
    }
    Q_UNREACHABLE_RETURN(QString{});
}
