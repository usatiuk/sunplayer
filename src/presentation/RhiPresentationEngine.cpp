#include "presentation/RhiPresentationEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

#include <QQuickWindow>
#include <QWindow>
#include <QtCore/qscopeguard.h>
#include <rhi/qrhi.h>

#include "app/PresentationSettings.h"
#include "app/SupportController.h"
#include "app/VideoViewportState.h"
#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsBackendFactory.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "playback/MediaSession.h"
#include "presentation/HdrCompositor.h"
#include "presentation/PresentationOutputState.h"
#include "presentation/QuickUiLayer.h"
#include "presentation/SubtitleRenderer.h"
#include "presentation/VideoPresentationGeometry.h"
#include "video/ActiveVideoSource.h"
#include "video/DiagnosticVideoSource.h"
#include "video/RenderedVideoProducer.h"
#include "video/RenderedVideoSource.h"
#include "video/RenderedVideoSurface.h"
#include "video/VideoTargetInterop.h"

namespace {
constexpr float scRgbReferenceWhiteNits = 80.0f;

float positiveOrFallback(float value, float fallback, char const* name) {
    if (std::isfinite(value) && value > 0.0f) {
        return value;
    }
    qCWarning(sunplayerLogPresentation, "QRhi reported invalid %s: %g; using %g", name, value, fallback);
    return fallback;
}

float nonNegativeOrZero(float value, char const* name) {
    if (std::isfinite(value) && value >= 0.0f) {
        return value;
    }
    qCWarning(sunplayerLogPresentation, "QRhi reported invalid %s: %g; using 0", name, value);
    return 0.0f;
}

void advanceRevision(std::uint64_t& revision) {
    ++revision;
    if (revision == 0) {
        ++revision;
    }
}

QRhiSwapChain::Format desiredSwapChainFormat(PresentationSurfaceContract const& surfaceContract,
                                             QRhiSwapChain& swapChain, bool displayHdrAvailable,
                                             bool hdr10PlatformSupported) {
    if (surfaceContract.hdr10Required()) {
        return hdr10PlatformSupported && swapChain.isFormatSupported(QRhiSwapChain::HDR10) ? QRhiSwapChain::HDR10
                                                                                           : QRhiSwapChain::SDR;
    }
    if (surfaceContract.mode != PresentationSurfaceMode::AdaptiveExtendedLinear) {
        return QRhiSwapChain::SDR;
    }

    bool extendedLinearAllowed = true;
#ifdef Q_OS_MACOS
    extendedLinearAllowed = displayHdrAvailable;
#else
    Q_UNUSED(displayHdrAvailable);
#endif
    return extendedLinearAllowed && swapChain.isFormatSupported(QRhiSwapChain::HDRExtendedSrgbLinear)
               ? QRhiSwapChain::HDRExtendedSrgbLinear
               : QRhiSwapChain::SDR;
}
} // namespace

RhiPresentationEngine::RhiPresentationEngine(QWindow& window, PresentationOutputState& outputState,
                                             PresentationSettings& settings, ActiveVideoSource& videoSource,
                                             DiagnosticVideoSource& diagnosticSource, MediaSession& mediaSession,
                                             VideoViewportState& videoViewport, SupportController& supportController,
                                             PresentationSurfaceContract surfaceContract,
                                             PresentationSurfaceController* surfaceController, QObject* parent)
    : QObject(parent), m_window(window), m_outputState(outputState), m_settings(settings), m_videoSource(videoSource),
      m_diagnosticSource(diagnosticSource), m_mediaSession(mediaSession), m_videoViewport(videoViewport),
      m_supportController(supportController), m_surfaceContract(surfaceContract),
      m_surfaceController(surfaceController) {
    Q_ASSERT((m_surfaceController != nullptr) ==
             (m_surfaceContract.mode != PresentationSurfaceMode::AdaptiveExtendedLinear));
    m_deviceRecoveryTimer.setSingleShot(true);
    m_swapChainRecoveryTimer.setSingleShot(true);

    connect(&m_settings, &PresentationSettings::settingsChanged, this, &RhiPresentationEngine::requestFrame);
    connect(&m_videoSource, &RenderedVideoSource::updateRequested, this, &RhiPresentationEngine::requestFrame);
    connect(&m_videoViewport, &VideoViewportState::viewportChanged, this, &RhiPresentationEngine::requestFrame);
    connect(&m_outputState, &PresentationOutputState::stateChanged, this,
            &RhiPresentationEngine::markPresentationDirty);
    connect(&m_outputState, &PresentationOutputState::outputCharacteristicsChanged, this,
            &RhiPresentationEngine::markOutputCharacteristicsDirty);
#ifdef Q_OS_MACOS
    connect(&m_window, &QWindow::screenChanged, this, [this] {
        m_swapChainSurfaceDirty = true;
        markOutputCharacteristicsDirty();
    });
#endif
    connect(&m_deviceRecoveryTimer, &QTimer::timeout, this, &RhiPresentationEngine::requestFrame);
    connect(&m_swapChainRecoveryTimer, &QTimer::timeout, this, &RhiPresentationEngine::requestFrame);
    connect(&m_mediaSession, &MediaSession::subtitleChanged, this, &RhiPresentationEngine::requestFrame);
}

RhiPresentationEngine::~RhiPresentationEngine() {
    m_destroying = true;
    releaseDevice();
}

