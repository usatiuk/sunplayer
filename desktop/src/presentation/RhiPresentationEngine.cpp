#include "presentation/RhiPresentationEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QWindow>
#include <rhi/qrhi.h>

#include "app/PresentationSettings.h"
#include "presentation/DiagnosticVideoProducer.h"
#include "presentation/HdrCompositor.h"
#include "presentation/PresentationOutputState.h"
#include "presentation/QuickUiLayer.h"
#include "presentation/RenderedVideoSurface.h"

namespace {
constexpr float scRgbReferenceWhiteNits = 80.0f;

float positiveOrFallback(float value, float fallback, const char *name) {
    if (std::isfinite(value) && value > 0.0f)
        return value;
    qWarning("QRhi reported invalid %s: %g; using %g",
             name, value, fallback);
    return fallback;
}

float nonNegativeOrZero(float value, const char *name) {
    if (std::isfinite(value) && value >= 0.0f)
        return value;
    qWarning("QRhi reported invalid %s: %g; using 0",
             name, value);
    return 0.0f;
}

void advanceRevision(std::uint64_t &revision) {
    ++revision;
    if (revision == 0)
        ++revision;
}
}

RhiPresentationEngine::RhiPresentationEngine(
        QWindow &window,
        PresentationOutputState &outputState,
        PresentationSettings &settings,
        QObject *parent)
    : QObject(parent),
      m_window(window),
      m_outputState(outputState),
      m_settings(settings) {
    m_outputVerificationTimer.setSingleShot(true);
    m_outputVerificationTimer.setInterval(std::chrono::milliseconds(100));
    m_deviceRecoveryTimer.setSingleShot(true);

    connect(&m_settings, &PresentationSettings::settingsChanged,
            this, &RhiPresentationEngine::markCanvasDirty);
    connect(&m_outputState, &PresentationOutputState::stateChanged,
            this, &RhiPresentationEngine::markPresentationDirty);
    connect(&m_outputState,
            &PresentationOutputState::outputCharacteristicsChanged,
            this, [this] {
                requestSwapChainRecreation();
                scheduleOutputVerification();
            });
    connect(&m_window, &QWindow::screenChanged, this, [this] {
        requestSwapChainRecreation();
        scheduleOutputVerification();
    });
    connect(&m_window, &QWindow::xChanged,
            this, &RhiPresentationEngine::scheduleOutputVerification);
    connect(&m_window, &QWindow::yChanged,
            this, &RhiPresentationEngine::scheduleOutputVerification);
    connect(&m_outputVerificationTimer, &QTimer::timeout,
            this, &RhiPresentationEngine::verifyOutput);
    connect(&m_deviceRecoveryTimer, &QTimer::timeout,
            this, &RhiPresentationEngine::requestFrame);
}

RhiPresentationEngine::~RhiPresentationEngine() {
    releaseDevice();
}

void RhiPresentationEngine::render() {
    Q_ASSERT(!m_rendering);

    m_framePending = false;
    m_frameRequestedWhileRendering = false;
    m_rendering = true;
    renderFrame();
    m_rendering = false;

    if (m_frameRequestedWhileRendering)
        requestFrame();
}

