#pragma once

#include <memory>

#include <QObject>
#include <QSize>

class PresentationOutputState;
class PresentationSettings;
class ActiveVideoSource;
class DiagnosticVideoSource;
class MediaSession;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;
class QQmlEngine;
class QRhi;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QWindow;
class VideoViewportState;

// Redirected Qt Quick scene exposed as one compositor texture.
class QuickUiLayer final : public QObject {
    Q_OBJECT

  public:
    enum class InitializationResult {
        Ready,
        DeviceLost,
    };

    enum class RenderTargetUpdate {
        Unchanged,
        Recreated,
        DeviceLost,
    };

    QuickUiLayer(QWindow& renderWindow, QRhi& rhi, PresentationOutputState& outputState, PresentationSettings& settings,
                 DiagnosticVideoSource& diagnosticSource, MediaSession& mediaSession,
                 ActiveVideoSource& activeVideoSource, VideoViewportState& videoViewport, QObject* parent = nullptr);
    ~QuickUiLayer() override;

    InitializationResult initialize();
    void setLogicalSize(QSize const& size);
    RenderTargetUpdate ensureRenderTarget(QSize const& pixelSize, qreal devicePixelRatio);
    void renderIfDirty();
    void markDirty();

    bool isDirty() const;
    QRhiTexture& texture() const;
    QQuickWindow* quickWindow() const;

  signals:
    void updateRequested();

  private:
    void configureRenderTarget();
    void releaseRenderTarget();

    QWindow& m_renderWindow;
    QRhi& m_rhi;
    PresentationOutputState& m_outputState;
    PresentationSettings& m_settings;
    DiagnosticVideoSource& m_diagnosticSource;
    MediaSession& m_mediaSession;
    ActiveVideoSource& m_activeVideoSource;
    VideoViewportState& m_videoViewport;
    std::unique_ptr<QQuickRenderControl> m_renderControl;
    std::unique_ptr<QQuickWindow> m_quickWindow;
    std::unique_ptr<QQmlEngine> m_qmlEngine;
    std::unique_ptr<QQuickItem> m_rootItem;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiRenderBuffer> m_depthStencilBuffer;
    std::unique_ptr<QRhiTextureRenderTarget> m_renderTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPassDescriptor;
    QSize m_logicalSize;
    QSize m_pixelSize;
    // DPR comes from renderWindow(); this copy only detects rerasterization.
    qreal m_devicePixelRatio = 0.0;
    bool m_dirty = true;
};
