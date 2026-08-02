#pragma once

#include <QMetaType>
#include <QString>

#include "presentation/PresentationSurfaceContract.h"

struct WaylandColorManagementCapabilities {
    bool protocolAdvertised = false;
    bool inventoryComplete = false;
    bool parametricDescriptions = false;
    bool perceptualIntent = false;
    bool namedSrgbPrimaries = false;
    bool gamma22Transfer = false;
    bool extendedLinearTransfer = false;

    bool supportsQtManagedSdr() const;
    bool supportsManagedHdrObservation() const;
};

enum class WaylandSdrSurfaceMode {
    UnmanagedSrgb,
    ManagedGamma22,
};

struct WaylandSdrSurfaceSelection {
    WaylandSdrSurfaceMode mode = WaylandSdrSurfaceMode::UnmanagedSrgb;
    QString diagnostic;

    PresentationSurfaceContract presentationContract() const;
};

WaylandSdrSurfaceSelection selectWaylandSdrSurface(
    const WaylandColorManagementCapabilities &capabilities);

QString waylandSdrSurfaceModeName(WaylandSdrSurfaceMode mode);

Q_DECLARE_METATYPE(WaylandColorManagementCapabilities)
