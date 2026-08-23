#include "graphics/backends/VulkanLibplaceboVideoTarget.h"

#include <QVulkanFunctions>
#include <QtCore/qlogging.h>
#include <libplacebo/vulkan.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include "diagnostics/LogCategories.h"

VulkanLibplaceboVideoTarget::VulkanLibplaceboVideoTarget(QRhi& rhi, pl_gpu gpu, QVulkanDeviceFunctions& deviceFunctions,
                                                         VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily,
                                                         VideoTargetReadback readback)
    : m_rhi(rhi), m_gpu(gpu), m_deviceFunctions(deviceFunctions), m_graphicsQueue(graphicsQueue),
      m_graphicsQueueFamily(graphicsQueueFamily), m_readback(readback) {
    Q_ASSERT(m_gpu);
    Q_ASSERT(m_graphicsQueue != VK_NULL_HANDLE);
    pl_vulkan_sem_params semaphoreParameters{};
    semaphoreParameters.type = VK_SEMAPHORE_TYPE_TIMELINE;
    semaphoreParameters.initial_value = 0;
    m_handoffSemaphore = pl_vulkan_sem_create(m_gpu, &semaphoreParameters);
    if (!m_handoffSemaphore) {
        setUnavailableDiagnostics(QStringLiteral("Could not create the Vulkan timeline handoff semaphore"));
    } else {
        setUnavailableDiagnostics(QStringLiteral("Video target not provisioned"));
    }
}

VulkanLibplaceboVideoTarget::~VulkanLibplaceboVideoTarget() {
    Q_ASSERT(!m_libplaceboAccessActive);
    // The handoff semaphore is submitted through libplacebo and may still be
    // in flight even when the QRhi producer is replaced between frames.
    pl_gpu_finish(m_gpu);
    resetTarget();
    pl_vulkan_sem_destroy(m_gpu, &m_handoffSemaphore);
}

VideoTargetUpdate VulkanLibplaceboVideoTarget::ensureTarget(RenderedVideoSurfaceDescription const& description) {
    Q_ASSERT(description.isValid());
    Q_ASSERT(!m_libplaceboAccessActive);
    Q_ASSERT(!m_submissionPending);
    Q_ASSERT(!m_compositionPrepared);
    if (!m_handoffSemaphore) {
        return VideoTargetUpdate::Unavailable;
    }
    if (!m_texture) {
        return createTarget(description.pixelSize);
    }
    if (m_texture->pixelSize() != description.pixelSize) {
        return resizeTarget(description.pixelSize);
    }
    return VideoTargetUpdate::Unchanged;
}

VideoOperationResult VulkanLibplaceboVideoTarget::beginProducerAccess(QRhiCommandBuffer&) {
    if (deviceLost()) {
        return VideoOperationResult::DeviceLost;
    }
    Q_ASSERT(!m_libplaceboAccessActive);
    Q_ASSERT(!m_submissionPending);
    Q_ASSERT(!m_compositionPrepared);
    if (!m_wrappedTexture) {
        return VideoOperationResult::Unavailable;
    }

    QRhiTexture::NativeTexture const native = m_texture->nativeTexture();
    if (!native.object) {
        return VideoOperationResult::Unavailable;
    }
    VideoOperationResult const signalResult = signalQrhiCompletion();
    if (signalResult != VideoOperationResult::Ready) {
        return signalResult;
    }
    pl_vulkan_release_params release{};
    release.tex = m_wrappedTexture;
    release.layout = static_cast<VkImageLayout>(native.layout);
    release.qf = m_graphicsQueueFamily;
    release.semaphore = {
        .sem = m_handoffSemaphore,
        .value = m_handoffValue,
    };
    pl_vulkan_release_ex(m_gpu, &release);
    m_compositionBarrierPending = false;
    m_libplaceboAccessActive = true;
    m_submissionPending = true;
    return VideoOperationResult::Ready;
}

