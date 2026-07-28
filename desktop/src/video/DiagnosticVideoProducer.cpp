#include "video/DiagnosticVideoProducer.h"

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <QFile>
#include <QtCore/qlogging.h>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include "graphics/GraphicsDeviceDomain.h"
#include "video/DiagnosticVideoSource.h"

namespace {
struct alignas(16) DiagnosticVideoParameters {
    // Headroom values are multiples of SDR white, not absolute nits.
    float sourcePeak = 12.5f;
    float targetPeak = 1.0f;
    float phase = 0.0f;
    float toneMappingEnabled = 1.0f;
    float canonicalYFlip = 0.0f;
    std::array<float, 3> padding{};
};

static_assert(std::is_standard_layout_v<DiagnosticVideoParameters>);
static_assert(sizeof(DiagnosticVideoParameters) == 32);
static_assert(offsetof(DiagnosticVideoParameters, canonicalYFlip) == 16);

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
        GraphicsDeviceDomain &graphicsDevice,
        const DiagnosticVideoSource &source,
        VideoTargetReadback readback)
    : m_rhi(graphicsDevice.rhi()),
      m_source(source),
      m_target(graphicsDevice.createVideoTarget({
          .producerApi = VideoProducerApi::Qrhi,
          .readback = readback,
      })) {
    if (!m_target)
        qFatal("The graphics backend does not provide a QRhi video target");
}
DiagnosticVideoProducer::~DiagnosticVideoProducer() = default;

VideoOperationResult
DiagnosticVideoProducer::ensureSurface(
        const RenderedVideoSurfaceState &requestedState) {
    Q_ASSERT(requestedState.isValid());

    const VideoTargetUpdate update =
        m_target->ensureTarget(requestedState.description);
    if (update == VideoTargetUpdate::DeviceLost)
        return VideoOperationResult::DeviceLost;
    if (update == VideoTargetUpdate::Unavailable)
        return VideoOperationResult::Unavailable;
    if (update == VideoTargetUpdate::Created)
        return createResources();
    if (update == VideoTargetUpdate::Resized) {
        m_completedState.reset();
        m_pendingState.reset();
    }
    return VideoOperationResult::Ready;
}

bool DiagnosticVideoProducer::needsRender(
        const RenderedVideoSurfaceState &requestedState) const {
    return !m_completedState
        || !m_completedState->isReusableFor(requestedState);
}

VideoOperationResult DiagnosticVideoProducer::render(
        QRhiCommandBuffer &commandBuffer,
        const RenderedVideoSurfaceState &requestedState) {
    Q_ASSERT(requestedState.isValid());
    Q_ASSERT(m_target->textureForComposition().pixelSize()
             == requestedState.description.pixelSize);
    Q_ASSERT(m_uniformBuffer);
    Q_ASSERT(m_bindings);
    Q_ASSERT(m_pipeline);
    Q_ASSERT(!m_pendingState);

    DiagnosticVideoParameters parameters;
    parameters.sourcePeak = m_source.sourcePeakHeadroom();
    parameters.targetPeak =
        requestedState.description.targetPeakHeadroom;
    parameters.phase = m_source.phase();
    parameters.toneMappingEnabled =
        m_source.toneMappingEnabled() ? 1.0f : 0.0f;
    parameters.canonicalYFlip =
        m_rhi.isYUpInNDC() != m_rhi.isYUpInFramebuffer()
        ? 1.0f
        : 0.0f;

    const QSize pixelSize = requestedState.description.pixelSize;
    QRhiTextureRenderTarget *const renderTarget =
        m_target->qrhiRenderTarget();
    if (!renderTarget)
        return VideoOperationResult::Unavailable;

    const VideoOperationResult beginResult =
        m_target->beginProducerAccess(commandBuffer);
    if (beginResult != VideoOperationResult::Ready)
        return beginResult;

    QRhiResourceUpdateBatch *updates = m_rhi.nextResourceUpdateBatch();
    updates->updateDynamicBuffer(
        m_uniformBuffer.get(), 0, sizeof(parameters), &parameters);
    commandBuffer.beginPass(
        renderTarget, Qt::black, {1.0f, 0}, updates);
    commandBuffer.setGraphicsPipeline(m_pipeline.get());
    commandBuffer.setViewport(
        {0.0f, 0.0f,
         static_cast<float>(pixelSize.width()),
         static_cast<float>(pixelSize.height())});
    commandBuffer.setShaderResources(m_bindings.get());
    commandBuffer.draw(3);
    commandBuffer.endPass();

    const VideoOperationResult endResult =
        m_target->endProducerAccess(commandBuffer);
    if (endResult != VideoOperationResult::Ready)
        return endResult;

    m_pendingState = requestedState;
    return VideoOperationResult::Ready;
}

VideoOperationResult DiagnosticVideoProducer::prepareForComposition(
        QRhiCommandBuffer &commandBuffer) {
    return m_target->prepareForComposition(commandBuffer);
}

void DiagnosticVideoProducer::submissionAccepted() {
    m_target->submissionAccepted();
}

void DiagnosticVideoProducer::submissionAborted() {
    m_target->submissionAborted();
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

QRhiTexture &DiagnosticVideoProducer::textureForComposition() const {
    return m_target->textureForComposition();
}

std::uint64_t
DiagnosticVideoProducer::compositionTextureRevision() const {
    return m_target->compositionTextureRevision();
}

RenderedVideoProducerDiagnostics
DiagnosticVideoProducer::diagnostics() const {
    RenderedVideoProducerDiagnostics result;
    result.producerName = QStringLiteral("Diagnostic pattern");
    result.target = m_target->diagnostics();
    Q_ASSERT(result.isValid());
    return result;
}

VideoOperationResult
DiagnosticVideoProducer::createResources() {
    Q_ASSERT(!m_uniformBuffer);
    Q_ASSERT(!m_bindings);
    Q_ASSERT(!m_pipeline);

    QRhiRenderPassDescriptor *const renderPassDescriptor =
        m_target->qrhiRenderPassDescriptor();
    if (!renderPassDescriptor)
        return VideoOperationResult::Unavailable;

    m_uniformBuffer.reset(m_rhi.newBuffer(
        QRhiBuffer::Dynamic,
        QRhiBuffer::UniformBuffer,
        m_rhi.ubufAligned(sizeof(DiagnosticVideoParameters))));
    m_uniformBuffer->setName(
        QByteArrayLiteral("Sunroom diagnostic video parameters"));
    if (!m_uniformBuffer->create()) {
        if (m_rhi.isDeviceLost())
            return VideoOperationResult::DeviceLost;
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
            return VideoOperationResult::DeviceLost;
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
    m_pipeline->setRenderPassDescriptor(renderPassDescriptor);
    if (!m_pipeline->create()) {
        if (m_rhi.isDeviceLost())
            return VideoOperationResult::DeviceLost;
        qFatal("Could not create the diagnostic video graphics pipeline");
    }
    return VideoOperationResult::Ready;
}
