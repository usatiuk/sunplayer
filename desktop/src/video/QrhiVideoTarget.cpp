#include "video/QrhiVideoTarget.h"

#include <QtCore/qlogging.h>
#include <rhi/qrhi.h>

QrhiVideoTarget::QrhiVideoTarget(
        QRhi &rhi, VideoTargetReadback readback)
    : m_rhi(rhi), m_readback(readback) {
    m_diagnostics.synchronizationMode =
        QStringLiteral("Not active");
    m_diagnostics.fallbackReason =
        QStringLiteral("Video target not provisioned");
    Q_ASSERT(m_diagnostics.isValid());
}

QrhiVideoTarget::~QrhiVideoTarget() = default;

VideoTargetUpdate QrhiVideoTarget::ensureTarget(
        const RenderedVideoSurfaceDescription &description) {
    Q_ASSERT(description.isValid());
    Q_ASSERT(!m_producerAccessActive);
    Q_ASSERT(!m_submissionPending);
    Q_ASSERT(!m_compositionPrepared);
    if (m_producerAccessActive || m_submissionPending
            || m_compositionPrepared) {
        return VideoTargetUpdate::Unavailable;
    }

    if (!m_texture)
        return createTarget(description.pixelSize);
    if (m_texture->pixelSize() != description.pixelSize)
        return resizeTarget(description.pixelSize);
    return VideoTargetUpdate::Unchanged;
}

VideoOperationResult QrhiVideoTarget::beginProducerAccess(
        QRhiCommandBuffer &) {
    if (m_rhi.isDeviceLost())
        return VideoOperationResult::DeviceLost;
    Q_ASSERT(!m_producerAccessActive);
    Q_ASSERT(!m_submissionPending);
    Q_ASSERT(!m_compositionPrepared);
    if (!m_texture || m_producerAccessActive || m_submissionPending
            || m_compositionPrepared) {
        return VideoOperationResult::Unavailable;
    }
    m_producerAccessActive = true;
    m_submissionPending = true;
    return VideoOperationResult::Ready;
}

VideoOperationResult QrhiVideoTarget::endProducerAccess(
        QRhiCommandBuffer &) {
    if (m_rhi.isDeviceLost())
        return VideoOperationResult::DeviceLost;
    Q_ASSERT(m_producerAccessActive);
    Q_ASSERT(m_submissionPending);
    if (!m_texture || !m_producerAccessActive
            || !m_submissionPending) {
        return VideoOperationResult::Unavailable;
    }
    m_producerAccessActive = false;
    return VideoOperationResult::Ready;
}

VideoOperationResult QrhiVideoTarget::prepareForComposition(
        QRhiCommandBuffer &) {
    if (m_rhi.isDeviceLost())
        return VideoOperationResult::DeviceLost;
    Q_ASSERT(!m_producerAccessActive);
    Q_ASSERT(!m_compositionPrepared);
    if (!m_texture || m_producerAccessActive
            || m_compositionPrepared) {
        return VideoOperationResult::Unavailable;
    }
    m_submissionPending = true;
    m_compositionPrepared = true;
    return VideoOperationResult::Ready;
}

void QrhiVideoTarget::submissionAccepted() {
    if (!m_submissionPending)
        return;
    m_producerAccessActive = false;
    m_submissionPending = false;
    m_compositionPrepared = false;
}

void QrhiVideoTarget::submissionAborted() {
    m_producerAccessActive = false;
    m_submissionPending = false;
    m_compositionPrepared = false;
}

QRhiTextureRenderTarget *QrhiVideoTarget::qrhiRenderTarget() const {
    return m_renderTarget.get();
}

QRhiRenderPassDescriptor *
QrhiVideoTarget::qrhiRenderPassDescriptor() const {
    return m_renderPassDescriptor.get();
}

pl_tex QrhiVideoTarget::libplaceboRenderTarget() const {
    return nullptr;
}

QRhiTexture &QrhiVideoTarget::textureForComposition() const {
    Q_ASSERT(m_texture);
    return *m_texture;
}

std::uint64_t QrhiVideoTarget::compositionTextureRevision() const {
    return m_compositionTextureRevision;
}

const VideoTargetInteropDiagnostics &
QrhiVideoTarget::diagnostics() const {
    Q_ASSERT(m_diagnostics.isValid());
    return m_diagnostics;
}

