#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>

#include <QSize>

#include "presentation/RenderedVideoSurface.h"

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

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

class DiagnosticVideoProducer final {
public:
    enum class ResourceResult {
        Ready,
        DeviceLost,
    };

    explicit DiagnosticVideoProducer(QRhi &rhi);
    ~DiagnosticVideoProducer();

    DiagnosticVideoProducer(const DiagnosticVideoProducer &) = delete;
    DiagnosticVideoProducer &operator=(const DiagnosticVideoProducer &) =
        delete;

    ResourceResult ensureSurface(
        const RenderedVideoSurfaceState &requestedState);
    bool needsRender(const RenderedVideoSurfaceState &requestedState) const;
    void render(QRhiCommandBuffer &commandBuffer,
                const DiagnosticVideoParameters &parameters,
                const RenderedVideoSurfaceState &completedState);

    QRhiTexture &texture() const;
    const std::optional<RenderedVideoSurfaceState> &completedState() const;

private:
    ResourceResult createResources(const QSize &pixelSize);
    ResourceResult resizeTexture(const QSize &pixelSize);

    QRhi &m_rhi;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiTextureRenderTarget> m_renderTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPassDescriptor;
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::optional<RenderedVideoSurfaceState> m_completedState;
};
