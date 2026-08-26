#pragma once

#include <QString>
#include <libplacebo/renderer.h>

#include "video/RenderedVideoSurface.h"
#include "video/libplacebo/LibplaceboColorPolicy.h"

struct LibplaceboGraphicsContext;

struct LibplaceboTargetLuminance {
    float coordinateWhiteNits = 0.0f;
    float maximumNits = 0.0f;
    float outputNormalizationScale = 1.0f;
};

// The source color must already be inferred. Absolute-luminance PQ/Dolby uses
// a fixed nominal 100-nit destination only when no HDR headroom is requested.
// Every other destination stays in libplacebo's 203-nit relative coordinate.
LibplaceboTargetLuminance calculateLibplaceboTargetLuminance(pl_frame const& source, float targetPeakHeadroom);

// Translates the surface's known/value pair into libplacebo target metadata.
// Numeric zero means unknown to libplacebo; PL_COLOR_HDR_BLACK means known
// effectively-zero black, and conservatively preserves legacy behavior for an
// unknown extended-linear HDR/EDR target (ADR 0026).
float calculateLibplaceboTargetMinimumNits(RenderedVideoSurfaceDescription const& description, float targetMaximumNits);

// Shared libplacebo renderer boundary for analytic and decoded inputs. It owns
// target color construction and applies the caller's tone-mapping selection.
// Target allocation and synchronization remain in VideoTargetInterop.
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
                          std::optional<float> effectiveSourceMaximumNits, QString* error);

    pl_renderer m_renderer = nullptr;
};
