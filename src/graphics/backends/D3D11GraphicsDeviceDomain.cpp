#include "graphics/backends/D3D11GraphicsDeviceDomain.h"

#include <array>
#include <memory>
#include <mutex>
#include <utility>

#include <QtCore/qlogging.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <libplacebo/d3d11.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#include "diagnostics/LogCategories.h"
#include "graphics/GraphicsDeviceDomain.h"
#include "graphics/backends/D3D11LibplaceboFrameImporter.h"
#include "graphics/backends/D3D11LibplaceboVideoTarget.h"
#include "media/FfmpegHardwareDevice.h"
#include "video/QrhiVideoTarget.h"
#include "video/VideoTargetInterop.h"
#include "video/libplacebo/LibplaceboFrameImporter.h"

namespace {
using Microsoft::WRL::ComPtr;

struct D3D11ExecutionState {
    std::recursive_mutex mutex;
};

void lockFfmpegDevice(void* opaque) { static_cast<D3D11ExecutionState*>(opaque)->mutex.lock(); }

void unlockFfmpegDevice(void* opaque) { static_cast<D3D11ExecutionState*>(opaque)->mutex.unlock(); }

void unlockGraphicsExecution(void* opaque) { unlockFfmpegDevice(opaque); }

void freeFfmpegDevice(AVHWDeviceContext* context) {
    delete static_cast<std::shared_ptr<D3D11ExecutionState>*>(context->user_opaque);
    context->user_opaque = nullptr;
}

QString ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
        return QStringLiteral("FFmpeg error %1").arg(code);
    }
    return QString::fromUtf8(buffer);
}

VideoHardwareDecodeCapability createVideoDecodeCapability(ID3D11Device& device, std::uint64_t graphicsDeviceGeneration,
                                                          std::shared_ptr<D3D11ExecutionState> const& executionState) {
    AVBufferRef* deviceReference = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!deviceReference) {
        return {
            .device = {},
            .unavailableReason = QStringLiteral("FFmpeg could not allocate a D3D11VA device context"),
        };
    }

    auto* const context = reinterpret_cast<AVHWDeviceContext*>(deviceReference->data);
    auto* const d3d11 = reinterpret_cast<AVD3D11VADeviceContext*>(context->hwctx);
    auto* const opaque = new std::shared_ptr<D3D11ExecutionState>(executionState);
    context->user_opaque = opaque;
    context->free = freeFfmpegDevice;
    d3d11->device = &device;
    d3d11->device->AddRef();
    d3d11->lock = lockFfmpegDevice;
    d3d11->unlock = unlockFfmpegDevice;
    d3d11->lock_ctx = executionState.get();
    d3d11->BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    int const status = av_hwdevice_ctx_init(deviceReference);
    if (status < 0) {
        QString const reason = QStringLiteral("FFmpeg could not initialize the shared "
                                              "D3D11VA device: %1")
                                   .arg(ffmpegError(status));
        av_buffer_unref(&deviceReference);
        return {
            .device = {},
            .unavailableReason = reason,
        };
    }

    std::shared_ptr<FfmpegHardwareDevice const> hardwareDevice =
        FfmpegHardwareDevice::adopt(deviceReference, graphicsDeviceGeneration, QStringLiteral("D3D11VA"));
    if (!hardwareDevice) {
        return {
            .device = {},
            .unavailableReason = QStringLiteral("The shared D3D11VA device description was invalid"),
        };
    }
    return {
        .device = std::move(hardwareDevice),
        .unavailableReason = {},
    };
}

void logLibplacebo(void*, enum pl_log_level level, char const* message) {
    switch (level) {
    case PL_LOG_FATAL:
    case PL_LOG_ERR:
        qCCritical(sunroomLogGraphics).noquote() << "libplacebo:" << message;
        break;
    case PL_LOG_WARN:
        qCWarning(sunroomLogGraphics).noquote() << "libplacebo:" << message;
        break;
    case PL_LOG_INFO:
        qCInfo(sunroomLogGraphics).noquote() << "libplacebo:" << message;
        break;
    case PL_LOG_DEBUG:
    case PL_LOG_TRACE:
        qCDebug(sunroomLogGraphics).noquote() << "libplacebo:" << message;
        break;
    case PL_LOG_NONE:
        break;
    }
}

