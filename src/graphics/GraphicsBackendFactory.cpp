#include "graphics/GraphicsBackendFactory.h"

#include <QQuickWindow>
#include <QtCore/qlogging.h>

#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsDeviceDomain.h"

#ifdef Q_OS_WIN
#include "graphics/backends/D3D11GraphicsDeviceDomain.h"
#elif defined(Q_OS_MACOS)
#include "graphics/backends/MetalGraphicsDeviceDomain.h"
#elif defined(Q_OS_LINUX)
#include "graphics/backends/VulkanGraphicsDeviceDomain.h"
#endif

void GraphicsBackendFactory::configureQtQuick() {
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#elif defined(Q_OS_MACOS)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#elif defined(Q_OS_LINUX)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
#else
    qCFatal(
        sunroomLogGraphics,
        "Sunroom does not provide a graphics backend for this platform");
#endif
}

QSurface::SurfaceType GraphicsBackendFactory::windowSurfaceType() {
#ifdef Q_OS_WIN
    return QSurface::Direct3DSurface;
#elif defined(Q_OS_MACOS)
    return QSurface::MetalSurface;
#elif defined(Q_OS_LINUX)
    return QSurface::VulkanSurface;
#else
    qCFatal(
        sunroomLogGraphics,
        "Sunroom does not provide a window surface for this platform");
    return QSurface::RasterSurface;
#endif
}

std::unique_ptr<GraphicsDeviceDomain>
GraphicsBackendFactory::createDeviceDomain(QWindow &window) {
#ifdef Q_OS_WIN
    Q_UNUSED(window);
    return createDeviceDomain();
#elif defined(Q_OS_MACOS)
    Q_UNUSED(window);
    return createDeviceDomain();
#elif defined(Q_OS_LINUX)
    return createVulkanGraphicsDeviceDomain(window);
#else
    Q_UNUSED(window);
    qCCritical(
        sunroomLogGraphics,
        "Sunroom does not provide a graphics device for this platform");
    return {};
#endif
}

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
std::unique_ptr<GraphicsDeviceDomain>
GraphicsBackendFactory::createDeviceDomain() {
#ifdef Q_OS_WIN
    return createD3D11GraphicsDeviceDomain();
#else
    return createMetalGraphicsDeviceDomain();
#endif
}
#endif