void RhiPresentationEngine::renderFrame() {
    if (!m_window.isExposed() || m_window.size().isEmpty())
        return;

    if (!m_rhi && !initializeDevice())
        return;
    if (m_recreateSwapChain) {
        releaseSwapChain();
        m_recreateSwapChain = false;
    }
    if (!m_swapChain && !createSwapChain())
        return;
    if (!resizeSwapChain())
        return;

    m_quickUi->setLogicalSize(m_window.size());
    const QSize pixelSize = m_swapChain->currentPixelSize();
    const QuickUiLayer::RenderTargetUpdate targetUpdate =
        m_quickUi->ensureRenderTarget(
            pixelSize, m_window.devicePixelRatio());
    if (targetUpdate == QuickUiLayer::RenderTargetUpdate::DeviceLost) {
        handleDeviceLoss("creating the Qt Quick render target");
        return;
    }

    // QQuickRenderControl uses a separate offscreen QRhi frame. It must be
    // completed before beginning the visible swapchain frame.
    m_quickUi->renderIfDirty();
    if (m_rhi->isDeviceLost()) {
        handleDeviceLoss("rendering the Qt Quick layer");
        return;
    }

    if (m_settings.animatePattern()) {
        const auto now = std::chrono::steady_clock::now();
        if (m_lastAnimationFrame) {
            const float delta =
                std::chrono::duration<float>(now - *m_lastAnimationFrame).count()
                / 8.0f;
            m_phase = std::fmod(m_phase + delta, 1.0f);
            advanceRevision(m_videoContentRevision);
        }
        m_lastAnimationFrame = now;
    } else {
        m_lastAnimationFrame.reset();
    }

    const float scaleX = static_cast<float>(pixelSize.width())
        / static_cast<float>(m_window.width());
    const float scaleY = static_cast<float>(pixelSize.height())
        / static_cast<float>(m_window.height());
    const QRectF canvas = m_settings.canvasRect();
    Q_ASSERT(std::isfinite(scaleX) && scaleX > 0.0f);
    Q_ASSERT(std::isfinite(scaleY) && scaleY > 0.0f);
    Q_ASSERT(canvas.width() >= 1.0 && canvas.height() >= 1.0);

    const QRectF scaledCanvas(
        canvas.x() * scaleX,
        canvas.y() * scaleY,
        canvas.width() * scaleX,
        canvas.height() * scaleY);
    const QRect videoRect = scaledCanvas.toAlignedRect().intersected(
        QRect(QPoint{}, pixelSize));
    Q_ASSERT(!videoRect.isEmpty());

    const float targetPeak = m_settings.automaticTargetPeak()
        ? m_outputState.effectiveTargetHeadroom()
        : m_settings.manualTargetHeadroom();
    Q_ASSERT(std::isfinite(targetPeak) && targetPeak >= 1.0f);
    const float referenceWhiteNits = m_outputState.sdrWhiteKnown()
        ? m_outputState.sdrWhiteNits()
        : scRgbReferenceWhiteNits;
    Q_ASSERT(
        std::isfinite(referenceWhiteNits) && referenceWhiteNits > 0.0f);

    RenderedVideoSurfaceState requestedSurface;
    requestedSurface.description.pixelSize = videoRect.size();
    requestedSurface.description.pixelFormat =
        RenderedVideoPixelFormat::Rgba16Float;
    requestedSurface.description.colorSpace =
        RenderedVideoColorSpace::LinearSrgb;
    requestedSurface.description.luminance =
        RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative;
    requestedSurface.description.alphaMode =
        RenderedVideoAlphaMode::Opaque;
    requestedSurface.description.referenceWhiteNits = referenceWhiteNits;
    requestedSurface.description.targetPeakHeadroom = targetPeak;
    requestedSurface.graphicsDeviceGeneration = m_deviceGeneration;
    requestedSurface.displayTargetRevision =
        m_outputState.displayTargetRevision();
    requestedSurface.contentRevision = m_videoContentRevision;
    Q_ASSERT(requestedSurface.isValid());

    Q_ASSERT(m_videoProducer);
    if (m_videoProducer->ensureSurface(requestedSurface)
            == DiagnosticVideoProducer::ResourceResult::DeviceLost) {
        handleDeviceLoss("creating the diagnostic video surface");
        return;
    }

    if (!m_compositor) {
        m_compositor = std::make_unique<HdrCompositor>(*m_rhi);
        if (m_compositor->initialize(
                *m_renderPassDescriptor,
                m_videoProducer->texture(),
                m_quickUi->texture())
                == HdrCompositor::ResourceResult::DeviceLost) {
            handleDeviceLoss("creating the HDR compositor");
            return;
        }
    } else if (targetUpdate
               == QuickUiLayer::RenderTargetUpdate::Recreated) {
        if (m_compositor->setTextures(
                m_videoProducer->texture(), m_quickUi->texture())
                == HdrCompositor::ResourceResult::DeviceLost) {
            handleDeviceLoss("rebinding compositor layer textures");
            return;
        }
    }

    QRhi::FrameOpResult result = m_rhi->beginFrame(m_swapChain.get());
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        // QRhi requires createOrResize() even when the pixel size is unchanged.
        if (!resizeSwapChain(true))
            return;
        result = m_rhi->beginFrame(m_swapChain.get());
    }
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        if (resizeSwapChain(true))
            requestFrame();
        return;
    }
    if (result == QRhi::FrameOpDeviceLost) {
        handleDeviceLoss("beginning a frame");
        return;
    }
    if (result != QRhi::FrameOpSuccess) {
        handleFrameError("beginning a frame", static_cast<int>(result));
        return;
    }

    QRhiCommandBuffer &commandBuffer =
        *m_swapChain->currentFrameCommandBuffer();
    if (m_videoProducer->needsRender(requestedSurface)) {
        DiagnosticVideoParameters videoParameters;
        videoParameters.sourcePeak = m_settings.sourcePeakHeadroom();
        videoParameters.targetPeak = targetPeak;
        videoParameters.phase = m_phase;
        videoParameters.toneMappingEnabled =
            m_settings.toneMappingEnabled() ? 1.0f : 0.0f;
        videoParameters.canonicalYFlip =
            m_rhi->isYUpInNDC() != m_rhi->isYUpInFramebuffer()
            ? 1.0f
            : 0.0f;
        m_videoProducer->render(
            commandBuffer, videoParameters, requestedSurface);
    }

    const float sdrScale = m_outputState.sdrScale();
    Q_ASSERT(std::isfinite(sdrScale) && sdrScale > 0.0f);
    HdrCompositorParameters parameters;
    parameters.viewportSize = {
        static_cast<float>(pixelSize.width()),
        static_cast<float>(pixelSize.height()),
    };
    parameters.videoOrigin = {
        static_cast<float>(videoRect.x()),
        static_cast<float>(videoRect.y()),
    };
    parameters.videoSize = {
        static_cast<float>(videoRect.width()),
        static_cast<float>(videoRect.height()),
    };
    parameters.sdrScale = sdrScale;
    parameters.ndcYUp = m_rhi->isYUpInNDC() ? 1.0f : 0.0f;
    // Final encoding follows the successfully created presentation path, not
    // asynchronous OS HDR metadata that may already describe another output.
    parameters.linearOutput =
        m_outputState.extendedLinearActive() ? 1.0f : 0.0f;

    Q_ASSERT(m_compositor);
    m_compositor->render(
        commandBuffer,
        *m_swapChain->currentFrameRenderTarget(),
        pixelSize,
        parameters);

    result = m_rhi->endFrame(m_swapChain.get());
    if (result == QRhi::FrameOpSuccess)
        m_videoProducer->commitPendingRender();
    else
        m_videoProducer->discardPendingRender();
    if (result == QRhi::FrameOpDeviceLost) {
        handleDeviceLoss("presenting a frame");
        return;
    }
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        if (resizeSwapChain(true))
            requestFrame();
        return;
    }
    if (result != QRhi::FrameOpSuccess) {
        handleFrameError("ending a frame", static_cast<int>(result));
        return;
    }

    m_retriedFrameError = false;
    scheduleNextFrame();
}