class D3D11GraphicsDeviceDomain final : public GraphicsDeviceDomain {
  public:
    D3D11GraphicsDeviceDomain() {
        constexpr std::array featureLevels{
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        auto const createNativeDevice = [this, &featureLevels](UINT flags) {
            m_context.Reset();
            m_device.Reset();
            D3D_FEATURE_LEVEL selectedFeatureLevel{};
            HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels.data(),
                                               static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION, &m_device,
                                               &selectedFeatureLevel, &m_context);
            if (result == E_INVALIDARG) {
                result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels.data() + 1,
                                           static_cast<UINT>(featureLevels.size() - 1), D3D11_SDK_VERSION, &m_device,
                                           &selectedFeatureLevel, &m_context);
            }
            return result;
        };

        QString hardwareDecodeUnavailableReason;
        HRESULT createResult = createNativeDevice(D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT);
        if (FAILED(createResult)) {
            hardwareDecodeUnavailableReason =
                QStringLiteral("Could not create a video-capable D3D11 device "
                               "(0x%1)")
                    .arg(static_cast<unsigned long>(createResult), 8, 16, QLatin1Char('0'));
            createResult = createNativeDevice(D3D11_CREATE_DEVICE_BGRA_SUPPORT);
        }
        if (FAILED(createResult) || !m_device || !m_context) {
            qCCritical(sunroomLogGraphics, "Could not create the video-capable D3D11 device: 0x%08lx",
                       static_cast<unsigned long>(createResult));
            return;
        }

        ComPtr<ID3D11DeviceContext1> context1;
        if (FAILED(m_context.As(&context1))) {
            qCCritical(sunroomLogGraphics, "The D3D11 immediate context does not provide "
                                           "ID3D11DeviceContext1");
            return;
        }
        ComPtr<ID3D11VideoDevice> videoDevice;
        if (hardwareDecodeUnavailableReason.isEmpty() && FAILED(m_device.As(&videoDevice))) {
            hardwareDecodeUnavailableReason = QStringLiteral("The D3D11 device does not provide video decoding");
        }
        ComPtr<ID3D11Multithread> multithread;
        if (hardwareDecodeUnavailableReason.isEmpty()) {
            if (FAILED(m_context.As(&multithread))) {
                hardwareDecodeUnavailableReason = QStringLiteral("The D3D11 device does not expose "
                                                                 "multithread protection");
            } else {
                multithread->SetMultithreadProtected(TRUE);
                if (!multithread->GetMultithreadProtected()) {
                    hardwareDecodeUnavailableReason = QStringLiteral("Could not enable D3D11 "
                                                                     "multithread protection");
                }
            }
        }

        QRhiD3D11InitParams parameters;
        QRhiD3D11NativeHandles importedDevice;
        importedDevice.dev = m_device.Get();
        importedDevice.context = m_context.Get();
        m_rhi.reset(QRhi::create(QRhi::D3D11, &parameters, {}, &importedDevice));
        if (!m_rhi) {
            return;
        }

        pl_log_params logParameters{};
        logParameters.log_cb = logLibplacebo;
        logParameters.log_level = PL_LOG_WARN;
        m_log = pl_log_create(PL_API_VER, &logParameters);
        if (!m_log) {
            return;
        }

        auto const* const nativeHandles = static_cast<QRhiD3D11NativeHandles const*>(m_rhi->nativeHandles());
        if (!nativeHandles || !nativeHandles->dev || !nativeHandles->context) {
            qCCritical(sunroomLogGraphics, "QRhi did not expose its D3D11 device and immediate context");
            return;
        }
        if (nativeHandles->dev != m_device.Get() || nativeHandles->context != m_context.Get()) {
            qCCritical(sunroomLogGraphics, "QRhi did not retain the imported D3D11 device domain");
            return;
        }

        pl_d3d11_params libplaceboParameters{};
        libplaceboParameters.device = static_cast<ID3D11Device*>(nativeHandles->dev);
        m_d3d11 = pl_d3d11_create(m_log, &libplaceboParameters);
        if (!m_d3d11) {
            return;
        }

