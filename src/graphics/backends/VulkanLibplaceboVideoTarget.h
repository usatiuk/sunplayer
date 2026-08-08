#pragma once

#include <cstdint>
#include <memory>

#include <libplacebo/gpu.h>
#include <vulkan/vulkan.h>

#include "video/VideoTargetInterop.h"

class QRhi;
class QRhiTexture;
class QVulkanDeviceFunctions;

// Shares one QRhi-owned Vulkan image with libplacebo. One same-queue timeline
// dependency orders QRhi before the producer, and one barrier recorded into
// QRhi's command buffer makes producer writes visible before sampling.
class VulkanLibplaceboVideoTarget final : public VideoTargetInterop {
  public:
    VulkanLibplaceboVideoTarget(QRhi& rhi, pl_gpu gpu, QVulkanDeviceFunctions& deviceFunctions, VkQueue graphicsQueue,
                                std::uint32_t graphicsQueueFamily, VideoTargetReadback readback);
    ~VulkanLibplaceboVideoTarget() override;

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
    bool wrapTexture();
    VideoOperationResult signalQrhiCompletion();
    void recordCompositionBarrier(QRhiCommandBuffer& commandBuffer);
    bool deviceLost() const;
    void resetTarget();
    void setDirectDiagnostics();
    void setUnavailableDiagnostics(QString const& reason);
    void advanceTextureRevision();

    QRhi& m_rhi;
    pl_gpu m_gpu = nullptr;
    QVulkanDeviceFunctions& m_deviceFunctions;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsQueueFamily = 0;
    VideoTargetReadback m_readback;
    VideoTargetInteropDiagnostics m_diagnostics;
    std::unique_ptr<QRhiTexture> m_texture;
    pl_tex m_wrappedTexture = nullptr;
    VkSemaphore m_handoffSemaphore = VK_NULL_HANDLE;
    std::uint64_t m_handoffValue = 0;
    bool m_compositionBarrierPending = false;
    bool m_libplaceboAccessActive = false;
    bool m_submissionPending = false;
    bool m_compositionPrepared = false;
    std::uint64_t m_compositionTextureRevision = 0;
};
