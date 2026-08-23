#include "presentation/QuickUiLayer.h"

#include <cmath>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QGuiApplication>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QVariant>
#include <QWindow>
#include <rhi/qrhi.h>

#include "app/PresentationSettings.h"
#include "app/SupportController.h"
#include "app/VideoViewportState.h"
#include "diagnostics/LogCategories.h"
#include "playback/MediaSession.h"
#include "presentation/PresentationOutputState.h"
#include "video/ActiveVideoSource.h"
#include "video/DiagnosticVideoSource.h"

namespace {
class RenderControl final : public QQuickRenderControl {
  public:
    explicit RenderControl(QWindow& window) : m_window(window) {}

    QWindow* renderWindow(QPoint* offset) override {
        if (offset) {
            *offset = {};
        }
        return &m_window;
    }

  private:
    QWindow& m_window;
};
} // namespace

QuickUiLayer::QuickUiLayer(QWindow& renderWindow, QRhi& rhi, PresentationOutputState& outputState,
                           PresentationSettings& settings, DiagnosticVideoSource& diagnosticSource,
                           MediaSession& mediaSession, ActiveVideoSource& activeVideoSource,
                           VideoViewportState& videoViewport, SupportController& supportController, QObject* parent)
    : QObject(parent), m_renderWindow(renderWindow), m_rhi(rhi), m_outputState(outputState), m_settings(settings),
      m_diagnosticSource(diagnosticSource), m_mediaSession(mediaSession), m_activeVideoSource(activeVideoSource),
      m_videoViewport(videoViewport), m_supportController(supportController) {}

QuickUiLayer::~QuickUiLayer() {
    Q_ASSERT(m_renderControl);
    // invalidate() may emit render requests; teardown must not schedule frames.
    disconnect(m_renderControl.get(), nullptr, this, nullptr);
    // Qt also permits this after initialize() failed; it checks its own state.
    m_renderControl->invalidate();
    m_rootItem.reset();
    m_qmlEngine.reset();
    releaseRenderTarget();
    m_quickWindow.reset();
    m_renderControl.reset();
}

QuickUiLayer::InitializationResult QuickUiLayer::initialize() {
    Q_ASSERT(!m_renderControl);
    Q_ASSERT(!m_quickWindow);
    Q_ASSERT(!m_qmlEngine);
    Q_ASSERT(!m_rootItem);

    m_renderControl = std::make_unique<RenderControl>(m_renderWindow);
    connect(m_renderControl.get(), &QQuickRenderControl::renderRequested, this, &QuickUiLayer::markDirty);
    connect(m_renderControl.get(), &QQuickRenderControl::sceneChanged, this, &QuickUiLayer::markDirty);

    m_quickWindow = std::make_unique<QQuickWindow>(m_renderControl.get());
    m_quickWindow->setColor(Qt::transparent);
#if QT_CONFIG(vulkan)
    if (m_renderWindow.surfaceType() == QSurface::VulkanSurface) {
        Q_ASSERT(m_renderWindow.vulkanInstance());
        m_quickWindow->setVulkanInstance(m_renderWindow.vulkanInstance());
    }
#endif
    m_quickWindow->setGraphicsDevice(QQuickGraphicsDevice::fromRhi(&m_rhi));

    m_qmlEngine = std::make_unique<QQmlEngine>();

    QQmlComponent component(m_qmlEngine.get());
    component.loadFromModule(QStringLiteral("SunPlayer"), QStringLiteral("Main"));
    if (component.isError()) {
        qCFatal(sunplayerLogPresentation, "Could not load the packaged SunPlayer QML component:\n%s",
                qPrintable(component.errorString()));
    }

    QVariantMap const initialProperties{
        {
            QStringLiteral("renderDevicePixelRatio"),
            m_renderWindow.devicePixelRatio(),
        },
        {
            QStringLiteral("applicationDisplayName"),
            QGuiApplication::applicationDisplayName(),
        },
        {
            QStringLiteral("windowCommands"),
            QVariant::fromValue(&m_renderWindow),
        },
        {
            QStringLiteral("presentationOutput"),
            QVariant::fromValue(&m_outputState),
        },
        {
            QStringLiteral("presentationPolicy"),
            QVariant::fromValue(&m_settings),
        },
        {
            QStringLiteral("diagnosticSource"),
            QVariant::fromValue(&m_diagnosticSource),
        },
        {
            QStringLiteral("mediaSession"),
            QVariant::fromValue(&m_mediaSession),
        },
        {
            QStringLiteral("activeVideoSource"),
            QVariant::fromValue(&m_activeVideoSource),
        },
        {
            QStringLiteral("viewportState"),
            QVariant::fromValue(&m_videoViewport),
        },
        {
            QStringLiteral("supportController"),
            QVariant::fromValue(&m_supportController),
        },
    };
    QObject* object = component.createWithInitialProperties(initialProperties);
    m_rootItem.reset(qobject_cast<QQuickItem*>(object));
    if (!m_rootItem) {
        delete object;
        qCFatal(sunplayerLogPresentation, "SunPlayer Main.qml must create a QQuickItem root:\n%s",
                qPrintable(component.errorString()));
    }

    m_rootItem->setParentItem(m_quickWindow->contentItem());
    m_rootItem->forceActiveFocus();
    if (!m_renderControl->initialize()) {
        if (m_rhi.isDeviceLost()) {
            return InitializationResult::DeviceLost;
        }
        qCCritical(sunplayerLogPresentation, "Could not initialize redirected Qt Quick rendering");
        return InitializationResult::Unavailable;
    }
    return InitializationResult::Ready;
}

