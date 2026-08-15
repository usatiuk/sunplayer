#define VK_USE_PLATFORM_METAL_EXT 1

#include "graphics/backends/MetalLibplaceboVideoTarget.h"

#include <cstdint>
#include <memory>

#import <Metal/Metal.h>

#include <QtCore/qlogging.h>
#include <libplacebo/vulkan.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <vulkan/vulkan.h>

#include "diagnostics/LogCategories.h"
#include "video/VideoTargetInterop.h"

namespace {
void advanceRevision(std::uint64_t &revision) {
    ++revision;
    if (revision == 0)
        ++revision;
}

class MetalLibplaceboVideoTarget final : public VideoTargetInterop {
public:
    MetalLibplaceboVideoTarget(
            QRhi &rhi,
            pl_vulkan vulkan,
            VideoTargetReadback readback)
        : m_rhi(rhi),
          m_vulkan(vulkan),
          m_gpu(vulkan ? vulkan->gpu : nullptr),
          m_readback(readback) {
        Q_ASSERT(m_vulkan);
        Q_ASSERT(m_gpu);
        if (!(m_gpu->import_caps.tex & PL_HANDLE_MTL_TEX)) {
            setUnavailableDiagnostics(QStringLiteral(
                "Libplacebo cannot import Metal textures"));
            return;
        }
        if (!createSharedEvent()) {
            setUnavailableDiagnostics(QStringLiteral(
                "Could not create the Metal/Vulkan shared event"));
            return;
        }
        setUnavailableDiagnostics(QStringLiteral(
            "Video target not provisioned"));
    }

    ~MetalLibplaceboVideoTarget() override {
        Q_ASSERT(!m_producerAccessActive);
        pl_gpu_finish(m_gpu);
        resetTarget();
        m_sharedEvent = nil;
        if (m_handoffSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(
                m_vulkan->device,
                m_handoffSemaphore,
                nullptr);
        }
    }

    VideoTargetUpdate ensureTarget(
            const RenderedVideoSurfaceDescription &description) override {
        Q_ASSERT(description.isValid());
        Q_ASSERT(!m_producerAccessActive);
        Q_ASSERT(!m_submissionPending);
        Q_ASSERT(!m_compositionPrepared);
        if (!m_sharedEvent)
            return VideoTargetUpdate::Unavailable;
        if (deviceLost())
            return VideoTargetUpdate::DeviceLost;
        if (!m_texture)
            return createTarget(description.pixelSize);
        if (m_texture->pixelSize() != description.pixelSize)
            return resizeTarget(description.pixelSize);
        return VideoTargetUpdate::Unchanged;
    }

    VideoOperationResult beginProducerAccess(
            QRhiCommandBuffer &commandBuffer) override {
        if (deviceLost())
            return VideoOperationResult::DeviceLost;
        Q_ASSERT(!m_producerAccessActive);
        Q_ASSERT(!m_submissionPending);
        Q_ASSERT(!m_compositionPrepared);
        if (!m_wrappedTexture || m_producerAccessActive
                || m_submissionPending || m_compositionPrepared) {
            return VideoOperationResult::Unavailable;
        }

        const VideoOperationResult ownershipResult =
            prepareMetalAccess(commandBuffer);
        if (ownershipResult != VideoOperationResult::Ready)
            return ownershipResult;
        const std::uint64_t metalSignal = nextHandoffValue();
        encodeMetalSignal(commandBuffer, metalSignal);

        pl_vulkan_release_params release{};
        release.tex = m_wrappedTexture;
        release.layout = m_vulkanLayout;
        release.qf = m_vulkan->queue_graphics.index;
        release.semaphore = {
            .sem = m_handoffSemaphore,
            .value = metalSignal,
        };
        pl_vulkan_release_ex(m_gpu, &release);
        m_producerAccessActive = true;
        m_submissionPending = true;
        return VideoOperationResult::Ready;
    }

    VideoOperationResult endProducerAccess(
            QRhiCommandBuffer &) override {
        Q_ASSERT(m_producerAccessActive);
        Q_ASSERT(m_submissionPending);
        if (!m_wrappedTexture || !m_producerAccessActive
                || !m_submissionPending) {
            return VideoOperationResult::Unavailable;
        }
        if (deviceLost()) {
            m_producerAccessActive = false;
            return VideoOperationResult::DeviceLost;
        }

        if (!holdForMetal()) {
            m_producerAccessActive = false;
            if (deviceLost())
                return VideoOperationResult::DeviceLost;
            qCFatal(
                sunplayerLogGraphics,
                "Libplacebo failed the Metal/Vulkan texture handoff");
        }
        m_producerAccessActive = false;
        return VideoOperationResult::Ready;
    }