VideoTargetUpdate QrhiVideoTarget::createTarget(const QSize &pixelSize) {
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(!m_texture);
    Q_ASSERT(!m_renderPassDescriptor);
    Q_ASSERT(!m_renderTarget);

    QRhiTexture::Flags textureFlags = QRhiTexture::RenderTarget;
    if (m_readback == VideoTargetReadback::Enabled)
        textureFlags |= QRhiTexture::UsedAsTransferSource;
    if (!m_rhi.isTextureFormatSupported(
            QRhiTexture::RGBA16F, textureFlags)) {
        setUnavailableDiagnostics(QStringLiteral(
            "RGBA16F render target is unsupported"));
        qWarning("The active QRhi backend cannot create the requested RGBA16F video target");
        return VideoTargetUpdate::Unavailable;
    }

    m_texture.reset(m_rhi.newTexture(
        QRhiTexture::RGBA16F, pixelSize, 1, textureFlags));
    m_texture->setName(QByteArrayLiteral("Sunroom video surface"));
    if (!m_texture->create()) {
        if (m_rhi.isDeviceLost()) {
            setUnavailableDiagnostics(
                QStringLiteral("Graphics device lost"));
            return VideoTargetUpdate::DeviceLost;
        }
        setUnavailableDiagnostics(
            QStringLiteral("Could not create the video texture"));
        qWarning("Could not create the video texture");
        return VideoTargetUpdate::Unavailable;
    }

    const QRhiTextureRenderTargetDescription description(
        QRhiColorAttachment(m_texture.get()));
    m_renderTarget.reset(m_rhi.newTextureRenderTarget(description));
    m_renderTarget->setName(
        QByteArrayLiteral("Sunroom video render target"));
    m_renderPassDescriptor.reset(
        m_renderTarget->newCompatibleRenderPassDescriptor());
    m_renderTarget->setRenderPassDescriptor(
        m_renderPassDescriptor.get());
    if (!m_renderTarget->create()) {
        if (m_rhi.isDeviceLost()) {
            setUnavailableDiagnostics(
                QStringLiteral("Graphics device lost"));
            return VideoTargetUpdate::DeviceLost;
        }
        setUnavailableDiagnostics(
            QStringLiteral("Could not create the video render target"));
        qWarning("Could not create the video render target");
        return VideoTargetUpdate::Unavailable;
    }

    setDirectDiagnostics();
    ++m_compositionTextureRevision;
    if (m_compositionTextureRevision == 0)
        ++m_compositionTextureRevision;
    return VideoTargetUpdate::Created;
}

VideoTargetUpdate QrhiVideoTarget::resizeTarget(const QSize &pixelSize) {
    Q_ASSERT(m_texture);
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(m_texture->pixelSize() != pixelSize);

    m_texture->setPixelSize(pixelSize);
    if (!m_texture->create()) {
        if (m_rhi.isDeviceLost()) {
            setUnavailableDiagnostics(
                QStringLiteral("Graphics device lost"));
            return VideoTargetUpdate::DeviceLost;
        }
        setUnavailableDiagnostics(
            QStringLiteral("Could not resize the video texture"));
        qWarning("Could not resize the video texture");
        return VideoTargetUpdate::Unavailable;
    }
    setDirectDiagnostics();
    ++m_compositionTextureRevision;
    if (m_compositionTextureRevision == 0)
        ++m_compositionTextureRevision;
    return VideoTargetUpdate::Resized;
}

void QrhiVideoTarget::setDirectDiagnostics() {
    m_diagnostics = {};
    m_diagnostics.outputPath = VideoOutputPath::DirectRenderTarget;
    m_diagnostics.synchronizationMode =
        QStringLiteral("QRhi command-buffer ordering");
    Q_ASSERT(m_diagnostics.isValid());
}

void QrhiVideoTarget::setUnavailableDiagnostics(
        const QString &reason) {
    m_diagnostics = {};
    m_diagnostics.outputPath = VideoOutputPath::Unavailable;
    m_diagnostics.synchronizationMode =
        QStringLiteral("Not active");
    m_diagnostics.fallbackReason = reason;
    Q_ASSERT(m_diagnostics.isValid());
}