void RhiPresentationEngine::requestFrame() {
    if (!m_window.isExposed())
        return;
    if (m_rendering) {
        m_frameRequestedWhileRendering = true;
        return;
    }
    if (m_framePending)
        return;
    m_framePending = true;
    m_window.requestUpdate();
}

void RhiPresentationEngine::handleExposure() {
    if (!m_window.isExposed()) {
        // Qt may discard a queued update while the native surface is hidden.
        // It cannot satisfy a future exposure once the surface has changed.
        m_framePending = false;
        return;
    }
    if (m_window.size().isEmpty()) {
        m_framePending = false;
        return;
    }

    // The first presentation must happen before requestUpdate() can switch to
    // DXGI's vsync service. Scheduling before the swapchain exists can leave
    // both Qt's timer path and its DXGI path trying to deliver one request.
    if (!m_framePending)
        render();
}

void RhiPresentationEngine::markUiDirty() {
    if (m_quickUi)
        m_quickUi->markDirty();
    requestFrame();
}

void RhiPresentationEngine::markCanvasDirty() {
    advanceRevision(m_videoContentRevision);
    requestFrame();
}

void RhiPresentationEngine::markPresentationDirty() {
    requestFrame();
}

void RhiPresentationEngine::releaseSwapChain() {
    m_framePending = false;
    // The pipeline and swapchain depend on this render-pass descriptor.
    // The independent Quick and video textures intentionally survive it.
    m_compositor.reset();
    if (m_swapChain)
        m_swapChain->destroy();
    m_swapChain.reset();
    m_renderPassDescriptor.reset();
    m_swapChainScreen = nullptr;
}

