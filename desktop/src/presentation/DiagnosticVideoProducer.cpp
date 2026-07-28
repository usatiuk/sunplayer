#include "presentation/DiagnosticVideoProducer.h"

#include <QFile>
#include <QtCore/qlogging.h>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <utility>

namespace {
QShader loadShader(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Could not open packaged shader: %s", qPrintable(path));
    QShader shader = QShader::fromSerialized(file.readAll());
    if (!shader.isValid())
        qFatal("Packaged shader is invalid: %s", qPrintable(path));
    return shader;
}
}

DiagnosticVideoProducer::DiagnosticVideoProducer(
        QRhi &rhi, CaptureMode captureMode)
    : m_rhi(rhi), m_captureMode(captureMode) {}
DiagnosticVideoProducer::~DiagnosticVideoProducer() = default;

DiagnosticVideoProducer::ResourceResult
DiagnosticVideoProducer::ensureSurface(
        const RenderedVideoSurfaceState &requestedState) {
    Q_ASSERT(requestedState.isValid());

    if (!m_texture)
        return createResources(requestedState.description.pixelSize);
    if (m_texture->pixelSize() != requestedState.description.pixelSize)
        return resizeTexture(requestedState.description.pixelSize);
    return ResourceResult::Ready;
}

bool DiagnosticVideoProducer::needsRender(
        const RenderedVideoSurfaceState &requestedState) const {
    return !m_completedState
        || !m_completedState->isReusableFor(requestedState);
}

void DiagnosticVideoProducer::render(
        QRhiCommandBuffer &commandBuffer,
        const DiagnosticVideoParameters &parameters,
        const RenderedVideoSurfaceState &completedState) {
    Q_ASSERT(completedState.isValid());
    Q_ASSERT(m_texture);
    Q_ASSERT(m_texture->pixelSize() == completedState.description.pixelSize);
    Q_ASSERT(m_renderTarget);
    Q_ASSERT(m_uniformBuffer);
    Q_ASSERT(m_bindings);
    Q_ASSERT(m_pipeline);
    Q_ASSERT(!m_pendingState);

    QRhiResourceUpdateBatch *updates = m_rhi.nextResourceUpdateBatch();
    updates->updateDynamicBuffer(
        m_uniformBuffer.get(), 0, sizeof(parameters), &parameters);

    const QSize pixelSize = completedState.description.pixelSize;
    commandBuffer.beginPass(
        m_renderTarget.get(), Qt::black, {1.0f, 0}, updates);
    commandBuffer.setGraphicsPipeline(m_pipeline.get());
    commandBuffer.setViewport(
        {0.0f, 0.0f,
         static_cast<float>(pixelSize.width()),
         static_cast<float>(pixelSize.height())});
    commandBuffer.setShaderResources(m_bindings.get());
    commandBuffer.draw(3);
    commandBuffer.endPass();

    m_pendingState = completedState;
}

void DiagnosticVideoProducer::commitPendingRender() {
    if (!m_pendingState)
        return;
    m_completedState = std::move(m_pendingState);
    m_pendingState.reset();
}

void DiagnosticVideoProducer::discardPendingRender() {
    m_pendingState.reset();
}

QRhiTexture &DiagnosticVideoProducer::texture() const {
    Q_ASSERT(m_texture);
    return *m_texture;
}

const std::optional<RenderedVideoSurfaceState> &
DiagnosticVideoProducer::completedState() const {
    return m_completedState;
}