    VideoOperationResult prepareForComposition(
            QRhiCommandBuffer &commandBuffer) override {
        if (deviceLost())
            return VideoOperationResult::DeviceLost;
        Q_ASSERT(!m_producerAccessActive);
        Q_ASSERT(!m_compositionPrepared);
        if (!m_wrappedTexture || m_producerAccessActive
                || m_compositionPrepared) {
            return VideoOperationResult::Unavailable;
        }
        const VideoOperationResult ownershipResult =
            prepareMetalAccess(commandBuffer);
        if (ownershipResult != VideoOperationResult::Ready)
            return ownershipResult;
        m_submissionPending = true;
        m_compositionPrepared = true;
        return VideoOperationResult::Ready;
    }

    void submissionAccepted() override {
        if (!m_submissionPending)
            return;
        Q_ASSERT(!m_producerAccessActive);
        m_submissionPending = false;
        m_compositionPrepared = false;
    }

    void submissionAborted() override {
        Q_ASSERT(!m_producerAccessActive);
        m_submissionPending = false;
        m_compositionPrepared = false;
    }

    QRhiTextureRenderTarget *qrhiRenderTarget() const override {
        return nullptr;
    }

    QRhiRenderPassDescriptor *
    qrhiRenderPassDescriptor() const override {
        return nullptr;
    }

    pl_tex libplaceboRenderTarget() const override {
        return m_wrappedTexture;
    }

    QRhiTexture &textureForComposition() const override {
        Q_ASSERT(m_texture);
        return *m_texture;
    }

    std::uint64_t compositionTextureRevision() const override {
        return m_compositionTextureRevision;
    }

    const VideoTargetInteropDiagnostics &diagnostics() const override {
        Q_ASSERT(m_diagnostics.isValid());
        return m_diagnostics;
    }

private:
    bool createSharedEvent() {
        const VkExportMetalObjectCreateInfoEXT metalExport{
            .sType =
                VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT,
            .exportObjectType =
                VK_EXPORT_METAL_OBJECT_TYPE_METAL_SHARED_EVENT_BIT_EXT,
        };
        const VkSemaphoreTypeCreateInfo timeline{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = &metalExport,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        };
        const VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &timeline,
        };
        if (vkCreateSemaphore(
                m_vulkan->device,
                &semaphoreInfo,
                nullptr,
                &m_handoffSemaphore) != VK_SUCCESS) {
            return false;
        }

        const auto exportMetalObjects =
            reinterpret_cast<PFN_vkExportMetalObjectsEXT>(
                vkGetDeviceProcAddr(
                    m_vulkan->device,
                    "vkExportMetalObjectsEXT"));
        if (!exportMetalObjects)
            return false;

        VkExportMetalSharedEventInfoEXT eventInfo{
            .sType =
                VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT,
            .semaphore = m_handoffSemaphore,
        };
        VkExportMetalObjectsInfoEXT objectsInfo{
            .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
            .pNext = &eventInfo,
        };
        exportMetalObjects(m_vulkan->device, &objectsInfo);
        m_sharedEvent = eventInfo.mtlSharedEvent;
        return m_sharedEvent != nil;
    }

    VideoTargetUpdate createTarget(const QSize &pixelSize) {
        Q_ASSERT(!pixelSize.isEmpty());
        Q_ASSERT(!m_texture);

        QRhiTexture::Flags flags = QRhiTexture::RenderTarget;
        if (m_readback == VideoTargetReadback::Enabled)
            flags |= QRhiTexture::UsedAsTransferSource;
        if (!m_rhi.isTextureFormatSupported(
                QRhiTexture::RGBA16F, flags)) {
            setUnavailableDiagnostics(QStringLiteral(
                "RGBA16F render target is unsupported"));
            return VideoTargetUpdate::Unavailable;
        }

        m_texture.reset(m_rhi.newTexture(
            QRhiTexture::RGBA16F, pixelSize, 1, flags));
        m_texture->setName(QByteArrayLiteral(
            "SunPlayer shared Metal video surface"));
        if (!m_texture->create() || !importTexture()) {
            const bool lost = deviceLost();
            resetTarget();
            return lost
                ? VideoTargetUpdate::DeviceLost
                : VideoTargetUpdate::Unavailable;
        }

        setDirectDiagnostics();
        advanceRevision(m_compositionTextureRevision);
        return VideoTargetUpdate::Created;
    }