QQuickWindow *RhiPresentationEngine::quickWindow() const {
    return m_quickUi ? m_quickUi->quickWindow() : nullptr;
}

bool RhiPresentationEngine::initializeDevice() {
    Q_ASSERT(!m_rhi);
    Q_ASSERT(!m_quickUi);
#ifdef Q_OS_WIN
    QRhiD3D11InitParams parameters;
    m_rhi.reset(QRhi::create(QRhi::D3D11, &parameters));
#else
    qFatal("Sunroom does not provide a QRhi backend for this platform");
#endif
    if (!m_rhi && !m_recoveringDevice)
        qFatal("Could not create the QRhi backend");
    if (!m_rhi) {
        scheduleDeviceRecovery();
        return false;
    }
    advanceRevision(m_deviceGeneration);

    m_quickUi = std::make_unique<QuickUiLayer>(
        m_window, *m_rhi, m_outputState, m_settings);
    connect(m_quickUi.get(), &QuickUiLayer::updateRequested,
            this, &RhiPresentationEngine::requestFrame);
    if (m_quickUi->initialize()
            == QuickUiLayer::InitializationResult::DeviceLost) {
        handleDeviceLoss("initializing Qt Quick");
        return false;
    }
    m_videoProducer = std::make_unique<DiagnosticVideoProducer>(*m_rhi);
    m_recoveringDevice = false;
    m_deviceRecoveryAttempts = 0;
    m_deviceRecoveryTimer.stop();
    return true;
}

bool RhiPresentationEngine::createSwapChain() {
    Q_ASSERT(m_rhi);
    Q_ASSERT(!m_swapChain);
    Q_ASSERT(!m_renderPassDescriptor);
    Q_ASSERT(!m_compositor);

    m_swapChain.reset(m_rhi->newSwapChain());
    m_swapChain->setWindow(&m_window);

    const bool extendedLinearSupported =
        m_swapChain->isFormatSupported(QRhiSwapChain::HDRExtendedSrgbLinear);
    m_swapChain->setFormat(extendedLinearSupported
        ? QRhiSwapChain::HDRExtendedSrgbLinear
        : QRhiSwapChain::SDR);
    m_renderPassDescriptor.reset(
        m_swapChain->newCompatibleRenderPassDescriptor());
    m_swapChain->setRenderPassDescriptor(m_renderPassDescriptor.get());
    if (!m_swapChain->createOrResize()) {
        const bool deviceLost = m_rhi->isDeviceLost();
        m_swapChain.reset();
        m_renderPassDescriptor.reset();
        if (deviceLost)
            handleDeviceLoss("creating the swapchain");
        else
            qWarning("Could not create the QRhi swapchain; waiting for another window update");
        return false;
    }
    updateBackendState();
    return true;
}

bool RhiPresentationEngine::resizeSwapChain(bool force) {
    Q_ASSERT(m_swapChain);
    Q_ASSERT(m_renderPassDescriptor);
    const QSize surfaceSize = m_swapChain->surfacePixelSize();
    if (surfaceSize.isEmpty())
        return false;
    if (!force && m_swapChain->currentPixelSize() == surfaceSize)
        return true;

    return createOrResizeSwapChain("resize");
}

bool RhiPresentationEngine::createOrResizeSwapChain(const char *operation) {
    Q_ASSERT(m_swapChain);
    if (m_swapChain->createOrResize()) {
        updateBackendState();
        return true;
    }

    if (m_rhi->isDeviceLost()) {
        handleDeviceLoss(operation);
    } else {
        qWarning("Could not %s the QRhi swapchain; waiting for another window update",
                 operation);
        releaseSwapChain();
    }
    return false;
}

void RhiPresentationEngine::releaseDevice() {
    // Every child resource must be gone before destroying the QRhi.
    releaseSwapChain();
    m_videoProducer.reset();
    // Quick invalidation may emit updateRequested while it tears down.
    if (m_quickUi)
        disconnect(m_quickUi.get(), nullptr, this, nullptr);
    m_quickUi.reset();
    m_rhi.reset();
    m_recreateSwapChain = false;
    m_lastAnimationFrame.reset();
}

void RhiPresentationEngine::handleDeviceLoss(const char *operation) {
    qWarning("QRhi device was lost while %s; retrying", operation);
    if (!m_recoveringDevice)
        m_deviceRecoveryAttempts = 0;
    m_recoveringDevice = true;
    releaseDevice();
    scheduleDeviceRecovery();
}

