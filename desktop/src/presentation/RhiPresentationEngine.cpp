#include "presentation/RhiPresentationEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

#include <QQuickWindow>
#include <QWindow>
#include <QtCore/qscopeguard.h>
#include <rhi/qrhi.h>

#include "app/PresentationSettings.h"
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
#include "video/RenderedVideoSurface.h"
#include "video/RenderedVideoSource.h"
#include "video/VideoTargetInterop.h"

namespace {
constexpr float scRgbReferenceWhiteNits = 80.0f;

float positiveOrFallback(float value, float fallback, const char *name) {
    if (std::isfinite(value) && value > 0.0f)
        return value;
    qCWarning(
        sunroomLogPresentation,
        "QRhi reported invalid %s: %g; using %g",
        name,
        value,
        fallback);
    return fallback;
}

float nonNegativeOrZero(float value, const char *name) {
    if (std::isfinite(value) && value >= 0.0f)
        return value;
    qCWarning(
        sunroomLogPresentation,
        "QRhi reported invalid %s: %g; using 0",
        name,
        value);
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
        ActiveVideoSource &videoSource,
        DiagnosticVideoSource &diagnosticSource,
        MediaSession &mediaSession,
        VideoViewportState &videoViewport,
        QObject *parent)
    : QObject(parent),
      m_window(window),
      m_outputState(outputState),
      m_settings(settings),
      m_videoSource(videoSource),
      m_diagnosticSource(diagnosticSource),
      m_mediaSession(mediaSession),
      m_videoViewport(videoViewport) {
    m_deviceRecoveryTimer.setSingleShot(true);

    connect(&m_settings, &PresentationSettings::settingsChanged,
            this, &RhiPresentationEngine::requestFrame);
    connect(&m_videoSource, &RenderedVideoSource::updateRequested,
            this, &RhiPresentationEngine::requestFrame);
    connect(&m_videoViewport, &VideoViewportState::viewportChanged,
            this, &RhiPresentationEngine::requestFrame);
    connect(&m_outputState, &PresentationOutputState::stateChanged,
            this, &RhiPresentationEngine::markPresentationDirty);
    connect(&m_outputState,
            &PresentationOutputState::outputCharacteristicsChanged,
            this,
            &RhiPresentationEngine::markOutputCharacteristicsDirty);
    connect(&m_deviceRecoveryTimer, &QTimer::timeout,
            this, &RhiPresentationEngine::requestFrame);
    connect(&m_mediaSession, &MediaSession::subtitleChanged,
            this, &RhiPresentationEngine::requestFrame);

    if (!initializeGraphicsDevice())
        qCFatal(
            sunroomLogPresentation,
            "Could not create the QRhi backend");
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

    if (!m_graphicsDevice && !initializeGraphicsDevice()) {
        scheduleDeviceRecovery();
        return;
    }
    QSize pixelSize;
    QuickUiLayer::RenderTargetUpdate targetUpdate =
        QuickUiLayer::RenderTargetUpdate::Unchanged;
    {
        GraphicsDeviceExecutionScope execution =
            m_graphicsDevice->acquireExecutionScope();
        if (!m_quickUi && !initializeDevice())
            return;
        reconcileOutputCharacteristics();
        if (!m_swapChain && !createSwapChain())
            return;
        if (!resizeSwapChain())
            return;

        m_quickUi->setLogicalSize(m_window.size());
        pixelSize = m_swapChain->currentPixelSize();
        targetUpdate = m_quickUi->ensureRenderTarget(
            pixelSize, m_window.devicePixelRatio());
        if (targetUpdate
                == QuickUiLayer::RenderTargetUpdate::DeviceLost) {
            handleDeviceLoss("creating the Qt Quick render target");
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
    const bool videoViewportActive =
        m_videoViewport.isRenderable();
    const auto presentationTime =
        std::chrono::steady_clock::now();
    if (videoViewportActive) {
        m_videoSource.prepareForPresentation(
            presentationTime);
    }

    const float scaleX = static_cast<float>(pixelSize.width())
        / static_cast<float>(m_window.width());
    const float scaleY = static_cast<float>(pixelSize.height())
        / static_cast<float>(m_window.height());
    Q_ASSERT(std::isfinite(scaleX) && scaleX > 0.0f);
    Q_ASSERT(std::isfinite(scaleY) && scaleY > 0.0f);

    QRect videoRect;
    std::optional<RenderedVideoSurfaceState> requestedSurface;
    const std::optional<double> displayAspectRatio =
        m_videoSource.displayAspectRatio();
    if (videoViewportActive && displayAspectRatio) {
        const QRectF viewport = m_videoViewport.rect();
        const QRectF scaledViewport(
            viewport.x() * scaleX,
            viewport.y() * scaleY,
            viewport.width() * scaleX,
            viewport.height() * scaleY);
        videoRect = aspectFitVideoRect(
            scaledViewport.toAlignedRect().intersected(
                QRect(QPoint{}, pixelSize)),
            displayAspectRatio);
    }

    if (!videoRect.isEmpty()) {
        const float targetPeak = m_settings.automaticTargetPeak()
            ? m_outputState.effectiveTargetHeadroom()
            : m_settings.manualTargetHeadroom();
        Q_ASSERT(std::isfinite(targetPeak) && targetPeak >= 1.0f);
        const float referenceWhiteNits = m_outputState.sdrWhiteKnown()
            ? m_outputState.sdrWhiteNits()
            : scRgbReferenceWhiteNits;
        Q_ASSERT(
            std::isfinite(referenceWhiteNits)
            && referenceWhiteNits > 0.0f);
        const bool targetMinimumLuminanceKnown =
            m_outputState.luminanceKnown();
        const float targetMinimumLuminanceNits =
            targetMinimumLuminanceKnown
            ? std::clamp(
                m_outputState.minLuminanceNits(),
                0.0f,
                referenceWhiteNits * targetPeak)
            : 0.0f;

        requestedSurface.emplace();
        requestedSurface->description.pixelSize = videoRect.size();
        requestedSurface->description.pixelFormat =
            RenderedVideoPixelFormat::Rgba16Float;
        requestedSurface->description.colorSpace =
            RenderedVideoColorSpace::LinearSrgb;
        requestedSurface->description.luminance =
            RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative;
        requestedSurface->description.alphaMode =
            RenderedVideoAlphaMode::Opaque;
        requestedSurface->description.referenceWhiteNits =
            referenceWhiteNits;
        requestedSurface->description.targetMinimumLuminanceKnown =
            targetMinimumLuminanceKnown;
        requestedSurface->description.targetMinimumLuminanceNits =
            targetMinimumLuminanceNits;
        requestedSurface->description.targetPeakHeadroom = targetPeak;
        requestedSurface->graphicsDeviceGeneration =
            m_graphicsDevice->generation();
        requestedSurface->contentRevision =
            m_videoSource.contentRevision();
        Q_ASSERT(requestedSurface->isValid());
    }

    GraphicsDeviceExecutionScope execution =
        m_graphicsDevice->acquireExecutionScope();
    // Rebind only after the page route and prepared source state are coherent.
    // Hidden route changes still replace the producer promptly.
    const bool videoProducerChanged = refreshVideoProducer();
    Q_ASSERT(m_videoProducer);

    if (requestedSurface) {
        const VideoOperationResult ensureResult =
            m_videoProducer->ensureSurface(*requestedSurface);
        if (!handleVideoOperationResult(
                "creating the rendered-video surface",
                ensureResult)) {
            return;
        }
        // Target provisioning can select a direct, copy, or fallback path.
        updateBackendState();
    }

    QRhiTexture *const compositionVideoTexture = requestedSurface
        ? &m_videoProducer->textureForComposition()
        : nullptr;
    const std::uint64_t compositionTextureRevision = requestedSurface
        ? m_videoProducer->compositionTextureRevision()
        : 0;
    Q_ASSERT(m_subtitleRenderer);
    const bool subtitleActive = videoViewportActive
        && m_videoSource.route()
            == ActiveVideoSource::Route::Player;
    const bool subtitlePrepared = m_subtitleRenderer->prepare(
        m_mediaSession.subtitlePresentationSnapshot(
            presentationTime),
        videoRect,
        pixelSize,
        subtitleActive);
    const QString subtitleError = m_subtitleRenderer->error();
    if (!subtitlePrepared && subtitleError != m_reportedSubtitleError) {
        m_reportedSubtitleError = subtitleError;
        qCWarning(sunroomLogPresentation).noquote()
            << "event=subtitle.presentation_failed"
            << "error=" + subtitleError;
    } else if (subtitlePrepared) {
        m_reportedSubtitleError.clear();
    }
    const std::uint64_t subtitleTextureRevision =
        m_subtitleRenderer->textureRevision();

    if (!m_compositor) {
        m_compositor = std::make_unique<HdrCompositor>(*m_rhi);
        if (m_compositor->initialize(
                *m_renderPassDescriptor,
                compositionVideoTexture,
                m_subtitleRenderer->texture(),
                m_quickUi->texture())
                == HdrCompositor::ResourceResult::DeviceLost) {
            handleDeviceLoss("creating the HDR compositor");
            return;
        }
        m_boundVideoTextureRevision =
            compositionTextureRevision;
        m_boundSubtitleTextureRevision =
            subtitleTextureRevision;
    } else if (videoProducerChanged
               || targetUpdate
                   == QuickUiLayer::RenderTargetUpdate::Recreated
               || m_boundVideoTextureRevision
                   != compositionTextureRevision
               || m_boundSubtitleTextureRevision
                   != subtitleTextureRevision) {
        if (m_compositor->setTextures(
                compositionVideoTexture,
                m_subtitleRenderer->texture(),
                m_quickUi->texture())
                == HdrCompositor::ResourceResult::DeviceLost) {
            handleDeviceLoss("rebinding compositor layer textures");
            return;
        }
        m_boundVideoTextureRevision =
            compositionTextureRevision;
        m_boundSubtitleTextureRevision =
            subtitleTextureRevision;
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
    bool frameFinished = false;
    const auto finishFrame =
        [this, &frameFinished](
                QRhi::EndFrameFlags flags,
                bool acceptPendingRender) {
            Q_ASSERT(!frameFinished);
            const QRhi::FrameOpResult finishResult =
                m_rhi->endFrame(m_swapChain.get(), flags);
            frameFinished = true;
            if (finishResult == QRhi::FrameOpSuccess)
                m_videoProducer->submissionAccepted();
            else
                m_videoProducer->submissionAborted();
            if (finishResult == QRhi::FrameOpSuccess
                    && acceptPendingRender) {
                m_videoProducer->commitPendingRender();
            } else {
                m_videoProducer->discardPendingRender();
            }
            return finishResult;
        };
    const auto unfinishedFrameGuard = qScopeGuard(
        [this, &finishFrame, &frameFinished] {
            if (frameFinished)
                return;
            const QRhi::FrameOpResult abandonedResult =
                finishFrame(QRhi::SkipPresent, false);
            if (abandonedResult != QRhi::FrameOpSuccess
                    && abandonedResult != QRhi::FrameOpDeviceLost) {
                qCWarning(
                    sunroomLogPresentation,
                    "QRhi returned %d while abandoning an unfinished frame",
                    static_cast<int>(abandonedResult));
            }
        });

    if (requestedSurface
            && m_videoProducer->needsRender(*requestedSurface)) {
        const VideoOperationResult renderResult =
            m_videoProducer->render(
                commandBuffer, *requestedSurface);
        if (renderResult != VideoOperationResult::Ready) {
            const QRhi::FrameOpResult abandonedResult =
                finishFrame(QRhi::SkipPresent, false);
            handleVideoOperationResult(
                "rendering the video surface",
                abandonedResult == QRhi::FrameOpDeviceLost
                    ? VideoOperationResult::DeviceLost
                    : renderResult);
            return;
        }
    }
    if (requestedSurface) {
        const VideoOperationResult compositionResult =
            m_videoProducer->prepareForComposition(commandBuffer);
        if (compositionResult != VideoOperationResult::Ready) {
            const QRhi::FrameOpResult abandonedResult =
                finishFrame(QRhi::SkipPresent, false);
            handleVideoOperationResult(
                "preparing video for composition",
                abandonedResult == QRhi::FrameOpDeviceLost
                    ? VideoOperationResult::DeviceLost
                    : compositionResult);
            return;
        }
    }
    m_subtitleRenderer->uploadIfNeeded(commandBuffer);

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

    result = finishFrame({}, true);
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
    if (requestedSurface) {
        emit videoFramePresented(
            requestedSurface->contentRevision);
    }
    scheduleNextFrame(videoViewportActive);
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

void RhiPresentationEngine::markPresentationDirty() {
    requestFrame();
}

void RhiPresentationEngine::releaseSwapChain() {
    std::optional<GraphicsDeviceExecutionScope> execution;
    if (m_graphicsDevice) {
        execution.emplace(
            m_graphicsDevice->acquireExecutionScope());
    }
    releaseSwapChainResources();
}

void RhiPresentationEngine::releaseSwapChainResources() {
    m_framePending = false;
    // The pipeline and swapchain depend on this render-pass descriptor.
    // The independent Quick and video textures intentionally survive it.
    m_compositor.reset();
    m_boundVideoTextureRevision = 0;
    m_boundSubtitleTextureRevision = 0;
    if (m_swapChain)
        m_swapChain->destroy();
    m_swapChain.reset();
    m_renderPassDescriptor.reset();
}

QQuickWindow *RhiPresentationEngine::quickWindow() const {
    return m_quickUi ? m_quickUi->quickWindow() : nullptr;
}

bool RhiPresentationEngine::initializeDevice() {
    Q_ASSERT(!m_quickUi);
    Q_ASSERT(m_graphicsDevice);
    Q_ASSERT(m_rhi);

    m_quickUi = std::make_unique<QuickUiLayer>(
        m_window,
        *m_rhi,
        m_outputState,
        m_settings,
        m_diagnosticSource,
        m_mediaSession,
        m_videoSource,
        m_videoViewport);
    connect(m_quickUi.get(), &QuickUiLayer::updateRequested,
            this, &RhiPresentationEngine::requestFrame);
    if (m_quickUi->initialize()
            == QuickUiLayer::InitializationResult::DeviceLost) {
        handleDeviceLoss("initializing Qt Quick");
        return false;
    }
    m_subtitleRenderer = std::make_unique<SubtitleRenderer>(*m_rhi);
    refreshVideoProducer();
    m_recoveringDevice = false;
    m_deviceRecoveryAttempts = 0;
    m_deviceRecoveryTimer.stop();
    return true;
}

bool RhiPresentationEngine::initializeGraphicsDevice() {
    Q_ASSERT(!m_graphicsDevice);
    Q_ASSERT(!m_rhi);
    m_graphicsDevice =
        GraphicsBackendFactory::createDeviceDomain();
    if (!m_graphicsDevice)
        return false;
    m_rhi = &m_graphicsDevice->rhi();
    m_mediaSession.setVideoDecodeCapability(
        m_graphicsDevice->videoDecodeCapability());
    return true;
}

bool RhiPresentationEngine::refreshVideoProducer() {
    Q_ASSERT(m_graphicsDevice);
    const std::uint64_t requestedRevision =
        m_videoSource.producerConfigurationRevision();
    Q_ASSERT(requestedRevision != 0);
    if (m_videoProducer
            && m_videoProducerConfigurationRevision
                == requestedRevision) {
        return false;
    }

    std::unique_ptr<RenderedVideoProducer> producer =
        m_videoSource.createProducer(*m_graphicsDevice);
    if (!producer)
        qCFatal(
            sunroomLogPresentation,
            "The video source did not create a producer");
    m_videoProducer = std::move(producer);
    m_videoProducerConfigurationRevision =
        requestedRevision;
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
            qCWarning(
                sunroomLogPresentation,
                "Could not create the QRhi swapchain; "
                "waiting for another window update");
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
        qCWarning(
            sunroomLogPresentation,
            "Could not %s the QRhi swapchain; "
            "waiting for another window update",
            operation);
        releaseSwapChainResources();
    }
    return false;
}

void RhiPresentationEngine::releaseDevice() {
    std::optional<GraphicsDeviceExecutionScope> execution;
    if (m_graphicsDevice) {
        execution.emplace(
            m_graphicsDevice->acquireExecutionScope());
    }
    // Every child resource must be gone before destroying the QRhi.
    Q_ASSERT(!m_rhi || !m_rhi->isRecordingFrame());
    releaseSwapChainResources();
    m_videoProducer.reset();
    m_videoProducerConfigurationRevision = 0;
    // Quick invalidation may emit updateRequested while it tears down.
    if (m_quickUi)
        disconnect(m_quickUi.get(), nullptr, this, nullptr);
    m_quickUi.reset();
    m_subtitleRenderer.reset();
    m_reportedSubtitleError.clear();
    m_rhi = nullptr;
    m_graphicsDevice.reset();
    m_outputCharacteristicsDirty = false;
}

void RhiPresentationEngine::handleDeviceLoss(const char *operation) {
    qCWarning(
        sunroomLogPresentation,
        "QRhi device was lost while %s; retrying",
        operation);
    if (!m_recoveringDevice)
        m_deviceRecoveryAttempts = 0;
    m_recoveringDevice = true;
    m_mediaSession.invalidateGraphicsDevice();
    releaseDevice();
    scheduleDeviceRecovery();
}

void RhiPresentationEngine::handleFrameError(
        const char *operation, int result) {
    if (m_retriedFrameError) {
        qCFatal(
            sunroomLogPresentation,
            "QRhi failed twice while %s: %d",
            operation,
            result);
    }
    qCWarning(
        sunroomLogPresentation,
        "QRhi failed while %s: %d; rebuilding once",
        operation,
        result);
    m_retriedFrameError = true;
    m_recoveringDevice = true;
    m_deviceRecoveryAttempts = 0;
    m_mediaSession.invalidateGraphicsDevice();
    releaseDevice();
    scheduleDeviceRecovery();
}

bool RhiPresentationEngine::handleVideoOperationResult(
        const char *operation, VideoOperationResult result) {
    if (result == VideoOperationResult::Ready)
        return true;
    if (result == VideoOperationResult::DeviceLost) {
        handleDeviceLoss(operation);
        return false;
    }

    updateBackendState();
    const RenderedVideoProducerDiagnostics diagnostics =
        m_videoProducer->diagnostics();
    const QString reason =
        diagnostics.target.fallbackReason;
    const QString effectiveReason = reason.isEmpty()
        ? QStringLiteral("No supported video path is available")
        : reason;
    if (m_videoSource.reportPresentationFailure(
            {
                .kind = diagnostics.failureKind
                    == VideoFailureKind::None
                    ? VideoFailureKind::General
                    : diagnostics.failureKind,
                .reason = effectiveReason,
            })) {
        qCWarning(
            sunroomLogPresentation,
            "Video path unavailable while %s: %s",
            operation,
            qPrintable(effectiveReason));
        requestFrame();
        return false;
    }
    qCFatal(
        sunroomLogPresentation,
        "Video path unavailable while %s: %s",
        operation,
        qPrintable(effectiveReason));
    return false;
}

void RhiPresentationEngine::scheduleDeviceRecovery() {
    constexpr int maximumAttempts = 8;
    constexpr auto retryDelay = std::chrono::milliseconds(250);

    if (m_deviceRecoveryAttempts >= maximumAttempts) {
        qCFatal(
            sunroomLogPresentation,
            "QRhi device recovery failed after %d attempts",
            maximumAttempts);
    }

    ++m_deviceRecoveryAttempts;
    qCWarning(
        sunroomLogPresentation,
        "Retrying QRhi device recovery (%d/%d)",
        m_deviceRecoveryAttempts,
        maximumAttempts);
    m_deviceRecoveryTimer.start(retryDelay);
}

void RhiPresentationEngine::updateBackendState() {
    PresentationBackendState state;
    const GraphicsDeviceDiagnostics &graphicsDiagnostics =
        m_graphicsDevice->diagnostics();
    state.graphicsApi = graphicsDiagnostics.backendName;
    state.graphicsAdapter = graphicsDiagnostics.adapterName;
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
    const RenderedVideoProducerDiagnostics videoDiagnostics =
        m_videoProducer->diagnostics();
    state.videoSurfaceProducer = videoDiagnostics.producerName;
    state.videoInputPath = videoDiagnostics.inputPath;
    state.videoColorPolicy = videoDiagnostics.colorPolicy;
    state.videoOutputPath =
        videoOutputPathName(videoDiagnostics.target.outputPath);
    state.videoSynchronization =
        videoDiagnostics.target.synchronizationMode;
    state.videoCopySummary =
        QStringLiteral(
            "%1 input CPU transfers per input frame · "
            "%2 input GPU copies per input frame · "
            "%3 output GPU copies · "
            "%4 output CPU transfers per render")
            .arg(
                videoDiagnostics
                    .knownInputCpuTransfersPerInputFrame)
            .arg(
                videoDiagnostics
                    .knownInputGpuCopiesPerInputFrame)
            .arg(
                videoDiagnostics.target
                    .knownOutputGpuCopiesPerRender)
            .arg(
                videoDiagnostics.target
                    .knownOutputCpuTransfersPerRender);
    state.videoFallbackReason =
        videoDiagnostics.target.fallbackReason;

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

void RhiPresentationEngine::scheduleNextFrame(bool videoViewportActive) {
    if ((videoViewportActive && m_videoSource.wantsContinuousFrames())
        || m_quickUi->isDirty()) {
        requestFrame();
    }
}

void RhiPresentationEngine::markOutputCharacteristicsDirty() {
    m_outputCharacteristicsDirty = true;
    markPresentationDirty();
}

void RhiPresentationEngine::reconcileOutputCharacteristics() {
    if (!m_outputCharacteristicsDirty)
        return;
    m_outputCharacteristicsDirty = false;
    if (!m_swapChain)
        return;

    const bool extendedLinearSupported =
        m_swapChain->isFormatSupported(
            QRhiSwapChain::HDRExtendedSrgbLinear);
    const QRhiSwapChain::Format desiredFormat =
        extendedLinearSupported
        ? QRhiSwapChain::HDRExtendedSrgbLinear
        : QRhiSwapChain::SDR;
    if (m_swapChain->format() != desiredFormat) {
        releaseSwapChainResources();
        return;
    }

    // Refresh QRhi luminance/headroom fallback data without rebuilding an
    // already compatible presentation surface.
    updateBackendState();
}