        ID3D11DeviceContext* libplaceboContext = nullptr;
        m_d3d11->device->GetImmediateContext(&libplaceboContext);
        bool const sharedImmediateContext = libplaceboContext == nativeHandles->context;
        if (libplaceboContext) {
            libplaceboContext->Release();
        }
        if (!sharedImmediateContext) {
            qCCritical(sunroomLogGraphics, "QRhi and libplacebo did not resolve the same D3D11 immediate context");
            return;
        }
        m_libplacebo = {
            .log = m_log,
            .gpu = m_d3d11->gpu,
        };
        m_videoDecode = hardwareDecodeUnavailableReason.isEmpty()
                            ? createVideoDecodeCapability(*m_device.Get(), generation(), m_executionState)
                            : VideoHardwareDecodeCapability{
                                  .device = {},
                                  .unavailableReason = std::move(hardwareDecodeUnavailableReason),
                              };
        if (!m_videoDecode.isAvailable()) {
            qCWarning(sunroomLogGraphics).noquote() << m_videoDecode.unavailableReason;
        }

        m_diagnostics.backend = GraphicsBackend::D3D11;
        m_diagnostics.backendName = QString::fromLatin1(m_rhi->backendName());
        m_diagnostics.nativeApi = QStringLiteral("D3D11");
        m_diagnostics.adapterName = QString::fromUtf8(m_rhi->driverInfo().deviceName);
        if (m_diagnostics.adapterName.isEmpty()) {
            m_diagnostics.adapterName = QStringLiteral("Unknown adapter");
        }
        Q_ASSERT(m_diagnostics.isValid());
    }

    ~D3D11GraphicsDeviceDomain() override {
        pl_d3d11_destroy(&m_d3d11);
        pl_log_destroy(&m_log);
    }

    QRhi& rhi() const override {
        Q_ASSERT(m_rhi);
        return *m_rhi;
    }

    GraphicsDeviceDiagnostics const& diagnostics() const override {
        Q_ASSERT(m_diagnostics.isValid());
        return m_diagnostics;
    }

    LibplaceboGraphicsContext const& libplaceboContext() const override {
        Q_ASSERT(m_libplacebo.isValid());
        return m_libplacebo;
    }

    VideoHardwareDecodeCapability const& videoDecodeCapability() const override { return m_videoDecode; }

    GraphicsDeviceExecutionScope acquireExecutionScope() override {
        m_executionState->mutex.lock();
        return GraphicsDeviceExecutionScope(m_executionState, unlockGraphicsExecution);
    }

    std::unique_ptr<VideoTargetInterop> createVideoTarget(VideoTargetRequest const& request) override {
        switch (request.producerApi) {
        case VideoProducerApi::Qrhi:
            return std::make_unique<QrhiVideoTarget>(*m_rhi, request.readback);
        case VideoProducerApi::Libplacebo:
            return std::make_unique<D3D11LibplaceboVideoTarget>(*m_rhi, m_libplacebo.gpu, request.readback);
        }
        return {};
    }

    std::unique_ptr<LibplaceboHardwareFrameImporter> createHardwareFrameImporter() override {
        return createD3D11LibplaceboFrameImporter(m_libplacebo.gpu);
    }

    bool isValid() const { return m_rhi && m_diagnostics.isValid() && m_libplacebo.isValid(); }

  private:
    std::shared_ptr<D3D11ExecutionState> m_executionState = std::make_shared<D3D11ExecutionState>();
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    std::unique_ptr<QRhi> m_rhi;
    pl_log m_log = nullptr;
    pl_d3d11 m_d3d11 = nullptr;
    LibplaceboGraphicsContext m_libplacebo;
    VideoHardwareDecodeCapability m_videoDecode;
    GraphicsDeviceDiagnostics m_diagnostics;
};
} // namespace

std::unique_ptr<GraphicsDeviceDomain> createD3D11GraphicsDeviceDomain() {
    auto domain = std::make_unique<D3D11GraphicsDeviceDomain>();
    if (!domain->isValid()) {
        return {};
    }
    return domain;
}