bool RhiPresentationEngine::start() {
    if (!initializeGraphicsDevice()) {
        fail(ApplicationError(ApplicationError::Code::PresentationInitializationFailed,
                              tr("SunPlayer could not initialize graphics presentation."),
                              QStringLiteral("GraphicsBackendFactory did not create a QRhi device domain")));
        return false;
    }
    return true;
}

void RhiPresentationEngine::render() {
    if (m_stopped) {
        return;
    }
    Q_ASSERT(!m_rendering);

    m_framePending = false;
    m_frameRequestedWhileRendering = false;
    m_rendering = true;
    renderFrame();
    m_rendering = false;

    if (!m_stopped && m_frameRequestedWhileRendering) {
        requestFrame();
    }
}

void RhiPresentationEngine::renderFrame() {
    if (m_stopped) {
        return;
    }
    if (m_surfaceTransitionPending || !m_window.isExposed() || m_window.size().isEmpty()) {
        return;
    }

    if (!m_graphicsDevice && !initializeGraphicsDevice()) {
        scheduleDeviceRecovery();
        return;
    }
    QSize pixelSize;
    QuickUiLayer::RenderTargetUpdate targetUpdate = QuickUiLayer::RenderTargetUpdate::Unchanged;
    {
        GraphicsDeviceExecutionScope execution = m_graphicsDevice->acquireExecutionScope();
        if (!m_quickUi && !initializeDevice()) {
            return;
        }
        if (!reconcileOutputCharacteristics()) {
            return;
        }
        if (!m_swapChain && !createSwapChain()) {
            return;
        }
        if (!resizeSwapChain()) {
            return;
        }

        m_quickUi->setLogicalSize(m_window.size());
        pixelSize = m_swapChain->currentPixelSize();
        targetUpdate = m_quickUi->ensureRenderTarget(pixelSize, m_window.devicePixelRatio());
        if (targetUpdate == QuickUiLayer::RenderTargetUpdate::DeviceLost) {
            handleDeviceLoss("creating the Qt Quick render target");
            return;
        }
        if (targetUpdate == QuickUiLayer::RenderTargetUpdate::Unavailable) {
            fail(ApplicationError(ApplicationError::Code::UiRenderingUnavailable,
                                  tr("SunPlayer could not create its user-interface render target."),
                                  QStringLiteral("QRhi rejected an FP16 Qt Quick render target")));
            return;
        }

        // QQuickRenderControl uses a separate offscreen QRhi frame. It must
        // complete before the visible swapchain frame starts.
        m_quickUi->renderIfDirty();
        if (m_rhi->isDeviceLost()) {
            handleDeviceLoss("rendering the Qt Quick layer");
            return;
        }
    }

    // QML synchronization can switch the active page/source. Let a visible
    // source select its frame first because preparation may change producer
    // configuration, content, or display geometry.
    bool const videoViewportActive = m_videoViewport.isRenderable();
    auto const presentationTime = std::chrono::steady_clock::now();
    if (videoViewportActive) {
        m_videoSource.prepareForPresentation(presentationTime);
    }

    float const scaleX = static_cast<float>(pixelSize.width()) / static_cast<float>(m_window.width());
    float const scaleY = static_cast<float>(pixelSize.height()) / static_cast<float>(m_window.height());
    Q_ASSERT(std::isfinite(scaleX) && scaleX > 0.0f);
    Q_ASSERT(std::isfinite(scaleY) && scaleY > 0.0f);

    QRect videoRect;
    std::optional<RenderedVideoSurfaceState> requestedSurface;
    std::optional<double> const displayAspectRatio = m_videoSource.displayAspectRatio();
    if (videoViewportActive && displayAspectRatio) {
        QRectF const viewport = m_videoViewport.rect();
        QRectF const scaledViewport(viewport.x() * scaleX, viewport.y() * scaleY, viewport.width() * scaleX,
                                    viewport.height() * scaleY);
        videoRect = aspectFitVideoRect(scaledViewport.toAlignedRect().intersected(QRect(QPoint{}, pixelSize)),
                                       displayAspectRatio);
    }

    if (!videoRect.isEmpty()) {
        PresentationTarget const presentationTarget = m_outputState.presentationTarget();
        bool const diagnosticsActive = m_videoSource.route() == ActiveVideoSource::Route::Diagnostics;
        // Manual target controls belong to HDR Lab. Player always follows the
        // live platform target, regardless of retained diagnostic state.
        float const requestedTargetPeak = diagnosticsActive && !m_settings.automaticTargetPeak()
                                              ? m_settings.manualTargetHeadroom()
                                              : presentationTarget.effectiveTargetHeadroom;
        float const targetPeak = m_surfaceContract.constrainTargetHeadroom(requestedTargetPeak);
        Q_ASSERT(std::isfinite(targetPeak) && targetPeak >= 1.0f);
        float const referenceWhiteNits =
            presentationTarget.sdrWhiteKnown ? presentationTarget.sdrWhiteNits : scRgbReferenceWhiteNits;
        Q_ASSERT(std::isfinite(referenceWhiteNits) && referenceWhiteNits > 0.0f);
        bool const targetMinimumLuminanceKnown = presentationTarget.luminanceKnown;
        float const targetMinimumLuminanceNits =
            targetMinimumLuminanceKnown
                ? std::clamp(presentationTarget.minLuminanceNits, 0.0f, referenceWhiteNits * targetPeak)
                : 0.0f;

        requestedSurface.emplace();
        requestedSurface->description.pixelSize = videoRect.size();
        requestedSurface->description.pixelFormat = RenderedVideoPixelFormat::Rgba16Float;
        requestedSurface->description.colorSpace = RenderedVideoColorSpace::LinearSrgb;
        requestedSurface->description.luminance = RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative;
        requestedSurface->description.alphaMode = RenderedVideoAlphaMode::Opaque;
        requestedSurface->description.referenceWhiteNits = referenceWhiteNits;
        requestedSurface->description.targetMinimumLuminanceKnown = targetMinimumLuminanceKnown;
        requestedSurface->description.targetMinimumLuminanceNits = targetMinimumLuminanceNits;
        requestedSurface->description.targetPeakHeadroom = targetPeak;
        requestedSurface->description.targetPrimariesKnown = presentationTarget.targetPrimariesKnown;
        requestedSurface->description.targetPrimaries = presentationTarget.targetPrimaries;
        requestedSurface->graphicsDeviceGeneration = m_graphicsDevice->generation();
        requestedSurface->contentRevision = m_videoSource.contentRevision();
        Q_ASSERT(requestedSurface->isValid());
    }

    GraphicsDeviceExecutionScope execution = m_graphicsDevice->acquireExecutionScope();
    // Rebind only after the page route and prepared source state are coherent.
    // Hidden route changes still replace the producer promptly.
    bool const videoProducerChanged = refreshVideoProducer();
    Q_ASSERT(m_videoProducer);

    if (requestedSurface) {
        VideoOperationResult const ensureResult = m_videoProducer->ensureSurface(*requestedSurface);
        if (!handleVideoOperationResult("creating the rendered-video surface", ensureResult)) {
            return;
        }
        // Target provisioning can select a direct, copy, or fallback path.
        updateBackendState();
    }

    QRhiTexture* const compositionVideoTexture = requestedSurface ? &m_videoProducer->textureForComposition() : nullptr;
    std::uint64_t const compositionTextureRevision =
        requestedSurface ? m_videoProducer->compositionTextureRevision() : 0;
    Q_ASSERT(m_subtitleRenderer);
    bool const subtitleActive = videoViewportActive && m_videoSource.route() == ActiveVideoSource::Route::Player;
    bool const subtitlePrepared = m_subtitleRenderer->prepare(
        m_mediaSession.subtitlePresentationSnapshot(presentationTime), videoRect, pixelSize, subtitleActive);
    QString const subtitleError = m_subtitleRenderer->error();
    if (!subtitlePrepared && subtitleError != m_reportedSubtitleError) {
        m_reportedSubtitleError = subtitleError;
        qCWarning(sunplayerLogPresentation).noquote() << "event=subtitle.presentation_failed"
                                                      << "error=" + subtitleError;
    } else if (subtitlePrepared) {
        m_reportedSubtitleError.clear();
    }
    std::uint64_t const subtitleTextureRevision = m_subtitleRenderer->textureRevision();

    if (!m_compositor) {
        m_compositor = std::make_unique<HdrCompositor>(*m_rhi);
        HdrCompositor::ResourceResult const compositorResult = m_compositor->initialize(
            *m_renderPassDescriptor, compositionVideoTexture, m_subtitleRenderer->texture(), m_quickUi->texture());
        if (compositorResult == HdrCompositor::ResourceResult::DeviceLost) {
            handleDeviceLoss("creating the HDR compositor");
            return;
        }
        if (compositorResult == HdrCompositor::ResourceResult::Unavailable) {
            fail(ApplicationError(ApplicationError::Code::CompositorUnavailable,
                                  tr("SunPlayer could not initialize video composition."),
                                  QStringLiteral("QRhi rejected an HDR compositor resource")));
            return;
        }
        m_boundVideoTextureRevision = compositionTextureRevision;
        m_boundSubtitleTextureRevision = subtitleTextureRevision;
    } else if (videoProducerChanged || targetUpdate == QuickUiLayer::RenderTargetUpdate::Recreated ||
               m_boundVideoTextureRevision != compositionTextureRevision ||
               m_boundSubtitleTextureRevision != subtitleTextureRevision) {
        HdrCompositor::ResourceResult const bindingResult =
            m_compositor->setTextures(compositionVideoTexture, m_subtitleRenderer->texture(), m_quickUi->texture());
        if (bindingResult == HdrCompositor::ResourceResult::DeviceLost) {
            handleDeviceLoss("rebinding compositor layer textures");
            return;
        }
        if (bindingResult == HdrCompositor::ResourceResult::Unavailable) {
            fail(ApplicationError(ApplicationError::Code::CompositorUnavailable,
                                  tr("SunPlayer could not update video composition."),
                                  QStringLiteral("QRhi rejected compositor texture bindings")));
            return;
        }
        m_boundVideoTextureRevision = compositionTextureRevision;
        m_boundSubtitleTextureRevision = subtitleTextureRevision;
    }

    QRhi::FrameOpResult result = m_rhi->beginFrame(m_swapChain.get());
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        // QRhi requires createOrResize() even when the pixel size is unchanged.
        if (!resizeSwapChain(true)) {
            return;
        }
        result = m_rhi->beginFrame(m_swapChain.get());
    }
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        if (resizeSwapChain(true)) {
            requestFrame();
        }
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

    QRhiCommandBuffer& commandBuffer = *m_swapChain->currentFrameCommandBuffer();
    bool frameFinished = false;
    auto const finishFrame = [this, &frameFinished](QRhi::EndFrameFlags flags, bool acceptPendingRender) {
        Q_ASSERT(!frameFinished);
        QRhi::FrameOpResult const finishResult = m_rhi->endFrame(m_swapChain.get(), flags);
        frameFinished = true;
        if (finishResult == QRhi::FrameOpSuccess) {
            m_videoProducer->submissionAccepted();
        } else {
            m_videoProducer->submissionAborted();
        }
        if (finishResult == QRhi::FrameOpSuccess && acceptPendingRender) {
            m_videoProducer->commitPendingRender();
        } else {
            m_videoProducer->discardPendingRender();
        }
        return finishResult;
    };
    auto const unfinishedFrameGuard = qScopeGuard([this, &finishFrame, &frameFinished] {
        if (frameFinished) {
            return;
        }
        QRhi::FrameOpResult const abandonedResult = finishFrame(QRhi::SkipPresent, false);
        if (abandonedResult != QRhi::FrameOpSuccess && abandonedResult != QRhi::FrameOpDeviceLost) {
            qCWarning(sunplayerLogPresentation, "QRhi returned %d while abandoning an unfinished frame",
                      static_cast<int>(abandonedResult));
        }
    });

    if (requestedSurface && m_videoProducer->needsRender(*requestedSurface)) {
        VideoOperationResult const renderResult = m_videoProducer->render(commandBuffer, *requestedSurface);
        if (renderResult != VideoOperationResult::Ready) {
            QRhi::FrameOpResult const abandonedResult = finishFrame(QRhi::SkipPresent, false);
            handleVideoOperationResult("rendering the video surface", abandonedResult == QRhi::FrameOpDeviceLost
                                                                          ? VideoOperationResult::DeviceLost
                                                                          : renderResult);
            return;
        }
    }
    if (requestedSurface) {
        VideoOperationResult const compositionResult = m_videoProducer->prepareForComposition(commandBuffer);
        if (compositionResult != VideoOperationResult::Ready) {
            QRhi::FrameOpResult const abandonedResult = finishFrame(QRhi::SkipPresent, false);
            handleVideoOperationResult("preparing video for composition", abandonedResult == QRhi::FrameOpDeviceLost
                                                                              ? VideoOperationResult::DeviceLost
                                                                              : compositionResult);
            return;
        }
    }
    m_subtitleRenderer->uploadIfNeeded(commandBuffer);

    float const sdrScale = m_outputState.sdrScale();
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
    bool const extendedLinearActive = m_swapChain->format() == QRhiSwapChain::HDRExtendedSrgbLinear;
    PresentationOutputEncoding const outputEncoding = m_surfaceContract.outputEncoding(extendedLinearActive);
    parameters.outputEncoding = static_cast<float>(outputEncoding);

    Q_ASSERT(m_compositor);
    m_compositor->render(commandBuffer, *m_swapChain->currentFrameRenderTarget(), pixelSize, parameters);

    if (m_surfaceDeclarationPending) {
        Q_ASSERT(m_surfaceController);
        // Queue the double-buffered declaration immediately before Vulkan WSI
        // presents the matching buffer. No Qt event processing can insert an
        // unrelated wl_surface commit between these two operations.
        m_surfaceController->applyMode(m_window, m_surfaceContract.mode);
    }
    result = finishFrame({}, true);
    if (result == QRhi::FrameOpDeviceLost) {
        handleDeviceLoss("presenting a frame");
        return;
    }
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        if (resizeSwapChain(true)) {
            requestFrame();
        }
        return;
    }
    if (result != QRhi::FrameOpSuccess) {
        handleFrameError("ending a frame", static_cast<int>(result));
        return;
    }

    m_surfaceDeclarationPending = false;
    m_retriedFrameError = false;
    m_hasPresentedFrame = true;
    if (requestedSurface) {
        emit videoFramePresented(requestedSurface->contentRevision);
    }
    scheduleNextFrame(videoViewportActive);
}

