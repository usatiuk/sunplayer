#pragma once

#include <memory>
#include <optional>

#include "media/DecodedVideoFrame.h"
#include "video/RenderedVideoProducer.h"
#include "video/libplacebo/LibplaceboFrameImporter.h"

class DecodedVideoSource;
class GraphicsDeviceDomain;
class LibplaceboRenderContext;
class QRhi;

class LibplaceboDecodedVideoProducer final : public RenderedVideoProducer {
  public:
    LibplaceboDecodedVideoProducer(GraphicsDeviceDomain& graphicsDevice, DecodedVideoSource const& source,
                                   VideoTargetReadback readback);
    ~LibplaceboDecodedVideoProducer() override;

    LibplaceboDecodedVideoProducer(LibplaceboDecodedVideoProducer const&) = delete;
    LibplaceboDecodedVideoProducer& operator=(LibplaceboDecodedVideoProducer const&) = delete;

    VideoOperationResult ensureSurface(RenderedVideoSurfaceState const& requestedState) override;
    bool needsRender(RenderedVideoSurfaceState const& requestedState) const override;
    VideoOperationResult render(QRhiCommandBuffer& commandBuffer,
                                RenderedVideoSurfaceState const& requestedState) override;
    VideoOperationResult prepareForComposition(QRhiCommandBuffer& commandBuffer) override;
    void submissionAccepted() override;
    void submissionAborted() override;
    void commitPendingRender() override;
    void discardPendingRender() override;

    QRhiTexture& textureForComposition() const override;
    std::uint64_t compositionTextureRevision() const override;
    RenderedVideoProducerDiagnostics diagnostics() const override;

    VideoFrameImportDiagnostics const& frameImportDiagnostics() const;
    std::uint64_t inputImportCount() const;

  private:
    bool deviceLost() const;
    VideoOperationResult unavailable(QString const& reason, VideoFailureKind failureKind = VideoFailureKind::General);

    QRhi& m_rhi;
    pl_gpu m_gpu = nullptr;
    DecodedVideoSource const& m_source;
    std::unique_ptr<VideoTargetInterop> m_target;
    std::unique_ptr<LibplaceboRenderContext> m_renderContext;
    std::unique_ptr<LibplaceboFrameImporter> m_importer;
    std::unique_ptr<LibplaceboFrameImporter::Mapping> m_mapping;
    std::shared_ptr<DecodedVideoFrame const> m_mappedSourceFrame;
    QString m_failureReason;
    VideoFailureKind m_failureKind = VideoFailureKind::None;
    std::optional<RenderedVideoSurfaceState> m_completedState;
    std::optional<RenderedVideoSurfaceState> m_pendingState;
};
