#pragma once

#include <QString>
#include <libplacebo/renderer.h>

#include "video/RenderedVideoSurface.h"

struct LibplaceboGraphicsContext;

// Shared libplacebo renderer policy for analytic and decoded inputs. It owns
// target color description, tone-mapping selection, and normalization from
// libplacebo's 203-nit linear convention into Sunroom's active-reference-white
// surface convention. Target allocation and synchronization remain in
// VideoTargetInterop.
class LibplaceboRenderContext final {
public:
    explicit LibplaceboRenderContext(
        const LibplaceboGraphicsContext &graphics);
    ~LibplaceboRenderContext();

    LibplaceboRenderContext(
        const LibplaceboRenderContext &) = delete;
    LibplaceboRenderContext &operator=(
        const LibplaceboRenderContext &) = delete;

    bool isValid() const;
    bool render(
        const pl_frame &source,
        pl_tex targetTexture,
        const RenderedVideoSurfaceDescription &targetDescription,
        bool toneMappingEnabled,
        QString *error = nullptr);

private:
    static pl_hook_res applyReferenceWhiteScale(
        void *privateData,
        const pl_hook_params *parameters);

    pl_renderer m_renderer = nullptr;
    float m_referenceWhiteScale = 1.0f;
    QSize m_targetSize;
    pl_shader_var m_referenceWhiteScaleVariable{};
    pl_hook m_referenceWhiteHook{};
};
