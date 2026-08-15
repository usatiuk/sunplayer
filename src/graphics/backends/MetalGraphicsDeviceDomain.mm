#define VK_USE_PLATFORM_METAL_EXT 1

#include "graphics/backends/MetalGraphicsDeviceDomain.h"

#include <memory>
#include <mutex>
#include <utility>

#import <Metal/Metal.h>

#include <QtCore/qlogging.h>
#include <libplacebo/vulkan.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <vulkan/vulkan.h>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
}

#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "graphics/backends/MetalLibplaceboFrameImporter.h"
#include "graphics/backends/MetalLibplaceboVideoTarget.h"
#include "media/FfmpegHardwareDevice.h"
#include "video/QrhiVideoTarget.h"
#include "video/VideoTargetInterop.h"
#include "video/libplacebo/LibplaceboFrameImporter.h"

namespace {
struct MetalExecutionState {
    std::recursive_mutex mutex;
};

void unlockGraphicsExecution(void *opaque) {
    static_cast<MetalExecutionState *>(opaque)->mutex.unlock();
}

QString ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0)
        return QStringLiteral("FFmpeg error %1").arg(code);
    return QString::fromUtf8(buffer);
}

VideoHardwareDecodeCapability createVideoDecodeCapability(
        std::uint64_t graphicsDeviceGeneration) {
    AVBufferRef *deviceReference = nullptr;
    const int status = av_hwdevice_ctx_create(
        &deviceReference,
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
        nullptr,
        nullptr,
        0);
    if (status < 0) {
        return {
            .device = {},
            .unavailableReason = QStringLiteral(
                "FFmpeg could not initialize VideoToolbox: %1")
                .arg(ffmpegError(status)),
        };
    }

    std::shared_ptr<const FfmpegHardwareDevice> hardwareDevice =
        FfmpegHardwareDevice::adopt(
            deviceReference,
            graphicsDeviceGeneration,
            QStringLiteral("VideoToolbox"));
    if (!hardwareDevice) {
        return {
            .device = {},
            .unavailableReason = QStringLiteral(
                "The VideoToolbox device description was invalid"),
        };
    }
    return {
        .device = std::move(hardwareDevice),
        .unavailableReason = {},
    };
}

void logLibplacebo(
        void *,
        enum pl_log_level level,
        const char *message) {
    switch (level) {
    case PL_LOG_FATAL:
    case PL_LOG_ERR:
        qCCritical(sunplayerLogGraphics).noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_WARN:
        qCWarning(sunplayerLogGraphics).noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_INFO:
        qCInfo(sunplayerLogGraphics).noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_DEBUG:
    case PL_LOG_TRACE:
        qCDebug(sunplayerLogGraphics).noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_NONE:
        break;
    }
}

class MetalGraphicsDeviceDomain final : public GraphicsDeviceDomain {
public:
    MetalGraphicsDeviceDomain() {
        QRhiMetalInitParams rhiParameters;
        m_rhi.reset(QRhi::create(QRhi::Metal, &rhiParameters));
        if (!m_rhi)
            return;

        const auto *const native =
            static_cast<const QRhiMetalNativeHandles *>(
                m_rhi->nativeHandles());
        if (!native || !native->dev || !native->cmdQueue) {
            qCCritical(
                sunplayerLogGraphics,
                "QRhi did not expose one complete Metal device domain");
            return;
        }
        m_metalDevice = (id<MTLDevice>)native->dev;

        pl_log_params logParameters{};
        logParameters.log_cb = logLibplacebo;
        logParameters.log_level = PL_LOG_WARN;
        m_log = pl_log_create(PL_API_VER, &logParameters);
        if (!m_log)
            return;

        const char *const requiredExtensions[] = {
            VK_EXT_METAL_OBJECTS_EXTENSION_NAME,
        };
        pl_vulkan_params vulkanParameters =
            pl_vulkan_default_params;
        vulkanParameters.get_proc_addr = vkGetInstanceProcAddr;
        vulkanParameters.extensions = requiredExtensions;
        vulkanParameters.num_extensions = 1;
        m_vulkan = pl_vulkan_create(
            m_log, &vulkanParameters);
        if (!m_vulkan)
            return;

        if (!usesSameMetalDevice()) {
            qCCritical(
                sunplayerLogGraphics,
                "QRhi Metal and MoltenVK selected different MTLDevices");
            return;
        }

        m_libplacebo = {
            .log = m_log,
            .gpu = m_vulkan->gpu,
        };
        m_videoDecode = createVideoDecodeCapability(generation());
        m_diagnostics.backend = GraphicsBackend::Metal;
        m_diagnostics.backendName =
            QString::fromLatin1(m_rhi->backendName());
        m_diagnostics.nativeApi = QStringLiteral(
            "Metal presentation · Vulkan over MoltenVK video");
        m_diagnostics.adapterName = QString::fromNSString(
            m_metalDevice.name);
        if (m_diagnostics.adapterName.isEmpty())
            m_diagnostics.adapterName =
                QStringLiteral("Unknown Apple GPU");
        Q_ASSERT(m_diagnostics.isValid());
    }

    ~MetalGraphicsDeviceDomain() override {
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
            return createMetalLibplaceboVideoTarget(
                *m_rhi, m_vulkan, request.readback);
        }
        return {};
    }

    std::unique_ptr<LibplaceboHardwareFrameImporter>
    createHardwareFrameImporter() override {
        return createMetalLibplaceboFrameImporter(
            m_vulkan, m_metalDevice);
    }

    bool isValid() const {
        return m_rhi
            && m_vulkan
            && m_metalDevice
            && m_diagnostics.isValid()
            && m_libplacebo.isValid();
    }

private:
    bool usesSameMetalDevice() const {
        const auto exportMetalObjects =
            reinterpret_cast<PFN_vkExportMetalObjectsEXT>(
                vkGetDeviceProcAddr(
                    m_vulkan->device,
                    "vkExportMetalObjectsEXT"));
        if (!exportMetalObjects)
            return false;

        VkExportMetalDeviceInfoEXT deviceInfo{
            .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT,
        };
        VkExportMetalObjectsInfoEXT objectsInfo{
            .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
            .pNext = &deviceInfo,
        };
        exportMetalObjects(m_vulkan->device, &objectsInfo);
        return deviceInfo.mtlDevice == m_metalDevice;
    }

    std::shared_ptr<MetalExecutionState> m_executionState =
        std::make_shared<MetalExecutionState>();
    std::unique_ptr<QRhi> m_rhi;
    pl_log m_log = nullptr;
    pl_vulkan m_vulkan = nullptr;
    id<MTLDevice> m_metalDevice = nil;
    LibplaceboGraphicsContext m_libplacebo;
    VideoHardwareDecodeCapability m_videoDecode;
    GraphicsDeviceDiagnostics m_diagnostics;
};
}

std::unique_ptr<GraphicsDeviceDomain>
createMetalGraphicsDeviceDomain() {
    auto domain = std::make_unique<MetalGraphicsDeviceDomain>();
    if (!domain->isValid())
        return {};
    return domain;
}
