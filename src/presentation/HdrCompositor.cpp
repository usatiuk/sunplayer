#include "presentation/HdrCompositor.h"

#include <QFile>
#include <QImage>
#include <QtCore/qlogging.h>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include "diagnostics/LogCategories.h"

namespace {
QShader loadShader(QString const& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCFatal(sunplayerLogPresentation, "Could not open packaged shader: %s", qPrintable(path));
    }
    QShader shader = QShader::fromSerialized(file.readAll());
    if (!shader.isValid()) {
        qCFatal(sunplayerLogPresentation, "Packaged shader is invalid: %s", qPrintable(path));
    }
    return shader;
}
} // namespace

HdrCompositor::HdrCompositor(QRhi& rhi) : m_rhi(rhi) {}
HdrCompositor::~HdrCompositor() = default;

HdrCompositor::ResourceResult HdrCompositor::initialize(QRhiRenderPassDescriptor& renderPassDescriptor,
                                                        QRhiTexture* videoTexture, QRhiTexture* subtitleTexture,
                                                        QRhiTexture& uiTexture) {
    Q_ASSERT(!m_uniformBuffer);
    Q_ASSERT(!m_sampler);
    Q_ASSERT(!m_emptyLayerTexture);
    Q_ASSERT(!m_bindings);
    Q_ASSERT(!m_pipeline);

    m_uniformBuffer.reset(m_rhi.newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                          m_rhi.ubufAligned(sizeof(HdrCompositorParameters))));
    if (!m_uniformBuffer->create()) {
        if (m_rhi.isDeviceLost()) {
            return ResourceResult::DeviceLost;
        }
        qCFatal(sunplayerLogPresentation, "Could not create the HDR compositor uniform buffer");
    }

    m_sampler.reset(m_rhi.newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if (!m_sampler->create()) {
        if (m_rhi.isDeviceLost()) {
            return ResourceResult::DeviceLost;
        }
        qCFatal(sunplayerLogPresentation, "Could not create the HDR compositor sampler");
    }

    m_emptyLayerTexture.reset(m_rhi.newTexture(QRhiTexture::RGBA8, {1, 1}, 1));
    m_emptyLayerTexture->setName(QByteArrayLiteral("SunPlayer empty composition layer"));
    if (!m_emptyLayerTexture->create()) {
        if (m_rhi.isDeviceLost()) {
            return ResourceResult::DeviceLost;
        }
        qCFatal(sunplayerLogPresentation, "Could not create the empty composition-layer texture");
    }
    if (createBindings(videoTexture, subtitleTexture, uiTexture) == ResourceResult::DeviceLost) {
        return ResourceResult::DeviceLost;
    }

    QShader const vertexShader = loadShader(QStringLiteral(":/shaders/fullscreen.vert.qsb"));
    QShader const fragmentShader = loadShader(QStringLiteral(":/shaders/compositor.frag.qsb"));
    m_pipeline.reset(m_rhi.newGraphicsPipeline());
    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertexShader},
        {QRhiShaderStage::Fragment, fragmentShader},
    });
    m_pipeline->setVertexInputLayout({});
    m_pipeline->setShaderResourceBindings(m_bindings.get());
    m_pipeline->setRenderPassDescriptor(&renderPassDescriptor);
    if (!m_pipeline->create()) {
        if (m_rhi.isDeviceLost()) {
            return ResourceResult::DeviceLost;
        }
        qCFatal(sunplayerLogPresentation, "Could not create the HDR compositor graphics pipeline");
    }
    return ResourceResult::Ready;
}

HdrCompositor::ResourceResult HdrCompositor::setTextures(QRhiTexture* videoTexture, QRhiTexture* subtitleTexture,
                                                         QRhiTexture& uiTexture) {
    return createBindings(videoTexture, subtitleTexture, uiTexture);
}

void HdrCompositor::render(QRhiCommandBuffer& commandBuffer, QRhiRenderTarget& renderTarget, QSize const& pixelSize,
                           HdrCompositorParameters const& parameters) {
    Q_ASSERT(!pixelSize.isEmpty());
    Q_ASSERT(m_uniformBuffer);
    Q_ASSERT(m_sampler);
    Q_ASSERT(m_bindings);
    Q_ASSERT(m_pipeline);
    Q_ASSERT(parameters.videoSize[0] >= 0.0f);
    Q_ASSERT(parameters.videoSize[1] >= 0.0f);
    Q_ASSERT(m_videoLayerAvailable || (parameters.videoSize[0] == 0.0f && parameters.videoSize[1] == 0.0f));

    QRhiResourceUpdateBatch* updates = m_rhi.nextResourceUpdateBatch();
    if (m_emptyLayerUploadPending) {
        QImage transparent(1, 1, QImage::Format_RGBA8888);
        transparent.fill(Qt::transparent);
        updates->uploadTexture(m_emptyLayerTexture.get(), transparent);
        m_emptyLayerUploadPending = false;
    }
    updates->updateDynamicBuffer(m_uniformBuffer.get(), 0, sizeof(parameters), &parameters);

    commandBuffer.beginPass(&renderTarget, Qt::black, {1.0f, 0}, updates);
    commandBuffer.setGraphicsPipeline(m_pipeline.get());
    commandBuffer.setViewport(
        {0.0f, 0.0f, static_cast<float>(pixelSize.width()), static_cast<float>(pixelSize.height())});
    commandBuffer.setShaderResources(m_bindings.get());
    commandBuffer.draw(3);
    commandBuffer.endPass();
}

HdrCompositor::ResourceResult HdrCompositor::createBindings(QRhiTexture* videoTexture, QRhiTexture* subtitleTexture,
                                                            QRhiTexture& uiTexture) {
    Q_ASSERT(m_uniformBuffer);
    Q_ASSERT(m_sampler);
    Q_ASSERT(m_emptyLayerTexture);

    QRhiTexture* const compositionVideoTexture = videoTexture ? videoTexture : m_emptyLayerTexture.get();
    QRhiTexture* const compositionSubtitleTexture = subtitleTexture ? subtitleTexture : m_emptyLayerTexture.get();

    if (!m_bindings) {
        m_bindings.reset(m_rhi.newShaderResourceBindings());
    }
    m_bindings->setBindings({
        QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, compositionVideoTexture,
                                                  m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  compositionSubtitleTexture, m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, &uiTexture,
                                                  m_sampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::FragmentStage, m_uniformBuffer.get()),
    });
    if (!m_bindings->create()) {
        if (m_rhi.isDeviceLost()) {
            return ResourceResult::DeviceLost;
        }
        qCFatal(sunplayerLogPresentation, "Could not create the HDR compositor resource bindings");
    }
    m_videoLayerAvailable = videoTexture != nullptr;
    return ResourceResult::Ready;
}
