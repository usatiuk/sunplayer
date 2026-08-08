#include "graphics/backends/D3D11LibplaceboVideoTarget.h"

#include <cstdint>

#include <QtCore/qlogging.h>
#include <QtGui/qcolor.h>
#include <libplacebo/d3d11.h>
#include <rhi/qrhi.h>

D3D11LibplaceboVideoTarget::D3D11LibplaceboVideoTarget(QRhi& rhi, pl_gpu gpu, VideoTargetReadback readback)
    : m_rhi(rhi), m_gpu(gpu), m_readback(readback) {
    Q_ASSERT(m_gpu);
    setUnavailableDiagnostics(QStringLiteral("Libplacebo video target not provisioned"));
}

D3D11LibplaceboVideoTarget::~D3D11LibplaceboVideoTarget() { resetTarget(); }

VideoTargetUpdate D3D11LibplaceboVideoTarget::ensureTarget(RenderedVideoSurfaceDescription const& description) {
    Q_ASSERT(description.isValid());
    Q_ASSERT(!m_producerAccessActive);
    Q_ASSERT(!m_submissionPending);
    Q_ASSERT(!m_compositionPrepared);
    if (m_producerAccessActive || m_submissionPending || m_compositionPrepared) {
        return VideoTargetUpdate::Unavailable;
    }
    if (deviceLost()) {
        return VideoTargetUpdate::DeviceLost;
    }

    if (!m_texture) {
        return createTarget(description.pixelSize);
    }
    if (m_texture->pixelSize() != description.pixelSize) {
        return resizeTarget(description.pixelSize);
    }
    if (!m_wrappedTexture) {
        if (!wrapTexture()) {
            return deviceLost() ? VideoTargetUpdate::DeviceLost : VideoTargetUpdate::Unavailable;
        }
        setDirectDiagnostics();
    }
    return VideoTargetUpdate::Unchanged;
}

VideoOperationResult D3D11LibplaceboVideoTarget::beginProducerAccess(QRhiCommandBuffer& commandBuffer) {
    if (deviceLost()) {
        return VideoOperationResult::DeviceLost;
    }
    Q_ASSERT(!m_producerAccessActive);
    Q_ASSERT(!m_submissionPending);
    Q_ASSERT(!m_compositionPrepared);
    if (!m_wrappedTexture || !m_renderTarget || m_producerAccessActive || m_submissionPending ||
        m_compositionPrepared) {
        return VideoOperationResult::Unavailable;
    }

    commandBuffer.beginPass(m_renderTarget.get(), Qt::black, {1.0f, 0}, nullptr, QRhiCommandBuffer::ExternalContent);
    commandBuffer.beginExternal();
    m_producerAccessActive = true;
    m_submissionPending = true;
    return VideoOperationResult::Ready;
}

VideoOperationResult D3D11LibplaceboVideoTarget::endProducerAccess(QRhiCommandBuffer& commandBuffer) {
    Q_ASSERT(m_producerAccessActive);
    Q_ASSERT(m_submissionPending);
    if (!m_producerAccessActive || !m_submissionPending) {
        return VideoOperationResult::Unavailable;
    }

    commandBuffer.endExternal();
    commandBuffer.endPass();
    m_producerAccessActive = false;
    return deviceLost() ? VideoOperationResult::DeviceLost : VideoOperationResult::Ready;
}

VideoOperationResult D3D11LibplaceboVideoTarget::prepareForComposition(QRhiCommandBuffer&) {
    if (deviceLost()) {
        return VideoOperationResult::DeviceLost;
    }
    Q_ASSERT(!m_producerAccessActive);
    Q_ASSERT(!m_compositionPrepared);
    if (!m_wrappedTexture || m_producerAccessActive || m_compositionPrepared) {
        return VideoOperationResult::Unavailable;
    }
    m_submissionPending = true;
    m_compositionPrepared = true;
    return VideoOperationResult::Ready;
}

