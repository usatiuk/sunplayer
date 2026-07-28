#include "graphics/GraphicsBackendFactory.h"

#include <QQuickWindow>
#include <QtCore/qlogging.h>

#include "graphics/GraphicsDeviceDomain.h"

#ifdef Q_OS_WIN
#include "graphics/backends/D3D11GraphicsDeviceDomain.h"
#endif

void GraphicsBackendFactory::configureQtQuick() {
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#else
    qFatal("Sunroom does not provide a graphics backend for this platform");
#endif
}

QSurface::SurfaceType GraphicsBackendFactory::windowSurfaceType() {
#ifdef Q_OS_WIN
    return QSurface::Direct3DSurface;
#else
    qFatal("Sunroom does not provide a window surface for this platform");
    return QSurface::RasterSurface;
#endif
}

std::unique_ptr<GraphicsDeviceDomain>
GraphicsBackendFactory::createDeviceDomain() {
#ifdef Q_OS_WIN
    return createD3D11GraphicsDeviceDomain();
#else
    qCritical("Sunroom does not provide a graphics device for this platform");
    return {};
#endif
}
