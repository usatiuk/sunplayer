#pragma once

#include <cstdint>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QTimer>

class GraphicsDeviceDomain;
class HdrCompositor;
class ActiveVideoSource;
class DiagnosticVideoSource;
class MediaSession;
class PresentationOutputState;
class PresentationSettings;
class QQuickWindow;
class QScreen;
class QRhi;
class QRhiRenderPassDescriptor;
class QRhiSwapChain;
class QWindow;
class QuickUiLayer;
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
                          QObject *parent = nullptr);
    ~RhiPresentationEngine() override;

    void render();
    void handleExposure();
    void requestFrame();
    void markUiDirty();
    void markPresentationDirty();
    void releaseSwapChain();

    QQuickWindow *quickWindow() const;

private:
    void renderFrame();
    bool initializeDevice();
    bool initializeGraphicsDevice();
    bool refreshVideoProducer();
    bool createSwapChain();
    bool resizeSwapChain(bool force = false);
    bool createOrResizeSwapChain(const char *operation);
    void releaseSwapChainResources();
    void releaseDevice();
    void handleDeviceLoss(const char *operation);
    void handleFrameError(const char *operation, int result);
    bool handleVideoOperationResult(
        const char *operation, VideoOperationResult result);
    void scheduleDeviceRecovery();
    void updateBackendState();
    void scheduleNextFrame(bool videoLayerActive);
    void requestSwapChainRecreation();
    void scheduleOutputVerification();
    void verifyOutput();

    QWindow &m_window;
    PresentationOutputState &m_outputState;
    PresentationSettings &m_settings;
    ActiveVideoSource &m_videoSource;
    DiagnosticVideoSource &m_diagnosticSource;
    MediaSession &m_mediaSession;
    VideoViewportState &m_videoViewport;
    std::unique_ptr<GraphicsDeviceDomain> m_graphicsDevice;
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiSwapChain> m_swapChain;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPassDescriptor;
    std::unique_ptr<QuickUiLayer> m_quickUi;
    std::unique_ptr<RenderedVideoProducer> m_videoProducer;
    std::unique_ptr<HdrCompositor> m_compositor;
    // Output selected when the current swapchain was successfully created.
    QPointer<QScreen> m_swapChainScreen;
    QTimer m_outputVerificationTimer;
    QTimer m_deviceRecoveryTimer;
    bool m_rendering = false;
    // Mirrors an outstanding QWindow UpdateRequest owned by this engine.
    bool m_framePending = false;
    // Captures synchronous dirty signals without recursively entering render().
    bool m_frameRequestedWhileRendering = false;
    // Resource mutation is deferred from display callbacks to the render point.
    bool m_recreateSwapChain = false;
    bool m_recoveringDevice = false;
    bool m_retriedFrameError = false;
    int m_deviceRecoveryAttempts = 0;
    std::uint64_t m_videoProducerConfigurationRevision = 0;
    std::uint64_t m_boundVideoTextureRevision = 0;
};
