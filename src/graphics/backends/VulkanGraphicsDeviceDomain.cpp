#include "graphics/backends/VulkanGraphicsDeviceDomain.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <QVulkanInstance>
#include <QVulkanFunctions>
#include <QWindow>
#include <libplacebo/vulkan.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <vulkan/vulkan.h>

#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "graphics/backends/VulkanLibplaceboVideoTarget.h"
#include "media/FfmpegHardwareDevice.h"
#include "video/QrhiVideoTarget.h"
#include "video/libplacebo/LibplaceboFrameImporter.h"

namespace {
struct VulkanExecutionState {
    std::recursive_mutex mutex;
};

void lockQueue(void *opaque, std::uint32_t, std::uint32_t) {
    static_cast<VulkanExecutionState *>(opaque)->mutex.lock();
}

void unlockQueue(void *opaque, std::uint32_t, std::uint32_t) {
    static_cast<VulkanExecutionState *>(opaque)->mutex.unlock();
}

void unlockGraphicsExecution(void *opaque) {
    static_cast<VulkanExecutionState *>(opaque)->mutex.unlock();
}

void logLibplacebo(
        void *,
        enum pl_log_level level,
        const char *message) {
    switch (level) {
    case PL_LOG_FATAL:
    case PL_LOG_ERR:
        qCCritical(sunroomLogGraphics).noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_WARN:
        qCWarning(sunroomLogGraphics).noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_INFO:
        qCInfo(sunroomLogGraphics).noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_DEBUG:
    case PL_LOG_TRACE:
        qCDebug(sunroomLogGraphics).noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_NONE:
        break;
    }
}

class VulkanGraphicsDeviceDomain final : public GraphicsDeviceDomain {
public:
    explicit VulkanGraphicsDeviceDomain(QWindow &window) {
        QVulkanInstance *const instance = window.vulkanInstance();
        if (!instance || !instance->isValid()) {
            qCCritical(
                sunroomLogGraphics,
                "The presentation window has no valid Vulkan instance");
            return;
        }

        QRhiVulkanInitParams parameters;
        parameters.inst = instance;
        parameters.window = &window;
        // Vulkan 1.3 makes synchronization2 available to libplacebo's
        // external-image handoff without a custom device-feature chain.
        // Request external-memory/modifier extensions with the future
        // VAAPI/DRM-PRIME importer that actually consumes them.
        m_rhi.reset(QRhi::create(QRhi::Vulkan, &parameters));
        if (!m_rhi)
            return;

        const auto *const native =
            static_cast<const QRhiVulkanNativeHandles *>(
                m_rhi->nativeHandles());
        if (!native || !native->inst
                || native->inst != instance
                || native->physDev == VK_NULL_HANDLE
                || native->dev == VK_NULL_HANDLE
                || native->gfxQueue == VK_NULL_HANDLE) {
            qCCritical(
                sunroomLogGraphics,
                "QRhi did not expose one complete Vulkan device domain");
            return;
        }
        if (native->gfxQueueIdx != 0) {
            qCCritical(
                sunroomLogGraphics,
                "QRhi selected unsupported Vulkan queue index %u",
                native->gfxQueueIdx);
            return;
        }

        QVulkanFunctions *const vulkanFunctions = instance->functions();
        QVulkanDeviceFunctions *const deviceFunctions =
            instance->deviceFunctions(native->dev);
        if (!vulkanFunctions || !deviceFunctions) {
            qCCritical(
                sunroomLogGraphics,
                "Qt did not expose Vulkan instance/device functions");
            return;
        }
        m_deviceFunctions = deviceFunctions;
        VkPhysicalDeviceProperties physicalDeviceProperties{};
        vulkanFunctions->vkGetPhysicalDeviceProperties(
            native->physDev, &physicalDeviceProperties);
        if (physicalDeviceProperties.apiVersion < VK_API_VERSION_1_3) {
            qCCritical(
                sunroomLogGraphics,
                "QRhi selected a physical device below Vulkan 1.3");
            return;
        }

        std::uint32_t queueFamilyCount = 0;
        vulkanFunctions->vkGetPhysicalDeviceQueueFamilyProperties(
            native->physDev, &queueFamilyCount, nullptr);
        if (native->gfxQueueFamilyIdx >= queueFamilyCount) {
            qCCritical(
                sunroomLogGraphics,
                "QRhi selected invalid Vulkan queue family %u",
                native->gfxQueueFamilyIdx);
            return;
        }
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vulkanFunctions->vkGetPhysicalDeviceQueueFamilyProperties(
            native->physDev, &queueFamilyCount, queueFamilies.data());
        const VkQueueFlags requiredQueueFlags =
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((queueFamilies[native->gfxQueueFamilyIdx].queueFlags
                & requiredQueueFlags) != requiredQueueFlags) {
            qCCritical(
                sunroomLogGraphics,
                "QRhi selected a Vulkan graphics queue without compute support");
            return;
        }

        VkPhysicalDeviceVulkan13Features enabledFeatures13{
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        };
        VkPhysicalDeviceVulkan12Features enabledFeatures12{
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &enabledFeatures13,
        };
        VkPhysicalDeviceVulkan11Features enabledFeatures11{
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &enabledFeatures12,
        };
        VkPhysicalDeviceFeatures2 enabledFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &enabledFeatures11,
        };
        vulkanFunctions->vkGetPhysicalDeviceFeatures2(
            native->physDev, &enabledFeatures);
        if (!enabledFeatures12.hostQueryReset
                || !enabledFeatures12.timelineSemaphore
                || !enabledFeatures13.synchronization2) {
            qCCritical(
                sunroomLogGraphics,
                "The selected Vulkan device lacks QRhi/libplacebo features");
            return;
        }
        // Qt 6.10 enables every reported core 1.0-1.3 feature except these
        // two robustness options when it creates QRhi's device. Mirror that
        // exact enabled subset instead of claiming unsupported device state.
        enabledFeatures.features.robustBufferAccess = VK_FALSE;
        enabledFeatures13.robustImageAccess = VK_FALSE;
        m_graphicsQueueFamily = native->gfxQueueFamilyIdx;
        m_graphicsQueue = native->gfxQueue;
        pl_log_params logParameters{};
        logParameters.log_cb = logLibplacebo;
        logParameters.log_level = PL_LOG_WARN;
        m_log = pl_log_create(PL_API_VER, &logParameters);
        if (!m_log)
            return;

