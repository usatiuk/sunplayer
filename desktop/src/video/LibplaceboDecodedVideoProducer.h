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

class LibplaceboDecodedVideoProducer final
    : public RenderedVideoProducer {
public:
    LibplaceboDecodedVideoProducer(
        GraphicsDeviceDomain &graphicsDevice,
        const DecodedVideoSource &source,
        VideoTargetReadback readback);
    ~LibplaceboDecodedVideoProducer() override;

    LibplaceboDecodedVideoProducer(
        const LibplaceboDecodedVideoProducer &) = delete;
    LibplaceboDecodedVideoProducer &operator=(
        const LibplaceboDecodedVideoProducer &) = delete;

    VideoOperationResult ensureSurface(
        const RenderedVideoSurfaceState &requestedState) override;
    bool needsRender(
        const RenderedVideoSurfaceState &requestedState) const override;
    VideoOperationResult render(
        QRhiCommandBuffer &commandBuffer,
        const RenderedVideoSurfaceState &requestedState) override;
    VideoOperationResult prepareForComposition(
        QRhiCommandBuffer &commandBuffer) override;
    void submissionAccepted() override;
    void submissionAborted() override;
    void commitPendingRender() override;
    void discardPendingRender() override;

    QRhiTexture &textureForComposition() const override;
    std::uint64_t compositionTextureRevision() const override;
    RenderedVideoProducerDiagnostics diagnostics() const override;

    const VideoFrameImportDiagnostics &
        frameImportDiagnostics() const;
    std::uint64_t inputImportCount() const;

private:
    bool deviceLost() const;
    VideoOperationResult unavailable(const QString &reason);

    QRhi &m_rhi;
    pl_gpu m_gpu = nullptr;
    const DecodedVideoSource &m_source;
    std::unique_ptr<VideoTargetInterop> m_target;
    std::unique_ptr<LibplaceboRenderContext> m_renderContext;
    std::unique_ptr<LibplaceboFrameImporter> m_importer;
    std::unique_ptr<LibplaceboFrameImporter::Mapping> m_mapping;
    std::shared_ptr<const DecodedVideoFrame> m_mappedSourceFrame;
    QString m_failureReason;
    std::optional<RenderedVideoSurfaceState> m_completedState;
    std::optional<RenderedVideoSurfaceState> m_pendingState;
};
