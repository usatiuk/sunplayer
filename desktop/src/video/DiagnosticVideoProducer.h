#pragma once

#include <memory>
#include <optional>

#include "video/RenderedVideoProducer.h"

class DiagnosticVideoSource;
class GraphicsDeviceDomain;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;

class DiagnosticVideoProducer final : public RenderedVideoProducer {
public:
    explicit DiagnosticVideoProducer(
        GraphicsDeviceDomain &graphicsDevice,
        const DiagnosticVideoSource &source,
        VideoTargetReadback readback = VideoTargetReadback::Disabled);
    ~DiagnosticVideoProducer() override;

    DiagnosticVideoProducer(const DiagnosticVideoProducer &) = delete;
    DiagnosticVideoProducer &operator=(const DiagnosticVideoProducer &) =
        delete;

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

private:
    VideoOperationResult createResources();

    QRhi &m_rhi;
    const DiagnosticVideoSource &m_source;
    std::unique_ptr<VideoTargetInterop> m_target;
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::optional<RenderedVideoSurfaceState> m_completedState;
    std::optional<RenderedVideoSurfaceState> m_pendingState;
};