        pl_vulkan_import_params import{};
        import.instance = instance->vkInstance();
        import.get_proc_addr = nullptr;
        import.phys_device = native->physDev;
        import.device = native->dev;
        import.queue_graphics = {
            .index = native->gfxQueueFamilyIdx,
            .count = 1,
        };
        import.features = &enabledFeatures;
        import.lock_queue = lockQueue;
        import.unlock_queue = unlockQueue;
        import.queue_ctx = m_executionState.get();
        import.max_api_version = VK_API_VERSION_1_3;
        m_vulkan = pl_vulkan_import(m_log, &import);
        if (!m_vulkan)
            return;

        m_libplacebo = {
            .log = m_log,
            .gpu = m_vulkan->gpu,
        };
        m_videoDecode = {
            .device = {},
            .unavailableReason = QStringLiteral(
                "VAAPI/DRM PRIME import is not implemented yet"),
        };
        m_diagnostics.backend = GraphicsBackend::Vulkan;
        m_diagnostics.backendName =
            QString::fromLatin1(m_rhi->backendName());
        m_diagnostics.nativeApi = QStringLiteral("Vulkan 1.3");
        m_diagnostics.adapterName =
            QString::fromUtf8(m_rhi->driverInfo().deviceName);
        if (m_diagnostics.adapterName.isEmpty())
            m_diagnostics.adapterName = QStringLiteral("Unknown adapter");
        Q_ASSERT(m_diagnostics.isValid());
    }

    ~VulkanGraphicsDeviceDomain() override {
        pl_vulkan_destroy(&m_vulkan);
        pl_log_destroy(&m_log);
    }

    QRhi &rhi() const override {
        Q_ASSERT(m_rhi);
        return *m_rhi;
    }

    const GraphicsDeviceDiagnostics &diagnostics() const override {
        Q_ASSERT(m_diagnostics.isValid());
        return m_diagnostics;
    }

    const LibplaceboGraphicsContext &
    libplaceboContext() const override {
        Q_ASSERT(m_libplacebo.isValid());
        return m_libplacebo;
    }

    const VideoHardwareDecodeCapability &
    videoDecodeCapability() const override {
        return m_videoDecode;
    }

    GraphicsDeviceExecutionScope acquireExecutionScope() override {
        m_executionState->mutex.lock();
        return GraphicsDeviceExecutionScope(
            m_executionState, unlockGraphicsExecution);
    }

    std::unique_ptr<VideoTargetInterop> createVideoTarget(
            const VideoTargetRequest &request) override {
        switch (request.producerApi) {
        case VideoProducerApi::Qrhi:
            return std::make_unique<QrhiVideoTarget>(
                *m_rhi, request.readback);
        case VideoProducerApi::Libplacebo:
            return std::make_unique<VulkanLibplaceboVideoTarget>(
                *m_rhi,
                m_libplacebo.gpu,
                *m_deviceFunctions,
                m_graphicsQueue,
                m_graphicsQueueFamily,
                request.readback);
        }
        return {};
    }

    std::unique_ptr<LibplaceboHardwareFrameImporter>
    createHardwareFrameImporter() override {
        return {};
    }

    bool isValid() const {
        return m_rhi
            && m_vulkan
            && m_diagnostics.isValid()
            && m_libplacebo.isValid();
    }

private:
    std::shared_ptr<VulkanExecutionState> m_executionState =
        std::make_shared<VulkanExecutionState>();
    std::unique_ptr<QRhi> m_rhi;
    pl_log m_log = nullptr;
    pl_vulkan m_vulkan = nullptr;
    QVulkanDeviceFunctions *m_deviceFunctions = nullptr;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    LibplaceboGraphicsContext m_libplacebo;
    VideoHardwareDecodeCapability m_videoDecode;
    GraphicsDeviceDiagnostics m_diagnostics;
    std::uint32_t m_graphicsQueueFamily = 0;
};
}

std::unique_ptr<GraphicsDeviceDomain>
createVulkanGraphicsDeviceDomain(QWindow &window) {
    auto domain = std::make_unique<VulkanGraphicsDeviceDomain>(window);
    if (!domain->isValid())
        return {};
    return domain;
}