void RhiPresentationEngine::requestFrame() {
    if (m_stopped) {
        return;
    }
    if (!m_window.isExposed()) {
        return;
    }
    if (m_rendering) {
        m_frameRequestedWhileRendering = true;
        return;
    }
    if (m_framePending) {
        return;
    }
    m_framePending = true;
    m_window.requestUpdate();
}

void RhiPresentationEngine::handleExposure() {
    if (m_stopped) {
        return;
    }
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

#ifdef Q_OS_MACOS
    // AppKit EDR headroom can change while the native surface is hidden.
    m_outputState.reprobePresentation();
#endif

    // The first presentation must happen before requestUpdate() can switch to
    // DXGI's vsync service. Scheduling before the swapchain exists can leave
    // both Qt's timer path and its DXGI path trying to deliver one request.
    if (!m_framePending) {
        render();
    }
}

void RhiPresentationEngine::markUiDirty() {
    if (m_stopped) {
        return;
    }
    if (m_quickUi) {
        m_quickUi->markDirty();
    }
    requestFrame();
}

void RhiPresentationEngine::markPresentationDirty() { requestFrame(); }

void RhiPresentationEngine::releaseSwapChain() {
    if (m_stopped && !m_graphicsDevice) {
        return;
    }
    std::optional<GraphicsDeviceExecutionScope> execution;
    if (m_graphicsDevice) {
        execution.emplace(m_graphicsDevice->acquireExecutionScope());
    }
    m_swapChainRecoveryTimer.stop();
    m_swapChainRecoveryAttempts = 0;
    releaseSwapChainResources();
}

