#include "graphics/backends/D3D11GraphicsDeviceDomain.h"

#include <memory>

#include <QtCore/qlogging.h>
#include <libplacebo/d3d11.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include "graphics/GraphicsDeviceDomain.h"
#include "graphics/backends/D3D11LibplaceboVideoTarget.h"
#include "video/QrhiVideoTarget.h"
#include "video/VideoTargetInterop.h"

namespace {
void logLibplacebo(
        void *,
        enum pl_log_level level,
        const char *message) {
    switch (level) {
    case PL_LOG_FATAL:
    case PL_LOG_ERR:
        qCritical().noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_WARN:
        qWarning().noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_INFO:
        qInfo().noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_DEBUG:
    case PL_LOG_TRACE:
        qDebug().noquote()
            << "libplacebo:" << message;
        break;
    case PL_LOG_NONE:
        break;
    }
}

class D3D11GraphicsDeviceDomain final : public GraphicsDeviceDomain {
public:
    D3D11GraphicsDeviceDomain() {
        QRhiD3D11InitParams parameters;
        m_rhi.reset(QRhi::create(QRhi::D3D11, &parameters));
        if (!m_rhi)
            return;

        pl_log_params logParameters{};
        logParameters.log_cb = logLibplacebo;
        logParameters.log_level = PL_LOG_WARN;
        m_log = pl_log_create(PL_API_VER, &logParameters);
        if (!m_log)
            return;

        const auto *const nativeHandles =
            static_cast<const QRhiD3D11NativeHandles *>(
                m_rhi->nativeHandles());
        if (!nativeHandles || !nativeHandles->dev
                || !nativeHandles->context) {
            qCritical(
                "QRhi did not expose its D3D11 device and immediate context");
            return;
        }

        pl_d3d11_params libplaceboParameters{};
        libplaceboParameters.device =
            static_cast<ID3D11Device *>(nativeHandles->dev);
        m_d3d11 = pl_d3d11_create(
            m_log, &libplaceboParameters);
        if (!m_d3d11)
            return;

        ID3D11DeviceContext *libplaceboContext = nullptr;
        m_d3d11->device->GetImmediateContext(
            &libplaceboContext);
        const bool sharedImmediateContext =
            libplaceboContext == nativeHandles->context;
        if (libplaceboContext)
            libplaceboContext->Release();
        if (!sharedImmediateContext) {
            qCritical(
                "QRhi and libplacebo did not resolve the same D3D11 immediate context");
            return;
        }
        m_libplacebo = {
            .log = m_log,
            .gpu = m_d3d11->gpu,
        };

        m_diagnostics.backend = GraphicsBackend::D3D11;
        m_diagnostics.backendName =
            QString::fromLatin1(m_rhi->backendName());
        m_diagnostics.nativeApi = QStringLiteral("D3D11");
        m_diagnostics.adapterName =
            QString::fromUtf8(m_rhi->driverInfo().deviceName);
        if (m_diagnostics.adapterName.isEmpty())
            m_diagnostics.adapterName = QStringLiteral("Unknown adapter");
        Q_ASSERT(m_diagnostics.isValid());
    }

    ~D3D11GraphicsDeviceDomain() override {
        pl_d3d11_destroy(&m_d3d11);
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

    std::unique_ptr<VideoTargetInterop> createVideoTarget(
            const VideoTargetRequest &request) override {
        switch (request.producerApi) {
        case VideoProducerApi::Qrhi:
            return std::make_unique<QrhiVideoTarget>(
                *m_rhi, request.readback);
        case VideoProducerApi::Libplacebo:
            return std::make_unique<
                D3D11LibplaceboVideoTarget>(
                    *m_rhi,
                    m_libplacebo.gpu,
                    request.readback);
        }
        return {};
    }

    bool isValid() const {
        return m_rhi
            && m_diagnostics.isValid()
            && m_libplacebo.isValid();
    }

private:
    std::unique_ptr<QRhi> m_rhi;
    pl_log m_log = nullptr;
    pl_d3d11 m_d3d11 = nullptr;
    LibplaceboGraphicsContext m_libplacebo;
    GraphicsDeviceDiagnostics m_diagnostics;
};
}

std::unique_ptr<GraphicsDeviceDomain>
createD3D11GraphicsDeviceDomain() {
    auto domain = std::make_unique<D3D11GraphicsDeviceDomain>();
    if (!domain->isValid())
        return {};
    return domain;
}
