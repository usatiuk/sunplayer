#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <libplacebo/renderer.h>

#include "video/RenderedVideoProducer.h"

class DiagnosticVideoSource;
class GraphicsDeviceDomain;
class LibplaceboRenderContext;
class QRhi;

// Fixed-size analytic input that exercises the software-frame upload branch of
// the production libplacebo renderer and backend target bridge. Normal
// playback can replace this input with software planes or hardware surfaces.
class LibplaceboDiagnosticVideoProducer final : public RenderedVideoProducer {
  public:
    LibplaceboDiagnosticVideoProducer(GraphicsDeviceDomain& graphicsDevice, DiagnosticVideoSource const& source,
                                      VideoTargetReadback readback);
    ~LibplaceboDiagnosticVideoProducer() override;

    LibplaceboDiagnosticVideoProducer(LibplaceboDiagnosticVideoProducer const&) = delete;
    LibplaceboDiagnosticVideoProducer& operator=(LibplaceboDiagnosticVideoProducer const&) = delete;

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
    std::uint64_t sourceUploadCount() const;

  private:
    struct SourceUploadKey {
        bool operator==(SourceUploadKey const&) const = default;

        QSize pixelSize;
        float sourcePeakHeadroom = 0.0f;
        float phase = 0.0f;
    };

    bool createSourceTexture(QSize const& sourceSize);
    void updateSourcePixels();
    SourceUploadKey sourceUploadKey() const;
    bool deviceLost() const;
    VideoOperationResult unavailable(QString const& reason);

    QRhi& m_rhi;
    pl_gpu m_gpu = nullptr;
    DiagnosticVideoSource const& m_source;
    std::unique_ptr<VideoTargetInterop> m_target;
    std::unique_ptr<LibplaceboRenderContext> m_renderContext;
    pl_tex m_sourceTexture = nullptr;
    QSize m_sourceSize;
    std::vector<std::array<float, 5>> m_encodedPatternColumns;
    std::vector<float> m_sourcePixels;
    std::optional<SourceUploadKey> m_uploadedSourceKey;
    std::uint64_t m_sourceUploadCount = 0;
    QString m_failureReason;
    std::optional<RenderedVideoSurfaceState> m_completedState;
    std::optional<RenderedVideoSurfaceState> m_pendingState;
};