void RhiPresentationEngine::releaseSwapChainResources() {
    m_framePending = false;
    // The pipeline and swapchain depend on this render-pass descriptor.
    // The independent Quick and video textures intentionally survive it.
    m_compositor.reset();
    m_boundVideoTextureRevision = 0;
    m_boundSubtitleTextureRevision = 0;
    if (m_swapChain) {
        m_swapChain->destroy();
    }
    m_swapChain.reset();
    m_renderPassDescriptor.reset();
}

QQuickWindow* RhiPresentationEngine::quickWindow() const { return m_quickUi ? m_quickUi->quickWindow() : nullptr; }

bool RhiPresentationEngine::hasPresentedFrame() const { return m_hasPresentedFrame; }

void RhiPresentationEngine::handleSurfaceCreated() {
    Q_ASSERT(!m_swapChain);
    m_hasPresentedFrame = false;
}

bool RhiPresentationEngine::initializeDevice() {
    Q_ASSERT(!m_quickUi);
    Q_ASSERT(m_graphicsDevice);
    Q_ASSERT(m_rhi);

    m_quickUi = std::make_unique<QuickUiLayer>(m_window, *m_rhi, m_outputState, m_settings, m_diagnosticSource,
                                               m_mediaSession, m_videoSource, m_videoViewport, m_supportController);
    connect(m_quickUi.get(), &QuickUiLayer::updateRequested, this, &RhiPresentationEngine::requestFrame);
    QuickUiLayer::InitializationResult const result = m_quickUi->initialize();
    if (result == QuickUiLayer::InitializationResult::DeviceLost) {
        handleDeviceLoss("initializing Qt Quick");
        return false;
    }
    if (result == QuickUiLayer::InitializationResult::Unavailable) {
        fail(ApplicationError(ApplicationError::Code::UiRenderingUnavailable,
                              tr("SunPlayer could not initialize its user interface."),
                              QStringLiteral("QQuickRenderControl initialization failed")));
        return false;
    }
    m_subtitleRenderer = std::make_unique<SubtitleRenderer>(*m_rhi);
    refreshVideoProducer();
    return true;
}

