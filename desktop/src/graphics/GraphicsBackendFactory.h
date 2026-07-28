#pragma once

#include <memory>

#include <QSurface>

class GraphicsDeviceDomain;

class GraphicsBackendFactory final {
public:
    GraphicsBackendFactory() = delete;

    static void configureQtQuick();
    static QSurface::SurfaceType windowSurfaceType();
    static std::unique_ptr<GraphicsDeviceDomain> createDeviceDomain();
};
