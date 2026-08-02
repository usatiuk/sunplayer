#pragma once

#include <memory>

#include <QSurface>

class GraphicsDeviceDomain;
class QWindow;

class GraphicsBackendFactory final {
public:
    GraphicsBackendFactory() = delete;

    static void configureQtQuick();
    static QSurface::SurfaceType windowSurfaceType();
    static std::unique_ptr<GraphicsDeviceDomain> createDeviceDomain(
        QWindow &window);
#ifdef Q_OS_WIN
    static std::unique_ptr<GraphicsDeviceDomain> createDeviceDomain();
#endif
};