bool RhiPresentationEngine::initializeGraphicsDevice() {
    Q_ASSERT(!m_graphicsDevice);
    Q_ASSERT(!m_rhi);
    m_graphicsDevice = GraphicsBackendFactory::createDeviceDomain(m_window);
    if (!m_graphicsDevice) {
        return false;
    }
    m_rhi = &m_graphicsDevice->rhi();
    m_mediaSession.setVideoDecodeCapability(m_graphicsDevice->videoDecodeCapability());
    if (m_surfaceController) {
        m_outputCharacteristicsDirty = true;
    }
    return true;
}

bool RhiPresentationEngine::refreshVideoProducer() {
    Q_ASSERT(m_graphicsDevice);
    std::uint64_t const requestedRevision = m_videoSource.producerConfigurationRevision();
    Q_ASSERT(requestedRevision != 0);
    if (m_videoProducer && m_videoProducerConfigurationRevision == requestedRevision) {
        return false;
    }

    std::unique_ptr<RenderedVideoProducer> producer = m_videoSource.createProducer(*m_graphicsDevice);
    if (!producer) {
        qCFatal(sunplayerLogPresentation, "The video source did not create a producer");
    }
    m_videoProducer = std::move(producer);
    m_videoProducerConfigurationRevision = requestedRevision;
    return true;
}

bool RhiPresentationEngine::createSwapChain() {
    Q_ASSERT(m_rhi);
    Q_ASSERT(!m_swapChain);
    Q_ASSERT(!m_renderPassDescriptor);
    Q_ASSERT(!m_compositor);

    m_swapChain.reset(m_rhi->newSwapChain());
    m_swapChain->setWindow(&m_window);

    bool const hdr10PlatformSupported =
        !m_surfaceContract.hdr10Required() || m_graphicsDevice->supportsHdr10Presentation(m_window);
    QRhiSwapChain::Format const desiredFormat = desiredSwapChainFormat(
        m_surfaceContract, *m_swapChain, m_outputState.displayHdrEnabled(), hdr10PlatformSupported);
    if (m_surfaceContract.hdr10Required() && desiredFormat != QRhiSwapChain::HDR10) {
        m_swapChain.reset();
        rejectRequiredHdrSurface("the current Wayland Vulkan surface does not expose "
                                 "10-bit BT.2020/PQ plus pass-through");
        return false;
    }
    m_swapChain->setFormat(desiredFormat);
    m_renderPassDescriptor.reset(m_swapChain->newCompatibleRenderPassDescriptor());
    if (!m_renderPassDescriptor) {
        handleSwapChainFailure("describing the swapchain render pass", "QRhi could not describe the HDR10 render pass");
        return false;
    }
    m_swapChain->setRenderPassDescriptor(m_renderPassDescriptor.get());
    if (!m_swapChain->createOrResize()) {
        handleSwapChainFailure("creating the swapchain", "QRhi could not create the HDR10 swapchain");
        return false;
    }
    completePresentationRecovery();
    updateBackendState();
    return true;
}

bool RhiPresentationEngine::resizeSwapChain(bool force) {
    Q_ASSERT(m_swapChain);
    Q_ASSERT(m_renderPassDescriptor);
    QSize const surfaceSize = m_swapChain->surfacePixelSize();
    if (surfaceSize.isEmpty()) {
        return false;
    }
    if (!force && m_swapChain->currentPixelSize() == surfaceSize) {
        return true;
    }

    return createOrResizeSwapChain("resize");
}

