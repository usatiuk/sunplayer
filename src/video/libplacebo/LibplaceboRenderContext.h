#pragma once

#include <QString>
#include <libplacebo/renderer.h>

#include "video/RenderedVideoSurface.h"
#include "video/libplacebo/LibplaceboColorPolicy.h"

struct LibplaceboGraphicsContext;

// Shared libplacebo renderer policy for analytic and decoded inputs. It owns
// target color description and tone-mapping selection. Relative-transfer paths
// describe the destination in libplacebo's fixed 203-nit coordinate system.
// Absolute PQ/Dolby paths use an authoritative physical target peak and
// normalize the result into the same active-reference-white surface contract.
// Headroom-only targets retain the relative path. Target allocation and
// synchronization remain in VideoTargetInterop.
class LibplaceboRenderContext final {
  public:
    explicit LibplaceboRenderContext(LibplaceboGraphicsContext const& graphics);
    ~LibplaceboRenderContext();

    LibplaceboRenderContext(LibplaceboRenderContext const&) = delete;
    LibplaceboRenderContext& operator=(LibplaceboRenderContext const&) = delete;

    static QString policyDescription(bool toneMappingEnabled);

    bool isValid() const;
    bool render(pl_frame const& source, pl_tex targetTexture, RenderedVideoSurfaceDescription const& targetDescription,
                bool toneMappingEnabled, QString* error = nullptr);
    bool renderDecoded(pl_frame const& source, pl_tex targetTexture,
                       RenderedVideoSurfaceDescription const& targetDescription,
                       LibplaceboColorPolicyDecision const& colorPolicy, QString* error = nullptr);

  private:
    bool renderWithPolicy(pl_frame const& source, pl_tex targetTexture,
                          RenderedVideoSurfaceDescription const& targetDescription,
                          LibplaceboToneMappingFunction toneMapping, enum pl_hdr_metadata_type metadata,
                          std::optional<float> effectiveSourceMaximumNits, bool useAbsoluteTargetLuminance,
                          QString* error);

    pl_renderer m_renderer = nullptr;
};
