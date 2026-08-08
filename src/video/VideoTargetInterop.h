#pragma once

#include <cstdint>

#include <QString>

#include "video/RenderedVideoSurface.h"

struct pl_tex_t;
using pl_tex = pl_tex_t const*;

class QRhiCommandBuffer;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;

enum class VideoProducerApi {
    Qrhi,
    Libplacebo,
};

enum class VideoOutputPath {
    Unavailable,
    DirectRenderTarget,
    SameDeviceGpuCopy,
    CpuRoundTrip,
};

enum class VideoTargetReadback {
    Disabled,
    Enabled,
};

enum class VideoTargetUpdate {
    Unchanged,
    Created,
    Resized,
    DeviceLost,
    Unavailable,
};

enum class VideoOperationResult {
    Ready,
    DeviceLost,
    Unavailable,
};

struct VideoTargetRequest {
    VideoProducerApi producerApi = VideoProducerApi::Qrhi;
    VideoTargetReadback readback = VideoTargetReadback::Disabled;
};

struct VideoTargetInteropDiagnostics {
    VideoOutputPath outputPath = VideoOutputPath::Unavailable;
    QString synchronizationMode;
    std::uint32_t knownOutputGpuCopiesPerRender = 0;
    std::uint32_t knownOutputCpuTransfersPerRender = 0;
    QString fallbackReason;

    bool isValid() const;
};

QString videoOutputPathName(VideoOutputPath path);

// Owns the texture boundary between a video renderer and QRhi composition.
// Backend implementations retain all native allocation and synchronization.
class VideoTargetInterop {
  public:
    virtual ~VideoTargetInterop() = default;

    virtual VideoTargetUpdate ensureTarget(RenderedVideoSurfaceDescription const& description) = 0;
    // Brackets producer access so native backends can transfer ownership or
    // synchronize external commands. Pure QRhi rendering needs no extra work.
    virtual VideoOperationResult beginProducerAccess(QRhiCommandBuffer& commandBuffer) = 0;
    virtual VideoOperationResult endProducerAccess(QRhiCommandBuffer& commandBuffer) = 0;
    virtual VideoOperationResult prepareForComposition(QRhiCommandBuffer& commandBuffer) = 0;
    // Called after QRhi accepts or rejects the frame submission. Acceptance
    // does not imply that GPU execution has already completed.
    virtual void submissionAccepted() = 0;
    virtual void submissionAborted() = 0;

    // QRhi producers use these cross-platform views. Native producers can
    // return null and keep their renderer-facing objects in a derived backend.
    virtual QRhiTextureRenderTarget* qrhiRenderTarget() const = 0;
    virtual QRhiRenderPassDescriptor* qrhiRenderPassDescriptor() const = 0;
    // Libplacebo producers use this cross-platform view. QRhi producers
    // return null. Backend-native texture handles never cross this boundary.
    virtual pl_tex libplaceboRenderTarget() const = 0;
    virtual QRhiTexture& textureForComposition() const = 0;
    virtual std::uint64_t compositionTextureRevision() const = 0;
    virtual VideoTargetInteropDiagnostics const& diagnostics() const = 0;
};