bool RhiPresentationEngine::createOrResizeSwapChain(char const* operation) {
    Q_ASSERT(m_swapChain);
    if (m_swapChain->createOrResize()) {
        completePresentationRecovery();
        updateBackendState();
        return true;
    }

    handleSwapChainFailure(operation, "QRhi could not resize the HDR10 swapchain");
    return false;
}

void RhiPresentationEngine::handleSwapChainFailure(char const* operation, char const* hdrRejectionReason) {
    Q_ASSERT(m_graphicsDevice);
    bool const deviceLost = m_rhi->isDeviceLost();
    releaseSwapChainResources();
    if (deviceLost) {
        handleDeviceLoss(operation);
    } else if (!m_graphicsDevice->supportsPresentation(m_window)) {
        rebuildForPresentIncompatibleSurface();
    } else if (m_surfaceContract.hdr10Required()) {
        rejectRequiredHdrSurface(hdrRejectionReason);
    } else {
        scheduleSwapChainRecovery(operation);
    }
}

void RhiPresentationEngine::scheduleSwapChainRecovery(char const* operation) {
    constexpr int maximumAttempts = 8;
    constexpr auto retryDelay = std::chrono::milliseconds(250);

    if (m_swapChainRecoveryAttempts >= maximumAttempts) {
        fail(ApplicationError(
            ApplicationError::Code::SwapChainUnavailable, tr("SunPlayer could not recover the presentation surface."),
            QStringLiteral("QRhi swap-chain recovery exhausted while %1").arg(QString::fromLatin1(operation))));
        return;
    }

    ++m_swapChainRecoveryAttempts;
    qCWarning(sunplayerLogPresentation, "Retrying QRhi swapchain creation after failure while %s (%d/%d)", operation,
              m_swapChainRecoveryAttempts, maximumAttempts);
    m_swapChainRecoveryTimer.start(retryDelay);
}

void RhiPresentationEngine::completePresentationRecovery() {
    m_deviceRecoveryTimer.stop();
    m_deviceRecoveryAttempts = 0;
    m_swapChainRecoveryTimer.stop();
    m_swapChainRecoveryAttempts = 0;
    m_recoveringDevice = false;
}

void RhiPresentationEngine::releaseDevice() {
    std::optional<GraphicsDeviceExecutionScope> execution;
    if (m_graphicsDevice) {
        execution.emplace(m_graphicsDevice->acquireExecutionScope());
    }
    // Every child resource must be gone before destroying the QRhi.
    Q_ASSERT(!m_rhi || !m_rhi->isRecordingFrame());
    if (m_rhi && !m_rhi->isDeviceLost()) {
        QRhi::FrameOpResult const finishResult = m_rhi->finish();
        if (finishResult != QRhi::FrameOpSuccess && finishResult != QRhi::FrameOpDeviceLost) {
            qCCritical(sunplayerLogGraphics, "Could not finish GPU work before releasing device resources");
            if (!m_destroying) {
                fail(ApplicationError(ApplicationError::Code::GraphicsCleanupFailed,
                                      tr("SunPlayer could not safely reset the graphics device."),
                                      QStringLiteral("QRhi::finish returned %1").arg(static_cast<int>(finishResult))));
            }
        }
    }
    releaseSwapChainResources();
    m_videoProducer.reset();
    m_videoProducerConfigurationRevision = 0;
    // Quick invalidation may emit updateRequested while it tears down.
    if (m_quickUi) {
        disconnect(m_quickUi.get(), nullptr, this, nullptr);
    }
    m_quickUi.reset();
    m_subtitleRenderer.reset();
    m_reportedSubtitleError.clear();
    m_rhi = nullptr;
    m_graphicsDevice.reset();
    m_swapChainRecoveryTimer.stop();
    m_swapChainRecoveryAttempts = 0;
    m_outputCharacteristicsDirty = false;
#ifdef Q_OS_MACOS
    m_swapChainSurfaceDirty = false;
#endif
}

void RhiPresentationEngine::handleDeviceLoss(char const* operation) {
    qCWarning(sunplayerLogPresentation, "QRhi device was lost while %s; retrying", operation);
    if (!m_recoveringDevice) {
        m_deviceRecoveryAttempts = 0;
    }
    m_recoveringDevice = true;
    m_mediaSession.invalidateGraphicsDevice();
    releaseDevice();
    scheduleDeviceRecovery();
}

void RhiPresentationEngine::handleFrameError(char const* operation, int result) {
    if (m_retriedFrameError) {
        fail(ApplicationError(ApplicationError::Code::FrameSubmissionFailed,
                              tr("SunPlayer could not submit video frames."),
                              QStringLiteral("QRhi failed twice while %1 with result %2")
                                  .arg(QString::fromLatin1(operation))
                                  .arg(result)));
        return;
    }
    qCWarning(sunplayerLogPresentation, "QRhi failed while %s: %d; rebuilding once", operation, result);
    m_retriedFrameError = true;
    m_recoveringDevice = true;
    m_deviceRecoveryAttempts = 0;
    m_mediaSession.invalidateGraphicsDevice();
    releaseDevice();
    scheduleDeviceRecovery();
}

