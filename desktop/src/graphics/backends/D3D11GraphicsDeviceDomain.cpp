#include "graphics/backends/D3D11GraphicsDeviceDomain.h"

#include <memory>

#include <rhi/qrhi.h>

#include "graphics/GraphicsDeviceDomain.h"
#include "video/QrhiVideoTarget.h"
#include "video/VideoTargetInterop.h"

namespace {
class D3D11GraphicsDeviceDomain final : public GraphicsDeviceDomain {
public:
    D3D11GraphicsDeviceDomain() {
        QRhiD3D11InitParams parameters;
        m_rhi.reset(QRhi::create(QRhi::D3D11, &parameters));
        if (!m_rhi)
            return;

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

    QRhi &rhi() const override {
        Q_ASSERT(m_rhi);
        return *m_rhi;
    }

    const GraphicsDeviceDiagnostics &diagnostics() const override {
        Q_ASSERT(m_diagnostics.isValid());
        return m_diagnostics;
    }

    std::unique_ptr<VideoTargetInterop> createVideoTarget(
            const VideoTargetRequest &request) override {
        if (request.producerApi != VideoProducerApi::Qrhi)
            return {};
        return std::make_unique<QrhiVideoTarget>(
            *m_rhi, request.readback);
    }

    bool isValid() const {
        return m_rhi && m_diagnostics.isValid();
    }

private:
    std::unique_ptr<QRhi> m_rhi;
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