void QuickUiLayer::setLogicalSize(QSize const& size) {
    Q_ASSERT(m_quickWindow);
    Q_ASSERT(m_rootItem);
    Q_ASSERT(!size.isEmpty());
    if (m_logicalSize == size) {
        return;
    }
    m_logicalSize = size;
    m_quickWindow->setGeometry(0, 0, size.width(), size.height());
    m_quickWindow->contentItem()->setSize(size);
    m_rootItem->setSize(size);
    markDirty();
}

QuickUiLayer::RenderTargetUpdate QuickUiLayer::ensureRenderTarget(QSize const& pixelSize, qreal devicePixelRatio) {
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(std::isfinite(devicePixelRatio) && devicePixelRatio > 0.0);

    if (m_texture && m_pixelSize == pixelSize) {
        Q_ASSERT(m_depthStencilBuffer);
        Q_ASSERT(m_renderTarget);
        Q_ASSERT(m_renderPassDescriptor);
        if (!qFuzzyCompare(m_devicePixelRatio, devicePixelRatio)) {
            m_devicePixelRatio = devicePixelRatio;
            if (!m_rootItem->setProperty("renderDevicePixelRatio", devicePixelRatio)) {
                qCFatal(sunplayerLogPresentation, "SunPlayer Main.qml must expose renderDevicePixelRatio");
            }
            markDirty();
        }
        return RenderTargetUpdate::Unchanged;
    }

    releaseRenderTarget();
    m_texture.reset(m_rhi.newTexture(QRhiTexture::RGBA16F, pixelSize, 1, QRhiTexture::RenderTarget));
    if (!m_texture->create()) {
        if (m_rhi.isDeviceLost()) {
            releaseRenderTarget();
            return RenderTargetUpdate::DeviceLost;
        }
        qCCritical(sunplayerLogPresentation, "Could not create the Qt Quick FP16 texture");
        return RenderTargetUpdate::Unavailable;
    }

    // fromRhiRenderTarget() adopts this target as-is. Qt Quick's default 2D
    // renderer uses depth to preserve front-to-back opaque scene ordering.
    m_depthStencilBuffer.reset(m_rhi.newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!m_depthStencilBuffer->create()) {
        if (m_rhi.isDeviceLost()) {
            releaseRenderTarget();
            return RenderTargetUpdate::DeviceLost;
        }
        qCCritical(sunplayerLogPresentation, "Could not create the Qt Quick depth/stencil buffer");
        releaseRenderTarget();
        return RenderTargetUpdate::Unavailable;
    }

    QRhiTextureRenderTargetDescription description(QRhiColorAttachment(m_texture.get()));
    description.setDepthStencilBuffer(m_depthStencilBuffer.get());
    m_renderTarget.reset(m_rhi.newTextureRenderTarget(description));
    m_renderPassDescriptor.reset(m_renderTarget->newCompatibleRenderPassDescriptor());
    m_renderTarget->setRenderPassDescriptor(m_renderPassDescriptor.get());
    if (!m_renderTarget->create()) {
        if (m_rhi.isDeviceLost()) {
            releaseRenderTarget();
            return RenderTargetUpdate::DeviceLost;
        }
        qCCritical(sunplayerLogPresentation, "Could not create the Qt Quick FP16 render target");
        releaseRenderTarget();
        return RenderTargetUpdate::Unavailable;
    }

    m_pixelSize = pixelSize;
    m_devicePixelRatio = devicePixelRatio;
    if (!m_rootItem->setProperty("renderDevicePixelRatio", devicePixelRatio)) {
        qCFatal(sunplayerLogPresentation, "SunPlayer Main.qml must expose renderDevicePixelRatio");
    }
    configureRenderTarget();
    markDirty();
    return RenderTargetUpdate::Recreated;
}

void QuickUiLayer::renderIfDirty() {
    if (!m_dirty) {
        return;
    }
    Q_ASSERT(m_renderControl);
    Q_ASSERT(m_quickWindow);
    Q_ASSERT(m_rootItem);
    Q_ASSERT(m_depthStencilBuffer);
    Q_ASSERT(m_renderTarget);

    m_dirty = false;
    m_renderControl->polishItems();
    m_renderControl->beginFrame();
    m_renderControl->sync();
    m_renderControl->render();
    m_renderControl->endFrame();
}

void QuickUiLayer::markDirty() {
    if (m_dirty) {
        return;
    }
    m_dirty = true;
    emit updateRequested();
}

bool QuickUiLayer::isDirty() const { return m_dirty; }
QRhiTexture& QuickUiLayer::texture() const {
    Q_ASSERT(m_texture);
    return *m_texture;
}
QQuickWindow* QuickUiLayer::quickWindow() const { return m_quickWindow.get(); }

void QuickUiLayer::configureRenderTarget() {
    Q_ASSERT(m_renderTarget);
    Q_ASSERT(m_quickWindow);

    QQuickRenderTarget quickTarget = QQuickRenderTarget::fromRhiRenderTarget(m_renderTarget.get());
    // renderWindow() supplies DPR. Setting it on the target would be ignored.
    // This normalizes Quick's texture origin; the final pass separately maps
    // the backend's NDC orientation.
    quickTarget.setMirrorVertically(m_rhi.isYUpInFramebuffer());
    m_quickWindow->setRenderTarget(quickTarget);
}

void QuickUiLayer::releaseRenderTarget() {
    Q_ASSERT(m_quickWindow);
    m_quickWindow->setRenderTarget({});
    m_renderTarget.reset();
    m_renderPassDescriptor.reset();
    m_depthStencilBuffer.reset();
    m_texture.reset();
    m_pixelSize = {};
    m_devicePixelRatio = 0.0;
}
