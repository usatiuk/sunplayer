#pragma once

#include <cstdint>
#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>

#include "presentation/PresentationSurfaceContract.h"

class GraphicsDeviceDomain;
class HdrCompositor;
class ActiveVideoSource;
class DiagnosticVideoSource;
class MediaSession;
class PresentationOutputState;
class PresentationSettings;
class QQuickWindow;
class QRhi;
class QRhiRenderPassDescriptor;
class QRhiSwapChain;
class QWindow;
class QuickUiLayer;
class SubtitleRenderer;
class RenderedVideoProducer;
class VideoViewportState;
enum class VideoOperationResult;

// Owns one presentation domain and sequences its QRhi work.
class RhiPresentationEngine final : public QObject {
    Q_OBJECT

public:
    RhiPresentationEngine(QWindow &window,
                          PresentationOutputState &outputState,
                          PresentationSettings &settings,
                          ActiveVideoSource &videoSource,
                          DiagnosticVideoSource &diagnosticSource,
                          MediaSession &mediaSession,
                          VideoViewportState &videoViewport,
                          PresentationSurfaceContract surfaceContract,
                          PresentationSurfaceController *surfaceController,
                          QObject *parent = nullptr);
    ~RhiPresentationEngine() override;

    void render();
    void handleExposure();
    void requestFrame();
    void markUiDirty();
    void markPresentationDirty();
    void releaseSwapChain();

    QQuickWindow *quickWindow() const;

signals:
    // Emitted only after the swapchain accepts a frame containing the active
    // video surface. Application scenarios can observe real presentation
    // progress without reaching into renderer internals or using screenshots.
    void videoFramePresented(qulonglong contentRevision);

private:
    void renderFrame();
    bool initializeDevice();
    bool initializeGraphicsDevice();
    bool refreshVideoProducer();
    bool createSwapChain();
    bool resizeSwapChain(bool force = false);
    bool createOrResizeSwapChain(const char *operation);
    void handleSwapChainFailure(
        const char *operation,
        const char *hdrRejectionReason);
    void scheduleSwapChainRecovery(const char *operation);
    void completePresentationRecovery();
    void releaseSwapChainResources();
    void releaseDevice();
    void handleDeviceLoss(const char *operation);
    void handleFrameError(const char *operation, int result);
    bool handleVideoOperationResult(
        const char *operation, VideoOperationResult result);
    void scheduleDeviceRecovery();
    void updateBackendState();
    void scheduleNextFrame(bool videoViewportActive);
    void markOutputCharacteristicsDirty();
    bool reconcileOutputCharacteristics();
    void queueSurfaceTransition();
    void rejectRequiredHdrSurface(const char *reason);
    void rebuildForPresentIncompatibleSurface();

    QWindow &m_window;
    PresentationOutputState &m_outputState;
    PresentationSettings &m_settings;
    ActiveVideoSource &m_videoSource;
    DiagnosticVideoSource &m_diagnosticSource;
    MediaSession &m_mediaSession;
    VideoViewportState &m_videoViewport;
    PresentationSurfaceContract m_surfaceContract;
    PresentationSurfaceController *m_surfaceController = nullptr;
    std::unique_ptr<GraphicsDeviceDomain> m_graphicsDevice;
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiSwapChain> m_swapChain;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPassDescriptor;
    std::unique_ptr<QuickUiLayer> m_quickUi;
    std::unique_ptr<SubtitleRenderer> m_subtitleRenderer;
    std::unique_ptr<RenderedVideoProducer> m_videoProducer;
    std::unique_ptr<HdrCompositor> m_compositor;
    QTimer m_deviceRecoveryTimer;
    QTimer m_swapChainRecoveryTimer;
    bool m_rendering = false;
    // Mirrors an outstanding QWindow UpdateRequest owned by this engine.
    bool m_framePending = false;
    // Captures synchronous dirty signals without recursively entering render().
    bool m_frameRequestedWhileRendering = false;
    // Native display callbacks are hints. Query and mutate at the render point.
    bool m_outputCharacteristicsDirty = false;
    bool m_surfaceTransitionPending = false;
    bool m_surfaceDeclarationPending = false;
#ifdef Q_OS_MACOS
    // Qt's Cocoa backing-property propagation can replace CAMetalLayer's
    // colorspace when the window changes screens. QRhi reapplies the selected
    // presentation colorspace from createOrResize().
    bool m_swapChainSurfaceDirty = false;
#endif
    bool m_recoveringDevice = false;
    bool m_retriedFrameError = false;
    int m_deviceRecoveryAttempts = 0;
    int m_swapChainRecoveryAttempts = 0;
    std::uint64_t m_videoProducerConfigurationRevision = 0;
    std::uint64_t m_boundVideoTextureRevision = 0;
    std::uint64_t m_boundSubtitleTextureRevision = 0;
    QString m_reportedSubtitleError;
};