DiagnosticVideoProducer::ResourceResult
DiagnosticVideoProducer::createResources(const QSize &pixelSize) {
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(!m_texture);
    Q_ASSERT(!m_renderTarget);
    Q_ASSERT(!m_renderPassDescriptor);
    Q_ASSERT(!m_uniformBuffer);
    Q_ASSERT(!m_bindings);
    Q_ASSERT(!m_pipeline);

    QRhiTexture::Flags textureFlags = QRhiTexture::RenderTarget;
    if (m_captureMode == CaptureMode::Enabled)
        textureFlags |= QRhiTexture::UsedAsTransferSource;
    if (!m_rhi.isTextureFormatSupported(
            QRhiTexture::RGBA16F, textureFlags)) {
        qFatal(
            "The active QRhi backend cannot create the requested RGBA16F diagnostic surface");
    }

    m_texture.reset(m_rhi.newTexture(
        QRhiTexture::RGBA16F, pixelSize, 1, textureFlags));
    m_texture->setName(QByteArrayLiteral("Sunroom diagnostic video surface"));
    if (!m_texture->create()) {
        if (m_rhi.isDeviceLost())
            return ResourceResult::DeviceLost;
        qFatal("Could not create the diagnostic video texture");
    }

    const QRhiTextureRenderTargetDescription description(
        QRhiColorAttachment(m_texture.get()));
    m_renderTarget.reset(m_rhi.newTextureRenderTarget(description));
    m_renderTarget->setName(
        QByteArrayLiteral("Sunroom diagnostic video render target"));
    m_renderPassDescriptor.reset(
        m_renderTarget->newCompatibleRenderPassDescriptor());
    m_renderTarget->setRenderPassDescriptor(m_renderPassDescriptor.get());
    if (!m_renderTarget->create()) {
        if (m_rhi.isDeviceLost())
            return ResourceResult::DeviceLost;
        qFatal("Could not create the diagnostic video render target");
    }

    m_uniformBuffer.reset(m_rhi.newBuffer(
        QRhiBuffer::Dynamic,
        QRhiBuffer::UniformBuffer,
        m_rhi.ubufAligned(sizeof(DiagnosticVideoParameters))));
    m_uniformBuffer->setName(
        QByteArrayLiteral("Sunroom diagnostic video parameters"));
    if (!m_uniformBuffer->create()) {
        if (m_rhi.isDeviceLost())
            return ResourceResult::DeviceLost;
        qFatal("Could not create the diagnostic video uniform buffer");
    }

    m_bindings.reset(m_rhi.newShaderResourceBindings());
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::FragmentStage,
            m_uniformBuffer.get()),
    });
    if (!m_bindings->create()) {
        if (m_rhi.isDeviceLost())
            return ResourceResult::DeviceLost;
        qFatal("Could not create the diagnostic video resource bindings");
    }

    const QShader vertexShader =
        loadShader(QStringLiteral(":/shaders/fullscreen.vert.qsb"));
    const QShader fragmentShader =
        loadShader(QStringLiteral(":/shaders/diagnostic_video.frag.qsb"));
    m_pipeline.reset(m_rhi.newGraphicsPipeline());
    m_pipeline->setName(
        QByteArrayLiteral("Sunroom diagnostic video pipeline"));
    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertexShader},
        {QRhiShaderStage::Fragment, fragmentShader},
    });
    m_pipeline->setVertexInputLayout({});
    m_pipeline->setShaderResourceBindings(m_bindings.get());
    m_pipeline->setRenderPassDescriptor(m_renderPassDescriptor.get());
    if (!m_pipeline->create()) {
        if (m_rhi.isDeviceLost())
            return ResourceResult::DeviceLost;
        qFatal("Could not create the diagnostic video graphics pipeline");
    }
    return ResourceResult::Ready;
}

DiagnosticVideoProducer::ResourceResult
DiagnosticVideoProducer::resizeTexture(const QSize &pixelSize) {
    Q_ASSERT(m_texture);
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(m_texture->pixelSize() != pixelSize);

    m_completedState.reset();
    m_pendingState.reset();
    m_texture->setPixelSize(pixelSize);
    if (!m_texture->create()) {
        if (m_rhi.isDeviceLost())
            return ResourceResult::DeviceLost;
        qFatal("Could not resize the diagnostic video texture");
    }
    return ResourceResult::Ready;
}