bool RhiPresentationEngine::handleVideoOperationResult(char const* operation, VideoOperationResult result) {
    if (result == VideoOperationResult::Ready) {
        return true;
    }
    if (result == VideoOperationResult::DeviceLost) {
        handleDeviceLoss(operation);
        return false;
    }

    updateBackendState();
    RenderedVideoProducerDiagnostics const diagnostics = m_videoProducer->diagnostics();
    QString const reason = diagnostics.target.fallbackReason;
    QString const effectiveReason = reason.isEmpty() ? QStringLiteral("No supported video path is available") : reason;
    if (m_videoSource.reportPresentationFailure({
            .kind =
                diagnostics.failureKind == VideoFailureKind::None ? VideoFailureKind::General : diagnostics.failureKind,
            .reason = effectiveReason,
        })) {
        qCWarning(sunplayerLogPresentation, "Video path unavailable while %s: %s", operation,
                  qPrintable(effectiveReason));
        requestFrame();
        return false;
    }
    ApplicationError::Code const code = m_videoSource.route() == ActiveVideoSource::Route::Diagnostics
                                            ? ApplicationError::Code::DiagnosticVideoUnavailable
                                            : ApplicationError::Code::FrameSubmissionFailed;
    fail(ApplicationError(code, tr("SunPlayer could not render the active video source."),
                          QStringLiteral("%1: %2").arg(QString::fromLatin1(operation), effectiveReason)));
    return false;
}

void RhiPresentationEngine::scheduleDeviceRecovery() {
    if (m_stopped) {
        return;
    }
    constexpr int maximumAttempts = 8;
    constexpr auto retryDelay = std::chrono::milliseconds(250);

    if (m_deviceRecoveryAttempts >= maximumAttempts) {
        fail(ApplicationError(ApplicationError::Code::GraphicsDeviceRecoveryExhausted,
                              tr("SunPlayer could not recover the graphics device."),
                              QStringLiteral("QRhi device recovery exhausted after %1 attempts").arg(maximumAttempts)));
        return;
    }

    ++m_deviceRecoveryAttempts;
    qCWarning(sunplayerLogPresentation, "Retrying QRhi device recovery (%d/%d)", m_deviceRecoveryAttempts,
              maximumAttempts);
    m_deviceRecoveryTimer.start(retryDelay);
}

void RhiPresentationEngine::updateBackendState() {
    PresentationBackendState state;
    GraphicsDeviceDiagnostics const& graphicsDiagnostics = m_graphicsDevice->diagnostics();
    state.graphicsApi = graphicsDiagnostics.backendName;
    state.graphicsAdapter = graphicsDiagnostics.adapterName;
    switch (m_swapChain->format()) {
    case QRhiSwapChain::HDRExtendedSrgbLinear:
        state.hdrPresentationActive = true;
        state.swapChainFormat = QStringLiteral("scRGB / extended linear sRGB");
        break;
    case QRhiSwapChain::HDR10:
        state.hdrPresentationActive = true;
        state.swapChainFormat = QStringLiteral("HDR10 / BT.2020 PQ");
        break;
    default:
        state.swapChainFormat = QStringLiteral("SDR / sRGB");
        break;
    }
    state.videoSurfaceFormat = QStringLiteral("RGBA16F · linear sRGB · SDR-white-relative");
    RenderedVideoProducerDiagnostics const videoDiagnostics = m_videoProducer->diagnostics();
    state.videoSurfaceProducer = videoDiagnostics.producerName;
    state.videoInputPath = videoDiagnostics.inputPath;
    state.videoColorPolicy = videoDiagnostics.colorPolicy;
    state.videoOutputPath = videoOutputPathName(videoDiagnostics.target.outputPath);
    state.videoSynchronization = videoDiagnostics.target.synchronizationMode;
    state.videoCopySummary = QStringLiteral("%1 input CPU transfers per input frame · "
                                            "%2 input GPU copies per input frame · "
                                            "%3 output GPU copies · "
                                            "%4 output CPU transfers per render")
                                 .arg(videoDiagnostics.knownInputCpuTransfersPerInputFrame)
                                 .arg(videoDiagnostics.knownInputGpuCopiesPerInputFrame)
                                 .arg(videoDiagnostics.target.knownOutputGpuCopiesPerRender)
                                 .arg(videoDiagnostics.target.knownOutputCpuTransfersPerRender);
    state.videoFallbackReason = videoDiagnostics.target.fallbackReason;

    QRhiSwapChainHdrInfo const info = m_swapChain->hdrInfo();
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    state.sceneReferred = false;
#else
    state.sceneReferred = info.luminanceBehavior == QRhiSwapChainHdrInfo::SceneReferred;
#endif
#ifdef Q_OS_LINUX
    state.useSdrDisplayTargetForHdrPresentation = true;
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    // Apple EDR is display-referred. Numeric 1.0 already means the current
    // SDR white. Managed Wayland uses the same surface-coordinate meaning;
    // its preferred-description provider supplies the compositor's target.
    // Vulkan's generic hdrInfo defaults are not Linux display observations.
    state.sdrWhiteKnown = false;
    state.sdrWhiteNits = 80.0f;
#else
    state.sdrWhiteKnown = std::isfinite(info.sdrWhiteLevel) && info.sdrWhiteLevel > 0.0f;
    state.sdrWhiteNits = positiveOrFallback(info.sdrWhiteLevel, 80.0f, "SDR white level");
#endif
    if (info.limitsType == QRhiSwapChainHdrInfo::LuminanceInNits) {
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
        state.luminanceKnown = false;
#else
        float const rawMinimumNits = info.limits.luminanceInNits.minLuminance;
        float const rawMaximumNits = info.limits.luminanceInNits.maxLuminance;
        state.luminanceKnown = isValidDisplayLuminanceRange(rawMinimumNits, rawMaximumNits);
        state.minLuminanceNits = nonNegativeOrZero(rawMinimumNits, "minimum luminance");
        state.maxLuminanceNits = nonNegativeOrZero(rawMaximumNits, "maximum luminance");
        if (std::isfinite(rawMinimumNits) && rawMinimumNits >= 0.0f && std::isfinite(rawMaximumNits) &&
            rawMaximumNits >= 0.0f && rawMinimumNits > rawMaximumNits) {
            qCWarning(sunplayerLogPresentation,
                      "QRhi reported minimum luminance above maximum luminance; treating the range as unknown");
        }
#endif
    } else {
        state.currentHeadroom =
            positiveOrFallback(info.limits.colorComponentValue.maxColorComponentValue, 1.0f, "current headroom");
        state.potentialHeadroom = positiveOrFallback(info.limits.colorComponentValue.maxPotentialColorComponentValue,
                                                     state.currentHeadroom, "potential headroom");
    }
    m_outputState.setBackendState(state);
}

