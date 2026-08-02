#pragma once

#include <memory>

#include "video/VideoTargetInterop.h"

class QRhi;
class QRhiRenderPassDescriptor;
class QRhiTextureRenderTarget;

// Direct QRhi target used by the diagnostic producer. It exercises the shared
// target ownership and diagnostics contract without native interop.
class QrhiVideoTarget final : public VideoTargetInterop {
public:
    explicit QrhiVideoTarget(
        QRhi &rhi,
        VideoTargetReadback readback = VideoTargetReadback::Disabled);
    ~QrhiVideoTarget() override;

    VideoTargetUpdate ensureTarget(
        const RenderedVideoSurfaceDescription &description) override;
    VideoOperationResult beginProducerAccess(
        QRhiCommandBuffer &commandBuffer) override;
    VideoOperationResult endProducerAccess(
        QRhiCommandBuffer &commandBuffer) override;
    VideoOperationResult prepareForComposition(
        QRhiCommandBuffer &commandBuffer) override;
    void submissionAccepted() override;
    void submissionAborted() override;
    QRhiTextureRenderTarget *qrhiRenderTarget() const override;
    QRhiRenderPassDescriptor *
        qrhiRenderPassDescriptor() const override;
    pl_tex libplaceboRenderTarget() const override;
    QRhiTexture &textureForComposition() const override;
    std::uint64_t compositionTextureRevision() const override;
    const VideoTargetInteropDiagnostics &diagnostics() const override;

private:
    VideoTargetUpdate createTarget(const QSize &pixelSize);
    VideoTargetUpdate resizeTarget(const QSize &pixelSize);
    void setDirectDiagnostics();
    void setUnavailableDiagnostics(const QString &reason);

    QRhi &m_rhi;
    VideoTargetReadback m_readback;
    VideoTargetInteropDiagnostics m_diagnostics;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPassDescriptor;
    std::unique_ptr<QRhiTextureRenderTarget> m_renderTarget;
    bool m_producerAccessActive = false;
    bool m_submissionPending = false;
    bool m_compositionPrepared = false;
    std::uint64_t m_compositionTextureRevision = 0;
};
