#pragma once

#include <QString>

#include "video/RenderedVideoSurface.h"
#include "video/VideoFailure.h"
#include "video/VideoTargetInterop.h"

class QRhiCommandBuffer;
class QRhiTexture;

struct RenderedVideoProducerDiagnostics {
    QString producerName;
    QString inputPath;
    QString colorPolicy;
    std::uint32_t knownInputCpuTransfersPerInputFrame = 0;
    std::uint32_t knownInputGpuCopiesPerInputFrame = 0;
    VideoFailureKind failureKind = VideoFailureKind::None;
    VideoTargetInteropDiagnostics target;

    bool isValid() const {
        return !producerName.isEmpty() && !inputPath.isEmpty() && !colorPolicy.isEmpty() && target.isValid();
    }
};

class RenderedVideoProducer {
  public:
    virtual ~RenderedVideoProducer() = default;

    virtual VideoOperationResult ensureSurface(RenderedVideoSurfaceState const& requestedState) = 0;
    virtual bool needsRender(RenderedVideoSurfaceState const& requestedState) const = 0;
    virtual VideoOperationResult render(QRhiCommandBuffer& commandBuffer,
                                        RenderedVideoSurfaceState const& requestedState) = 0;
    virtual VideoOperationResult prepareForComposition(QRhiCommandBuffer& commandBuffer) = 0;
    // Submission acceptance is distinct from accepting a newly rendered
    // content state: reused surfaces still participate in compositor reads.
    virtual void submissionAccepted() = 0;
    virtual void submissionAborted() = 0;
    virtual void commitPendingRender() = 0;
    virtual void discardPendingRender() = 0;

    virtual QRhiTexture& textureForComposition() const = 0;
    virtual std::uint64_t compositionTextureRevision() const = 0;
    virtual RenderedVideoProducerDiagnostics diagnostics() const = 0;
};
