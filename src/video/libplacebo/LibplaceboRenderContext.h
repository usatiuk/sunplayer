#pragma once

#include <QString>
#include <libplacebo/renderer.h>

#include "video/RenderedVideoSurface.h"

struct LibplaceboGraphicsContext;

// Shared libplacebo renderer policy for analytic and decoded inputs. It owns
// target color description and tone-mapping selection. Sunroom describes the
// destination in libplacebo's fixed 203-nit coordinate system so its linear
// output directly satisfies the active-reference-white surface contract.
// Target allocation and synchronization remain in VideoTargetInterop.
class LibplaceboRenderContext final {
public:
    explicit LibplaceboRenderContext(
        const LibplaceboGraphicsContext &graphics);
    ~LibplaceboRenderContext();

    LibplaceboRenderContext(
        const LibplaceboRenderContext &) = delete;
    LibplaceboRenderContext &operator=(
        const LibplaceboRenderContext &) = delete;

    static QString policyDescription(
        bool toneMappingEnabled);

    bool isValid() const;
    bool render(
        const pl_frame &source,
        pl_tex targetTexture,
        const RenderedVideoSurfaceDescription &targetDescription,
        bool toneMappingEnabled,
        QString *error = nullptr);

private:
    pl_renderer m_renderer = nullptr;
};