void RhiPresentationEngine::scheduleNextFrame(bool videoViewportActive) {
    if ((videoViewportActive && m_videoSource.wantsContinuousFrames()) || m_quickUi->isDirty()) {
        requestFrame();
    }
}

void RhiPresentationEngine::markOutputCharacteristicsDirty() {
    m_outputCharacteristicsDirty = true;
    markPresentationDirty();
}

bool RhiPresentationEngine::reconcileOutputCharacteristics() {
#ifdef Q_OS_MACOS
    if (!m_outputCharacteristicsDirty && !m_swapChainSurfaceDirty) {
        return true;
    }
    bool const refreshSwapChainSurface = m_swapChainSurfaceDirty;
    m_swapChainSurfaceDirty = false;
#else
    if (!m_outputCharacteristicsDirty) {
        return true;
    }
#endif
    m_outputCharacteristicsDirty = false;

    if (m_surfaceController) {
        Q_ASSERT(m_graphicsDevice);
        PresentationSurfaceMode const desiredMode = m_surfaceController->desiredMode(m_graphicsDevice->generation());
        if (desiredMode != m_surfaceContract.mode) {
            queueSurfaceTransition();
            return false;
        }
    }

    if (!m_swapChain) {
        return true;
    }

    bool const hdr10PlatformSupported =
        !m_surfaceContract.hdr10Required() || m_graphicsDevice->supportsHdr10Presentation(m_window);
    QRhiSwapChain::Format const desiredFormat = desiredSwapChainFormat(
        m_surfaceContract, *m_swapChain, m_outputState.displayHdrEnabled(), hdr10PlatformSupported);
    if (m_swapChain->format() != desiredFormat) {
        releaseSwapChainResources();
        return true;
    }

#ifdef Q_OS_MACOS
    if (refreshSwapChainSurface) {
        qCInfo(sunplayerLogPresentation, "Reapplying the Metal presentation surface after a screen change");
        return createOrResizeSwapChain("reconfiguring the presentation surface");
    }
#endif

    // Refresh QRhi luminance/headroom fallback data without rebuilding an
    // already compatible presentation surface.
    updateBackendState();
    return true;
}

void RhiPresentationEngine::queueSurfaceTransition() {
    if (m_stopped) {
        return;
    }
    Q_ASSERT(m_surfaceController);
    if (m_surfaceTransitionPending) {
        return;
    }

    releaseSwapChainResources();
    m_surfaceTransitionPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (m_stopped) {
                return;
            }
            if (!m_graphicsDevice) {
                m_surfaceTransitionPending = false;
                m_outputCharacteristicsDirty = true;
                requestFrame();
                return;
            }

            PresentationSurfaceMode const latestMode = m_surfaceController->desiredMode(m_graphicsDevice->generation());
            if (latestMode != m_surfaceContract.mode) {
                m_surfaceContract.mode = latestMode;
                m_surfaceDeclarationPending = true;
            }

            m_surfaceTransitionPending = false;
            m_outputCharacteristicsDirty = true;
            requestFrame();
        },
        Qt::QueuedConnection);
}

void RhiPresentationEngine::rejectRequiredHdrSurface(char const* reason) {
    Q_ASSERT(m_surfaceController);
    Q_ASSERT(m_graphicsDevice);
    Q_ASSERT(m_surfaceContract.hdr10Required());
    m_surfaceController->rejectHdrTarget(m_graphicsDevice->generation(), reason);
    m_outputCharacteristicsDirty = true;
    queueSurfaceTransition();
}

void RhiPresentationEngine::rebuildForPresentIncompatibleSurface() {
    Q_ASSERT(m_graphicsDevice);
    qCWarning(sunplayerLogPresentation, "The Vulkan device cannot present to the current Wayland "
                                        "surface; rebuilding the graphics domain");
    m_mediaSession.invalidateGraphicsDevice();
    if (!m_recoveringDevice) {
        m_deviceRecoveryAttempts = 0;
    }
    m_recoveringDevice = true;
    releaseDevice();
    scheduleDeviceRecovery();
}

void RhiPresentationEngine::fail(ApplicationError error) {
    if (m_stopped) {
        return;
    }
    Q_ASSERT(error.isValid());
    m_stopped = true;
    m_mediaSession.invalidateGraphicsDevice();
    m_deviceRecoveryTimer.stop();
    m_swapChainRecoveryTimer.stop();
    m_framePending = false;
    m_frameRequestedWhileRendering = false;
    qCCritical(sunplayerLogPresentation).noquote()
        << "event=presentation.terminal_error"
        << "code=" + error.stableCode() << "subsystem=" + error.subsystemName() << "detail=" + error.technicalDetail();
    emit terminalError(std::move(error));
}
