#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <type_traits>

#include <QSize>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

struct alignas(16) HdrCompositorParameters {
    std::array<float, 2> viewportSize{};
    std::array<float, 2> videoOrigin{};
    std::array<float, 2> videoSize{};
    // Maps SDR white into a scene-referred extended-linear target.
    float sdrScale = 1.0f;
    float ndcYUp = 1.0f;
    float linearOutput = 1.0f;
    std::array<float, 3> padding{};
};

static_assert(std::is_standard_layout_v<HdrCompositorParameters>);
static_assert(sizeof(HdrCompositorParameters) == 48);
static_assert(offsetof(HdrCompositorParameters, sdrScale) == 24);
static_assert(offsetof(HdrCompositorParameters, ndcYUp) == 28);
static_assert(offsetof(HdrCompositorParameters, linearOutput) == 32);

// Final presentation pass; source color processing belongs to video producers.
class HdrCompositor final {
public:
    enum class ResourceResult {
        Ready,
        DeviceLost,
    };

    explicit HdrCompositor(QRhi &rhi);
    ~HdrCompositor();

    HdrCompositor(const HdrCompositor &) = delete;
    HdrCompositor &operator=(const HdrCompositor &) = delete;

    ResourceResult initialize(QRhiRenderPassDescriptor &renderPassDescriptor,
                              QRhiTexture *videoTexture,
                              QRhiTexture &uiTexture);
    ResourceResult setTextures(QRhiTexture *videoTexture,
                               QRhiTexture &uiTexture);
    void render(QRhiCommandBuffer &commandBuffer,
                QRhiRenderTarget &renderTarget,
                const QSize &pixelSize,
                const HdrCompositorParameters &parameters);

private:
    ResourceResult createBindings(QRhiTexture *videoTexture,
                                  QRhiTexture &uiTexture);

    QRhi &m_rhi;
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_emptyVideoTexture;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    bool m_videoLayerAvailable = false;
};
