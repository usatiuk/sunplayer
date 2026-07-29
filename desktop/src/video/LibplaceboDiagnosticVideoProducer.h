#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <libplacebo/renderer.h>

#include "video/RenderedVideoProducer.h"

class DiagnosticVideoSource;
class GraphicsDeviceDomain;
class QRhi;

// Fixed-size analytic input that exercises the software-frame upload branch of
// the production libplacebo renderer and backend target bridge. Normal
// playback can replace this input with software planes or hardware surfaces.
class LibplaceboDiagnosticVideoProducer final
    : public RenderedVideoProducer {
public:
    LibplaceboDiagnosticVideoProducer(
        GraphicsDeviceDomain &graphicsDevice,
        const DiagnosticVideoSource &source,
        VideoTargetReadback readback);
    ~LibplaceboDiagnosticVideoProducer() override;

    LibplaceboDiagnosticVideoProducer(
        const LibplaceboDiagnosticVideoProducer &) = delete;
    LibplaceboDiagnosticVideoProducer &operator=(
        const LibplaceboDiagnosticVideoProducer &) = delete;

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
    struct SourceUploadKey {
        bool operator==(const SourceUploadKey &) const = default;

        QSize pixelSize;
        float sourcePeakHeadroom = 0.0f;
        float phase = 0.0f;
        float encodingReferenceWhiteNits = 0.0f;
    };

    bool createSourceTexture(const QSize &sourceSize);
    void updateSourcePixels(float referenceWhiteNits);
    SourceUploadKey sourceUploadKey(float referenceWhiteNits) const;
    bool deviceLost() const;
    VideoOperationResult unavailable(const QString &reason);
    static pl_hook_res applyReferenceWhiteScale(
        void *privateData,
        const pl_hook_params *parameters);

    QRhi &m_rhi;
    pl_gpu m_gpu = nullptr;
    const DiagnosticVideoSource &m_source;
    std::unique_ptr<VideoTargetInterop> m_target;
    pl_renderer m_renderer = nullptr;
    pl_tex m_sourceTexture = nullptr;
    float m_referenceWhiteScale = 1.0f;
    pl_shader_var m_referenceWhiteScaleVariable{};
    pl_hook m_referenceWhiteHook{};
    QSize m_sourceSize;
    QSize m_targetSize;
    std::vector<std::array<float, 5>>
        m_encodedPatternColumns;
    std::vector<float> m_sourcePixels;
    std::optional<SourceUploadKey> m_uploadedSourceKey;
    QString m_failureReason;
    std::optional<RenderedVideoSurfaceState> m_completedState;
    std::optional<RenderedVideoSurfaceState> m_pendingState;
};