    VideoTargetUpdate resizeTarget(const QSize &pixelSize) {
        Q_ASSERT(m_texture);
        Q_ASSERT(!pixelSize.isEmpty());
        Q_ASSERT(m_texture->pixelSize() != pixelSize);
        Q_ASSERT(m_metalWaitPending == 0);

        pl_tex_destroy(m_gpu, &m_wrappedTexture);
        m_initialHandoffPending = false;
        m_texture->setPixelSize(pixelSize);
        if (!m_texture->create() || !importTexture()) {
            const bool lost = deviceLost();
            resetTarget();
            return lost
                ? VideoTargetUpdate::DeviceLost
                : VideoTargetUpdate::Unavailable;
        }

        setDirectDiagnostics();
        advanceRevision(m_compositionTextureRevision);
        return VideoTargetUpdate::Resized;
    }

    bool importTexture() {
        Q_ASSERT(m_texture);
        Q_ASSERT(!m_wrappedTexture);
        const QRhiTexture::NativeTexture native =
            m_texture->nativeTexture();
        id<MTLTexture> metalTexture = (__bridge id<MTLTexture>)(
            reinterpret_cast<void *>(
                static_cast<std::uintptr_t>(native.object)));
        if (!metalTexture
                || metalTexture.pixelFormat
                    != MTLPixelFormatRGBA16Float
                || !(metalTexture.usage
                    & MTLTextureUsageRenderTarget)
                || !(metalTexture.usage
                    & MTLTextureUsageShaderRead)) {
            setUnavailableDiagnostics(QStringLiteral(
                "QRhi exposed an incompatible Metal video texture"));
            return false;
        }

        const pl_fmt format = pl_find_named_fmt(m_gpu, "rgba16f");
        if (!format || !(format->caps & PL_FMT_CAP_RENDERABLE)) {
            setUnavailableDiagnostics(QStringLiteral(
                "Libplacebo has no renderable RGBA16F format"));
            return false;
        }
        pl_tex_params parameters{};
        parameters.w = m_texture->pixelSize().width();
        parameters.h = m_texture->pixelSize().height();
        parameters.format = format;
        parameters.renderable = true;
        parameters.import_handle = PL_HANDLE_MTL_TEX;
        parameters.shared_mem.handle.handle =
            (__bridge void *)metalTexture;
        parameters.debug_tag = "SunPlayer shared Metal video surface";
        m_wrappedTexture = pl_tex_create(m_gpu, &parameters);
        if (!m_wrappedTexture
                || !m_wrappedTexture->params.renderable) {
            pl_tex_destroy(m_gpu, &m_wrappedTexture);
            setUnavailableDiagnostics(QStringLiteral(
                "Libplacebo could not import the QRhi Metal texture"));
            return false;
        }

        // Delay the first Vulkan-to-Metal hold until a QRhi command buffer can
        // consume its wait. A provisioned target may be resized before the
        // first frame starts (for example after repeated swapchain-out-of-date
        // results), and in that case no queued external handoff may outlive
        // the discarded texture.
        m_initialHandoffPending = true;
        return true;
    }

    bool holdForMetal() {
        Q_ASSERT(m_wrappedTexture);
        Q_ASSERT(m_metalWaitPending == 0);
        const std::uint64_t vulkanSignal = nextHandoffValue();
        pl_vulkan_hold_params hold{};
        hold.tex = m_wrappedTexture;
        hold.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hold.qf = m_vulkan->queue_graphics.index;
        hold.semaphore = {
            .sem = m_handoffSemaphore,
            .value = vulkanSignal,
        };
        if (!pl_vulkan_hold_ex(m_gpu, &hold))
            return false;
        m_vulkanLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        m_metalWaitPending = vulkanSignal;
        return true;
    }

    VideoOperationResult prepareMetalAccess(
            QRhiCommandBuffer &commandBuffer) {
        if (m_initialHandoffPending) {
            if (!holdForMetal()) {
                if (deviceLost())
                    return VideoOperationResult::DeviceLost;
                qCFatal(
                    sunplayerLogGraphics,
                    "Libplacebo failed the initial Metal/Vulkan texture handoff");
            }
            m_initialHandoffPending = false;
        }
        if (m_metalWaitPending != 0) {
            encodeMetalWait(commandBuffer, m_metalWaitPending);
            m_metalWaitPending = 0;
        }
        return VideoOperationResult::Ready;
    }