void D3D11LibplaceboVideoTarget::submissionAccepted() {
    if (!m_submissionPending) {
        return;
    }
    m_producerAccessActive = false;
    m_submissionPending = false;
    m_compositionPrepared = false;
}

void D3D11LibplaceboVideoTarget::submissionAborted() {
    m_producerAccessActive = false;
    m_submissionPending = false;
    m_compositionPrepared = false;
}

QRhiTextureRenderTarget* D3D11LibplaceboVideoTarget::qrhiRenderTarget() const { return m_renderTarget.get(); }

QRhiRenderPassDescriptor* D3D11LibplaceboVideoTarget::qrhiRenderPassDescriptor() const {
    return m_renderPassDescriptor.get();
}

pl_tex D3D11LibplaceboVideoTarget::libplaceboRenderTarget() const { return m_wrappedTexture; }

QRhiTexture& D3D11LibplaceboVideoTarget::textureForComposition() const {
    Q_ASSERT(m_texture);
    return *m_texture;
}

std::uint64_t D3D11LibplaceboVideoTarget::compositionTextureRevision() const { return m_compositionTextureRevision; }

VideoTargetInteropDiagnostics const& D3D11LibplaceboVideoTarget::diagnostics() const {
    Q_ASSERT(m_diagnostics.isValid());
    return m_diagnostics;
}

VideoTargetUpdate D3D11LibplaceboVideoTarget::createTarget(QSize const& pixelSize) {
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(!m_texture);

    QRhiTexture::Flags textureFlags = QRhiTexture::RenderTarget;
    if (m_readback == VideoTargetReadback::Enabled) {
        textureFlags |= QRhiTexture::UsedAsTransferSource;
    }
    if (!m_rhi.isTextureFormatSupported(QRhiTexture::RGBA16F, textureFlags)) {
        setUnavailableDiagnostics(QStringLiteral("RGBA16F render target is unsupported"));
        return VideoTargetUpdate::Unavailable;
    }

    m_texture.reset(m_rhi.newTexture(QRhiTexture::RGBA16F, pixelSize, 1, textureFlags));
    m_texture->setName(QByteArrayLiteral("Sunroom libplacebo video surface"));
    if (!m_texture->create()) {
        bool const lost = deviceLost();
        setUnavailableDiagnostics(lost ? QStringLiteral("Graphics device lost")
                                       : QStringLiteral("Could not create the libplacebo video texture"));
        resetTarget();
        return lost ? VideoTargetUpdate::DeviceLost : VideoTargetUpdate::Unavailable;
    }

    QRhiTextureRenderTargetDescription const description(QRhiColorAttachment(m_texture.get()));
    m_renderTarget.reset(m_rhi.newTextureRenderTarget(description));
    m_renderTarget->setName(QByteArrayLiteral("Sunroom libplacebo external render target"));
    m_renderPassDescriptor.reset(m_renderTarget->newCompatibleRenderPassDescriptor());
    m_renderTarget->setRenderPassDescriptor(m_renderPassDescriptor.get());
    if (!m_renderTarget->create()) {
        bool const lost = deviceLost();
        setUnavailableDiagnostics(lost ? QStringLiteral("Graphics device lost")
                                       : QStringLiteral("Could not create the external-command render target"));
        resetTarget();
        return lost ? VideoTargetUpdate::DeviceLost : VideoTargetUpdate::Unavailable;
    }
    if (!wrapTexture()) {
        bool const lost = deviceLost();
        resetTarget();
        return lost ? VideoTargetUpdate::DeviceLost : VideoTargetUpdate::Unavailable;
    }

    setDirectDiagnostics();
    advanceTextureRevision();
    return VideoTargetUpdate::Created;
}

