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
    explicit QrhiVideoTarget(QRhi& rhi, VideoTargetReadback readback = VideoTargetReadback::Disabled);
    ~QrhiVideoTarget() override;

    VideoTargetUpdate ensureTarget(RenderedVideoSurfaceDescription const& description) override;
    VideoOperationResult beginProducerAccess(QRhiCommandBuffer& commandBuffer) override;
    VideoOperationResult endProducerAccess(QRhiCommandBuffer& commandBuffer) override;
    VideoOperationResult prepareForComposition(QRhiCommandBuffer& commandBuffer) override;
    void submissionAccepted() override;
    void submissionAborted() override;
    QRhiTextureRenderTarget* qrhiRenderTarget() const override;
    QRhiRenderPassDescriptor* qrhiRenderPassDescriptor() const override;
    pl_tex libplaceboRenderTarget() const override;
    QRhiTexture& textureForComposition() const override;
    std::uint64_t compositionTextureRevision() const override;
    VideoTargetInteropDiagnostics const& diagnostics() const override;

  private:
    VideoTargetUpdate createTarget(QSize const& pixelSize);
    VideoTargetUpdate resizeTarget(QSize const& pixelSize);
    void setDirectDiagnostics();
    void setUnavailableDiagnostics(QString const& reason);

    QRhi& m_rhi;
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
