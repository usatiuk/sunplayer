#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include <QObject>
#include <QPointer>
#include <QTimer>

class HdrCompositor;
class PresentationOutputState;
class PresentationSettings;
class QQuickWindow;
class QScreen;
class QRhi;
class QRhiRenderPassDescriptor;
class QRhiSwapChain;
class QWindow;
class QuickUiLayer;

class RhiPresentationEngine final : public QObject {
    Q_OBJECT

public:
    RhiPresentationEngine(QWindow &window,
                          PresentationOutputState &outputState,
                          PresentationSettings &settings,
                          QObject *parent = nullptr);
    ~RhiPresentationEngine() override;

    void render();
    void handleExposure();
    void requestFrame();
    void markUiDirty();
    void markCanvasDirty();
    void markPresentationDirty();
    void releaseSwapChain();

    QQuickWindow *quickWindow() const;

private:
    void renderFrame();
    bool initializeDevice();
    bool createSwapChain();
    bool resizeSwapChain(bool force = false);
    bool createOrResizeSwapChain(const char *operation);
    void releaseDevice();
    void handleDeviceLoss(const char *operation);
    void handleFrameError(const char *operation, int result);
    void scheduleDeviceRecovery();
    void updateBackendState();
    void scheduleNextFrame();
    void requestSwapChainRecreation();
    void scheduleOutputVerification();
    void verifyOutput();

    QWindow &m_window;
    PresentationOutputState &m_outputState;
    PresentationSettings &m_settings;
    std::unique_ptr<QRhi> m_rhi;
    std::unique_ptr<QRhiSwapChain> m_swapChain;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPassDescriptor;
    std::unique_ptr<QuickUiLayer> m_quickUi;
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
    float m_phase = 0.0f;
    std::optional<std::chrono::steady_clock::time_point> m_lastAnimationFrame;
};
