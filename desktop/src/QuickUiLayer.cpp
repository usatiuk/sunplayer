#include "QuickUiLayer.h"

#include <cmath>

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QWindow>
#include <rhi/qrhi.h>

#include "PresentationOutputState.h"
#include "PresentationSettings.h"

namespace {
class RenderControl final : public QQuickRenderControl {
public:
    explicit RenderControl(QWindow &window) : m_window(window) {}

    QWindow *renderWindow(QPoint *offset) override {
        if (offset)
            *offset = {};
        return &m_window;
    }

private:
    QWindow &m_window;
};
}

QuickUiLayer::QuickUiLayer(QWindow &renderWindow,
                           QRhi &rhi,
                           PresentationOutputState &outputState,
                           PresentationSettings &settings,
                           QObject *parent)
    : QObject(parent),
      m_renderWindow(renderWindow),
      m_rhi(rhi),
      m_outputState(outputState),
      m_settings(settings) {
}

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
    connect(m_renderControl.get(), &QQuickRenderControl::renderRequested,
            this, &QuickUiLayer::markDirty);
    connect(m_renderControl.get(), &QQuickRenderControl::sceneChanged,
            this, &QuickUiLayer::markDirty);

    m_quickWindow = std::make_unique<QQuickWindow>(m_renderControl.get());
    m_quickWindow->setColor(Qt::transparent);
    m_quickWindow->setGraphicsDevice(QQuickGraphicsDevice::fromRhi(&m_rhi));

    m_qmlEngine = std::make_unique<QQmlEngine>();
    m_qmlEngine->rootContext()->setContextProperty(
        QStringLiteral("outputState"), &m_outputState);
    m_qmlEngine->rootContext()->setContextProperty(
        QStringLiteral("presentationSettings"), &m_settings);

    QQmlComponent component(m_qmlEngine.get());
    component.loadFromModule(QStringLiteral("Sunroom"), QStringLiteral("Main"));
    if (component.isError()) {
        for (const auto &error : component.errors())
            qWarning() << error;
        qFatal("Could not load the packaged Sunroom QML component");
    }

    QObject *object = component.create();
    m_rootItem.reset(qobject_cast<QQuickItem *>(object));
    if (!m_rootItem) {
        delete object;
        qFatal("Sunroom Main.qml must create a QQuickItem root");
    }

    m_rootItem->setParentItem(m_quickWindow->contentItem());
    m_rootItem->forceActiveFocus();
    if (!m_renderControl->initialize()) {
        if (m_rhi.isDeviceLost())
            return InitializationResult::DeviceLost;
        qFatal("Could not initialize redirected Qt Quick rendering");
    }
    return InitializationResult::Ready;
}

void QuickUiLayer::setLogicalSize(const QSize &size) {
    Q_ASSERT(m_quickWindow);
    Q_ASSERT(m_rootItem);
    Q_ASSERT(!size.isEmpty());
    if (m_logicalSize == size)
        return;
    m_logicalSize = size;
    m_quickWindow->setGeometry(0, 0, size.width(), size.height());
    m_quickWindow->contentItem()->setSize(size);
    m_rootItem->setSize(size);
    markDirty();
}

QuickUiLayer::RenderTargetUpdate QuickUiLayer::ensureRenderTarget(
        const QSize &pixelSize, qreal devicePixelRatio) {
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(std::isfinite(devicePixelRatio) && devicePixelRatio > 0.0);

    if (m_texture && m_pixelSize == pixelSize) {
        Q_ASSERT(m_renderTarget);
        Q_ASSERT(m_renderPassDescriptor);
        if (!qFuzzyCompare(m_devicePixelRatio, devicePixelRatio)) {
            m_devicePixelRatio = devicePixelRatio;
            markDirty();
        }
        return RenderTargetUpdate::Unchanged;
    }

    releaseRenderTarget();
    m_texture.reset(m_rhi.newTexture(
        QRhiTexture::RGBA16F, pixelSize, 1, QRhiTexture::RenderTarget));
    if (!m_texture->create()) {
        if (m_rhi.isDeviceLost()) {
            releaseRenderTarget();
            return RenderTargetUpdate::DeviceLost;
        }
        qFatal("Could not create the Qt Quick FP16 texture");
    }

    const QRhiTextureRenderTargetDescription description(
        QRhiColorAttachment(m_texture.get()));
    m_renderTarget.reset(m_rhi.newTextureRenderTarget(description));
    m_renderPassDescriptor.reset(
        m_renderTarget->newCompatibleRenderPassDescriptor());
    m_renderTarget->setRenderPassDescriptor(m_renderPassDescriptor.get());
    if (!m_renderTarget->create()) {
        if (m_rhi.isDeviceLost()) {
            releaseRenderTarget();
            return RenderTargetUpdate::DeviceLost;
        }
        qFatal("Could not create the Qt Quick FP16 render target");
    }

    m_pixelSize = pixelSize;
    m_devicePixelRatio = devicePixelRatio;
    configureRenderTarget();
    markDirty();
    return RenderTargetUpdate::Recreated;
}

void QuickUiLayer::renderIfDirty() {
    if (!m_dirty)
        return;
    Q_ASSERT(m_renderControl);
    Q_ASSERT(m_quickWindow);
    Q_ASSERT(m_rootItem);
    Q_ASSERT(m_renderTarget);

    m_dirty = false;
    m_renderControl->polishItems();
    m_renderControl->beginFrame();
    m_renderControl->sync();
    m_renderControl->render();
    m_renderControl->endFrame();
}

void QuickUiLayer::markDirty() {
    if (m_dirty)
        return;
    m_dirty = true;
    emit updateRequested();
}

bool QuickUiLayer::isDirty() const { return m_dirty; }
QRhiTexture &QuickUiLayer::texture() const {
    Q_ASSERT(m_texture);
    return *m_texture;
}
QQuickWindow *QuickUiLayer::quickWindow() const { return m_quickWindow.get(); }

void QuickUiLayer::configureRenderTarget() {
    Q_ASSERT(m_renderTarget);
    Q_ASSERT(m_quickWindow);

    QQuickRenderTarget quickTarget =
        QQuickRenderTarget::fromRhiRenderTarget(m_renderTarget.get());
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
    m_texture.reset();
    m_pixelSize = {};
    m_devicePixelRatio = 0.0;
}