VideoTargetUpdate D3D11LibplaceboVideoTarget::resizeTarget(QSize const& pixelSize) {
    Q_ASSERT(m_texture);
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(m_texture->pixelSize() != pixelSize);

    pl_tex_destroy(m_gpu, &m_wrappedTexture);
    m_texture->setPixelSize(pixelSize);
    if (!m_texture->create()) {
        bool const lost = deviceLost();
        setUnavailableDiagnostics(lost ? QStringLiteral("Graphics device lost")
                                       : QStringLiteral("Could not resize the libplacebo video texture"));
        resetTarget();
        return lost ? VideoTargetUpdate::DeviceLost : VideoTargetUpdate::Unavailable;
    }
    if (!wrapTexture()) {
        bool const lost = deviceLost();
        resetTarget();
        return lost ? VideoTargetUpdate::DeviceLost : VideoTargetUpdate::Unavailable;
    }

    setDirectDiagnostics();
    advanceTextureRevision();
    return VideoTargetUpdate::Resized;
}

bool D3D11LibplaceboVideoTarget::wrapTexture() {
    Q_ASSERT(m_texture);
    Q_ASSERT(!m_wrappedTexture);

    QRhiTexture::NativeTexture const native = m_texture->nativeTexture();
    auto* const texture = reinterpret_cast<ID3D11Texture2D*>(static_cast<std::uintptr_t>(native.object));
    if (!texture) {
        setUnavailableDiagnostics(QStringLiteral("QRhi did not expose its D3D11 video texture"));
        return false;
    }

    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || description.Usage != D3D11_USAGE_DEFAULT ||
        description.MipLevels != 1 || description.SampleDesc.Count != 1 ||
        !(description.BindFlags & D3D11_BIND_RENDER_TARGET)) {
        setUnavailableDiagnostics(QStringLiteral("QRhi exposed an incompatible D3D11 video texture"));
        return false;
    }

    pl_d3d11_wrap_params parameters{};
    parameters.tex = texture;
    m_wrappedTexture = pl_d3d11_wrap(m_gpu, &parameters);
    if (!m_wrappedTexture || !m_wrappedTexture->params.renderable) {
        pl_tex_destroy(m_gpu, &m_wrappedTexture);
        setUnavailableDiagnostics(QStringLiteral("Libplacebo could not wrap the QRhi RGBA16F render target"));
        return false;
    }
    return true;
}

bool D3D11LibplaceboVideoTarget::deviceLost() const { return m_rhi.isDeviceLost() || pl_gpu_is_failed(m_gpu); }

void D3D11LibplaceboVideoTarget::resetTarget() {
    pl_tex_destroy(m_gpu, &m_wrappedTexture);
    m_renderTarget.reset();
    m_renderPassDescriptor.reset();
    m_texture.reset();
    m_producerAccessActive = false;
    m_submissionPending = false;
    m_compositionPrepared = false;
}

void D3D11LibplaceboVideoTarget::setDirectDiagnostics() {
    Q_ASSERT(m_wrappedTexture);
    m_diagnostics = {};
    m_diagnostics.outputPath = VideoOutputPath::DirectRenderTarget;
    m_diagnostics.synchronizationMode = QStringLiteral("Shared D3D11 immediate context · QRhi external commands · %1")
                                            .arg(QString::fromLatin1(m_wrappedTexture->params.format->name));
    Q_ASSERT(m_diagnostics.isValid());
}

void D3D11LibplaceboVideoTarget::setUnavailableDiagnostics(QString const& reason) {
    m_diagnostics = {};
    m_diagnostics.outputPath = VideoOutputPath::Unavailable;
    m_diagnostics.synchronizationMode = QStringLiteral("Not active");
    m_diagnostics.fallbackReason = reason;
    Q_ASSERT(m_diagnostics.isValid());
}

void D3D11LibplaceboVideoTarget::advanceTextureRevision() {
    ++m_compositionTextureRevision;
    if (m_compositionTextureRevision == 0) {
        ++m_compositionTextureRevision;
    }
}
