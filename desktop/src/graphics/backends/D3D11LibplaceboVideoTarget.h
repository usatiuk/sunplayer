#pragma once

#include <memory>

#include <libplacebo/gpu.h>

#include "video/VideoTargetInterop.h"

class QRhi;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;

// Owns the D3D11-specific relationship between a QRhi composition texture and
// the pl_tex that renders into the same native allocation.
class D3D11LibplaceboVideoTarget final : public VideoTargetInterop {
public:
    D3D11LibplaceboVideoTarget(
        QRhi &rhi,
        pl_gpu gpu,
        VideoTargetReadback readback);
    ~D3D11LibplaceboVideoTarget() override;

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
    bool wrapTexture();
    bool deviceLost() const;
    void resetTarget();
    void setDirectDiagnostics();
    void setUnavailableDiagnostics(const QString &reason);
    void advanceTextureRevision();

    QRhi &m_rhi;
    pl_gpu m_gpu = nullptr;
    VideoTargetReadback m_readback;
    VideoTargetInteropDiagnostics m_diagnostics;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPassDescriptor;
    std::unique_ptr<QRhiTextureRenderTarget> m_renderTarget;
    pl_tex m_wrappedTexture = nullptr;
    bool m_producerAccessActive = false;
    bool m_submissionPending = false;
    bool m_compositionPrepared = false;
    std::uint64_t m_compositionTextureRevision = 0;
};
