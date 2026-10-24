#pragma once

#include <QString>
#include <libplacebo/renderer.h>

#include "video/RenderedVideoSurface.h"
#include "video/libplacebo/LibplaceboColorPolicy.h"

struct LibplaceboGraphicsContext;

struct LibplaceboTargetLuminance {
    float coordinateWhiteNits = 0.0f;
    float maximumNits = 0.0f;
    float minimumNits = 0.0f;

    float workingPeak() const { return (maximumNits - minimumNits) / (coordinateWhiteNits - minimumNits); }
};

// The source color must already be inferred. PQ/Dolby uses the selected source
// reference white as its virtual target coordinate, or physical display white
// when Absolute PQ is requested and supported by the HDR target.
// Relative sources retain libplacebo's 203-nit coordinate.
LibplaceboTargetLuminance calculateLibplaceboTargetLuminance(pl_frame const& source, VideoColorTarget const& target,
                                                             float sourceHdrReferenceWhiteNits,
                                                             bool absolutePqEnabled = false);

// Translates the surface's known/value pair into libplacebo target metadata.
// Numeric zero means unknown to libplacebo; PL_COLOR_HDR_BLACK means known
// effectively-zero black. The library infers an unknown minimum once when the
// linear target is constructed, independently of the tone operator.
float calculateLibplaceboTargetMinimumNits(VideoColorTarget const& target, float targetMaximumNits);

// Conservative composition bound in the same normalized units as the renderer.
float libplaceboWorkingPeak(VideoColorTarget const& target);

// Shared libplacebo renderer boundary for analytic and decoded inputs. It owns
// target color construction and applies the caller's tone-mapping selection.
// Target allocation and synchronization remain in VideoTargetInterop.
class LibplaceboRenderContext final {
  public:
    explicit LibplaceboRenderContext(LibplaceboGraphicsContext const& graphics);
    ~LibplaceboRenderContext();

    LibplaceboRenderContext(LibplaceboRenderContext const&) = delete;
    LibplaceboRenderContext& operator=(LibplaceboRenderContext const&) = delete;

    bool isValid() const;
    void resetTemporalState();
    std::optional<pl_hdr_metadata> detectedHdrMetadata() const;
    bool render(pl_frame const& source, pl_tex targetTexture, RenderedVideoSurfaceDescription const& targetDescription,
                bool toneMappingEnabled, QString* error = nullptr);
    bool renderDecoded(pl_frame const& source, pl_tex targetTexture,
                       RenderedVideoSurfaceDescription const& targetDescription,
                       LibplaceboColorPolicyDecision const& colorPolicy, float sourceHdrReferenceWhiteNits,
                       bool absolutePqEnabled, QString* error = nullptr);

  private:
    bool renderWithPolicy(pl_frame const& source, pl_tex targetTexture,
                          RenderedVideoSurfaceDescription const& targetDescription,
                          LibplaceboToneMappingFunction toneMapping, enum pl_hdr_metadata_type metadata,
                          std::optional<float> effectiveSourceMaximumNits, float sourceHdrReferenceWhiteNits,
                          bool absolutePqEnabled, bool peakDetectionEnabled, QString* error);

    pl_renderer m_renderer = nullptr;
};