void RhiPresentationEngine::handleFrameError(
        const char *operation, int result) {
    if (m_retriedFrameError) {
        qFatal("QRhi failed twice while %s: %d", operation, result);
    }
    qWarning("QRhi failed while %s: %d; rebuilding once",
             operation, result);
    m_retriedFrameError = true;
    m_recoveringDevice = true;
    m_deviceRecoveryAttempts = 0;
    releaseDevice();
    scheduleDeviceRecovery();
}

void RhiPresentationEngine::scheduleDeviceRecovery() {
    constexpr int maximumAttempts = 8;
    constexpr auto retryDelay = std::chrono::milliseconds(250);

    if (m_deviceRecoveryAttempts >= maximumAttempts) {
        qFatal("QRhi device recovery failed after %d attempts",
               maximumAttempts);
    }

    ++m_deviceRecoveryAttempts;
    qWarning("Retrying QRhi device recovery (%d/%d)",
             m_deviceRecoveryAttempts, maximumAttempts);
    m_deviceRecoveryTimer.start(retryDelay);
}

void RhiPresentationEngine::updateBackendState() {
    m_swapChainScreen =
        QGuiApplication::screenAt(m_window.geometry().center());
    if (!m_swapChainScreen)
        m_swapChainScreen = m_window.screen();

    PresentationBackendState state;
    state.graphicsApi = QString::fromLatin1(m_rhi->backendName());
    const bool extendedLinearSupported =
        m_swapChain->isFormatSupported(QRhiSwapChain::HDRExtendedSrgbLinear);
    state.extendedLinearActive =
        extendedLinearSupported
        && m_swapChain->format() == QRhiSwapChain::HDRExtendedSrgbLinear;
    state.swapChainFormat =
        state.extendedLinearActive
        ? QStringLiteral("scRGB / extended linear sRGB")
        : QStringLiteral("SDR / sRGB");
    state.videoSurfaceFormat =
        QStringLiteral("RGBA16F · linear sRGB · SDR-white-relative");
    state.videoSurfaceProducer = QStringLiteral("Diagnostic pattern");

    const QRhiSwapChainHdrInfo info = m_swapChain->hdrInfo();
    state.sceneReferred =
        info.luminanceBehavior == QRhiSwapChainHdrInfo::SceneReferred;
    state.sdrWhiteKnown =
        std::isfinite(info.sdrWhiteLevel) && info.sdrWhiteLevel > 0.0f;
    state.sdrWhiteNits = positiveOrFallback(
        info.sdrWhiteLevel, 80.0f, "SDR white level");
    if (info.limitsType == QRhiSwapChainHdrInfo::LuminanceInNits) {
        state.luminanceKnown =
            std::isfinite(info.limits.luminanceInNits.maxLuminance)
            && info.limits.luminanceInNits.maxLuminance > 0.0f;
        state.minLuminanceNits = nonNegativeOrZero(
            info.limits.luminanceInNits.minLuminance,
            "minimum luminance");
        state.maxLuminanceNits = nonNegativeOrZero(
            info.limits.luminanceInNits.maxLuminance,
            "maximum luminance");
    } else {
        state.currentHeadroom = positiveOrFallback(
            info.limits.colorComponentValue.maxColorComponentValue,
            1.0f, "current headroom");
        state.potentialHeadroom = positiveOrFallback(
            info.limits.colorComponentValue.maxPotentialColorComponentValue,
            state.currentHeadroom, "potential headroom");
    }
    m_outputState.setBackendState(state);
}

void RhiPresentationEngine::scheduleNextFrame() {
    if (m_settings.animatePattern() || m_quickUi->isDirty()) {
        requestFrame();
    }
}

void RhiPresentationEngine::requestSwapChainRecreation() {
    // Display callbacks may occur during Quick synchronization. Defer all QRhi
    // resource mutation to the next engine-owned render point.
    m_recreateSwapChain = true;
    markPresentationDirty();
}

void RhiPresentationEngine::scheduleOutputVerification() {
    m_outputVerificationTimer.start();
}

void RhiPresentationEngine::verifyOutput() {
    QScreen *screen =
        QGuiApplication::screenAt(m_window.geometry().center());
    if (!screen)
        screen = m_window.screen();
    if (screen != m_swapChainScreen)
        requestSwapChainRecreation();
}