    void encodeMetalSignal(
            QRhiCommandBuffer &commandBuffer,
            std::uint64_t value) {
        commandBuffer.beginExternal();
        const auto *const native =
            static_cast<const QRhiMetalCommandBufferNativeHandles *>(
                commandBuffer.nativeHandles());
        if (!native || !native->commandBuffer) {
            qCFatal(
                sunplayerLogGraphics,
                "QRhi did not expose its Metal command buffer");
        }
        id<MTLCommandBuffer> metalCommandBuffer =
            (id<MTLCommandBuffer>)native->commandBuffer;
        [metalCommandBuffer encodeSignalEvent:m_sharedEvent value:value];
        commandBuffer.endExternal();
    }

    void encodeMetalWait(
            QRhiCommandBuffer &commandBuffer,
            std::uint64_t value) {
        commandBuffer.beginExternal();
        const auto *const native =
            static_cast<const QRhiMetalCommandBufferNativeHandles *>(
                commandBuffer.nativeHandles());
        if (!native || !native->commandBuffer) {
            qCFatal(
                sunplayerLogGraphics,
                "QRhi did not expose its Metal command buffer");
        }
        id<MTLCommandBuffer> metalCommandBuffer =
            (id<MTLCommandBuffer>)native->commandBuffer;
        [metalCommandBuffer encodeWaitForEvent:m_sharedEvent value:value];
        commandBuffer.endExternal();
    }

    std::uint64_t nextHandoffValue() {
        ++m_handoffValue;
        if (m_handoffValue == 0) {
            qCFatal(
                sunplayerLogGraphics,
                "The Metal/Vulkan handoff counter overflowed");
        }
        return m_handoffValue;
    }

    bool deviceLost() const {
        return m_rhi.isDeviceLost() || pl_gpu_is_failed(m_gpu);
    }

    void resetTarget() {
        pl_tex_destroy(m_gpu, &m_wrappedTexture);
        m_texture.reset();
        m_vulkanLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        m_metalWaitPending = 0;
        m_initialHandoffPending = false;
        m_producerAccessActive = false;
        m_submissionPending = false;
        m_compositionPrepared = false;
    }

    void setDirectDiagnostics() {
        Q_ASSERT(m_wrappedTexture);
        m_diagnostics = {};
        m_diagnostics.outputPath =
            VideoOutputPath::DirectRenderTarget;
        m_diagnostics.synchronizationMode = QStringLiteral(
            "Shared MTLTexture · Metal/Vulkan shared-event timeline · %1")
            .arg(QString::fromLatin1(
                m_wrappedTexture->params.format->name));
        Q_ASSERT(m_diagnostics.isValid());
    }

    void setUnavailableDiagnostics(const QString &reason) {
        m_diagnostics = {};
        m_diagnostics.outputPath = VideoOutputPath::Unavailable;
        m_diagnostics.synchronizationMode =
            QStringLiteral("Not active");
        m_diagnostics.fallbackReason = reason;
        Q_ASSERT(m_diagnostics.isValid());
    }

    QRhi &m_rhi;
    pl_vulkan m_vulkan = nullptr;
    pl_gpu m_gpu = nullptr;
    VideoTargetReadback m_readback;
    VideoTargetInteropDiagnostics m_diagnostics;
    std::unique_ptr<QRhiTexture> m_texture;
    pl_tex m_wrappedTexture = nullptr;
    VkSemaphore m_handoffSemaphore = VK_NULL_HANDLE;
    id<MTLSharedEvent> m_sharedEvent = nil;
    VkImageLayout m_vulkanLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint64_t m_handoffValue = 0;
    std::uint64_t m_metalWaitPending = 0;
    std::uint64_t m_compositionTextureRevision = 0;
    bool m_initialHandoffPending = false;
    bool m_producerAccessActive = false;
    bool m_submissionPending = false;
    bool m_compositionPrepared = false;
};
}

std::unique_ptr<VideoTargetInterop>
createMetalLibplaceboVideoTarget(
        QRhi &rhi,
        pl_vulkan vulkan,
        VideoTargetReadback readback) {
    return std::make_unique<MetalLibplaceboVideoTarget>(
        rhi, vulkan, readback);
}