VideoOperationResult VulkanLibplaceboVideoTarget::endProducerAccess(QRhiCommandBuffer&) {
    Q_ASSERT(m_libplaceboAccessActive);
    Q_ASSERT(m_submissionPending);
    Q_ASSERT(m_wrappedTexture);
    if (deviceLost()) {
        m_libplaceboAccessActive = false;
        return VideoOperationResult::DeviceLost;
    }

    ++m_handoffValue;
    Q_ASSERT(m_handoffValue != 0);
    pl_vulkan_hold_params hold{};
    hold.tex = m_wrappedTexture;
    hold.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hold.qf = m_graphicsQueueFamily;
    hold.semaphore = {
        .sem = m_handoffSemaphore,
        .value = m_handoffValue,
    };
    if (!pl_vulkan_hold_ex(m_gpu, &hold)) {
        m_libplaceboAccessActive = false;
        if (deviceLost()) {
            return VideoOperationResult::DeviceLost;
        }
        qCCritical(sunplayerLogGraphics, "Libplacebo failed the mandatory Vulkan image handoff");
        return VideoOperationResult::Unavailable;
    }
    m_compositionBarrierPending = true;
    m_texture->setNativeLayout(static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    m_libplaceboAccessActive = false;
    return VideoOperationResult::Ready;
}

VideoOperationResult VulkanLibplaceboVideoTarget::prepareForComposition(QRhiCommandBuffer& commandBuffer) {
    if (deviceLost()) {
        return VideoOperationResult::DeviceLost;
    }
    Q_ASSERT(!m_libplaceboAccessActive);
    Q_ASSERT(!m_compositionPrepared);
    if (!m_wrappedTexture) {
        return VideoOperationResult::Unavailable;
    }
    if (m_compositionBarrierPending) {
        recordCompositionBarrier(commandBuffer);
    }
    m_submissionPending = true;
    m_compositionPrepared = true;
    return VideoOperationResult::Ready;
}

void VulkanLibplaceboVideoTarget::submissionAccepted() {
    if (!m_submissionPending) {
        return;
    }
    Q_ASSERT(!m_libplaceboAccessActive);
    if (m_compositionPrepared) {
        m_compositionBarrierPending = false;
    }
    m_submissionPending = false;
    m_compositionPrepared = false;
}

void VulkanLibplaceboVideoTarget::submissionAborted() {
    Q_ASSERT(!m_libplaceboAccessActive);
    // endFrame() can submit successfully and only then report a present error.
    // Non-submission failures rebuild the graphics domain, so a prepared
    // external barrier is canonical in either result.
    if (m_compositionPrepared) {
        m_compositionBarrierPending = false;
    }
    m_submissionPending = false;
    m_compositionPrepared = false;
}

QRhiTextureRenderTarget* VulkanLibplaceboVideoTarget::qrhiRenderTarget() const { return nullptr; }

QRhiRenderPassDescriptor* VulkanLibplaceboVideoTarget::qrhiRenderPassDescriptor() const { return nullptr; }

pl_tex VulkanLibplaceboVideoTarget::libplaceboRenderTarget() const { return m_wrappedTexture; }

QRhiTexture& VulkanLibplaceboVideoTarget::textureForComposition() const {
    Q_ASSERT(m_texture);
    return *m_texture;
}

std::uint64_t VulkanLibplaceboVideoTarget::compositionTextureRevision() const { return m_compositionTextureRevision; }

VideoTargetInteropDiagnostics const& VulkanLibplaceboVideoTarget::diagnostics() const {
    Q_ASSERT(m_diagnostics.isValid());
    return m_diagnostics;
}

VideoTargetUpdate VulkanLibplaceboVideoTarget::createTarget(QSize const& pixelSize) {
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(!m_texture);

    QRhiTexture::Flags flags = QRhiTexture::RenderTarget;
    if (m_readback == VideoTargetReadback::Enabled) {
        flags |= QRhiTexture::UsedAsTransferSource;
    }
    if (!m_rhi.isTextureFormatSupported(QRhiTexture::RGBA16F, flags)) {
        setUnavailableDiagnostics(QStringLiteral("RGBA16F render target is unsupported"));
        return VideoTargetUpdate::Unavailable;
    }

    m_texture.reset(m_rhi.newTexture(QRhiTexture::RGBA16F, pixelSize, 1, flags));
    m_texture->setName(QByteArrayLiteral("SunPlayer libplacebo Vulkan video surface"));
    if (!m_texture->create() || !wrapTexture()) {
        bool const lost = deviceLost();
        resetTarget();
        return lost ? VideoTargetUpdate::DeviceLost : VideoTargetUpdate::Unavailable;
    }

    setDirectDiagnostics();
    advanceTextureRevision();
    return VideoTargetUpdate::Created;
}

VideoTargetUpdate VulkanLibplaceboVideoTarget::resizeTarget(QSize const& pixelSize) {
    Q_ASSERT(m_texture);
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(m_texture->pixelSize() != pixelSize);

    pl_tex_destroy(m_gpu, &m_wrappedTexture);
    m_texture->setPixelSize(pixelSize);
    if (!m_texture->create() || !wrapTexture()) {
        bool const lost = deviceLost();
        resetTarget();
        return lost ? VideoTargetUpdate::DeviceLost : VideoTargetUpdate::Unavailable;
    }

    setDirectDiagnostics();
    advanceTextureRevision();
    return VideoTargetUpdate::Resized;
}

bool VulkanLibplaceboVideoTarget::wrapTexture() {
    Q_ASSERT(m_texture);
    Q_ASSERT(!m_wrappedTexture);
    QRhiTexture::NativeTexture const native = m_texture->nativeTexture();
    if (!native.object) {
        setUnavailableDiagnostics(QStringLiteral("QRhi did not expose its Vulkan video image"));
        return false;
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (m_readback == VideoTargetReadback::Enabled) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    pl_vulkan_wrap_params parameters{};
    parameters.image = reinterpret_cast<VkImage>(native.object);
    parameters.width = m_texture->pixelSize().width();
    parameters.height = m_texture->pixelSize().height();
    parameters.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    parameters.usage = usage;
    m_wrappedTexture = pl_vulkan_wrap(m_gpu, &parameters);
    if (!m_wrappedTexture || !m_wrappedTexture->params.renderable) {
        pl_tex_destroy(m_gpu, &m_wrappedTexture);
        setUnavailableDiagnostics(QStringLiteral("Libplacebo could not wrap the QRhi Vulkan RGBA16F image"));
        return false;
    }
    return true;
}

VideoOperationResult VulkanLibplaceboVideoTarget::signalQrhiCompletion() {
    ++m_handoffValue;
    Q_ASSERT(m_handoffValue != 0);
    VkTimelineSemaphoreSubmitInfo const timeline{
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = 1,
        .pSignalSemaphoreValues = &m_handoffValue,
    };
    VkSubmitInfo const submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = &timeline,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &m_handoffSemaphore,
    };
    VkResult const result = m_deviceFunctions.vkQueueSubmit(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    if (result == VK_SUCCESS) {
        return VideoOperationResult::Ready;
    }
    if (result == VK_ERROR_DEVICE_LOST) {
        return VideoOperationResult::DeviceLost;
    }
    qCCritical(sunplayerLogGraphics, "Could not order QRhi completion before libplacebo: Vulkan error %d",
               static_cast<int>(result));
    return VideoOperationResult::Unavailable;
}

void VulkanLibplaceboVideoTarget::recordCompositionBarrier(QRhiCommandBuffer& commandBuffer) {
    Q_ASSERT(m_texture);
    QRhiTexture::NativeTexture const native = m_texture->nativeTexture();
    Q_ASSERT(native.object);

    commandBuffer.beginExternal();
    auto const* const nativeCommandBuffer =
        static_cast<QRhiVulkanCommandBufferNativeHandles const*>(commandBuffer.nativeHandles());
    if (!nativeCommandBuffer || nativeCommandBuffer->commandBuffer == VK_NULL_HANDLE) {
        qCFatal(sunplayerLogGraphics, "QRhi did not expose its Vulkan command buffer");
    }

    VkImageMemoryBarrier2 const imageBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = reinterpret_cast<VkImage>(native.object),
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    VkDependencyInfo const dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &imageBarrier,
    };
    m_deviceFunctions.vkCmdPipelineBarrier2(nativeCommandBuffer->commandBuffer, &dependency);
    commandBuffer.endExternal();
    m_texture->setNativeLayout(static_cast<int>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

bool VulkanLibplaceboVideoTarget::deviceLost() const { return m_rhi.isDeviceLost() || pl_gpu_is_failed(m_gpu); }

void VulkanLibplaceboVideoTarget::resetTarget() {
    pl_tex_destroy(m_gpu, &m_wrappedTexture);
    m_texture.reset();
    m_libplaceboAccessActive = false;
    m_submissionPending = false;
    m_compositionPrepared = false;
    m_compositionBarrierPending = false;
}

void VulkanLibplaceboVideoTarget::setDirectDiagnostics() {
    m_diagnostics = {};
    m_diagnostics.outputPath = VideoOutputPath::DirectRenderTarget;
    m_diagnostics.synchronizationMode = QStringLiteral("Shared Vulkan graphics queue · timeline dependency · "
                                                       "QRhi command-buffer barrier");
    Q_ASSERT(m_diagnostics.isValid());
}

void VulkanLibplaceboVideoTarget::setUnavailableDiagnostics(QString const& reason) {
    m_diagnostics = {};
    m_diagnostics.outputPath = VideoOutputPath::Unavailable;
    m_diagnostics.synchronizationMode = QStringLiteral("Not active");
    m_diagnostics.fallbackReason = reason;
    Q_ASSERT(m_diagnostics.isValid());
}

void VulkanLibplaceboVideoTarget::advanceTextureRevision() {
    ++m_compositionTextureRevision;
    if (m_compositionTextureRevision == 0) {
        ++m_compositionTextureRevision;
    }
}
