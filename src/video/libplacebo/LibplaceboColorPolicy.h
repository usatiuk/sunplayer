#pragma once

#include <cstdint>
#include <optional>

#include <QString>
#include <libplacebo/renderer.h>

#include "media/DecodedVideoFrame.h"
#include "video/RenderedVideoSurface.h"

enum class LibplaceboToneMappingFunction {
    Clip,
    Spline,
    Bt2446a,
    St2094_40,
};

enum class LibplaceboSourceMetadataProvenance {
    None,
    ExistingSelection,
    Hdr10PlusOotf,
    Hdr10PlusScene,
    DolbyVisionLevel1,
    DolbyVisionSourceRange,
    MaxCll,
    MasteringDisplay,
    PqCompatibilityFallback,
};

struct LibplaceboColorPolicyDecision {
    LibplaceboToneMappingFunction toneMapping = LibplaceboToneMappingFunction::Spline;
    enum pl_hdr_metadata_type metadata = PL_HDR_METADATA_ANY;
    LibplaceboSourceMetadataProvenance provenance = LibplaceboSourceMetadataProvenance::ExistingSelection;
    std::optional<float> effectiveSourceMaximumNits;
    std::optional<float> selectedSourceAverageNits;
    bool useAbsoluteTargetLuminance = false;
    QString qualification;

    QString description() const;
};

// Stateful only for one representation choice: a proven dual Dolby Vision
// Profile 8.1/HDR10+ source stays on its HDR10-compatible base representation
// for the whole playback generation while targeting SDR/WCG. Everything else
// is resolved from the current frame and normalized target description.
class LibplaceboColorPolicy final {
  public:
    static constexpr float pqCompatibilityMaximumNits = 1000.0f;

    bool shouldMapDolbyVision(DecodedVideoFrame const& frame, RenderedVideoSurfaceDescription const& targetDescription);
    LibplaceboColorPolicyDecision resolve(DecodedVideoFrame const& frame, pl_frame const& mappedFrame,
                                          RenderedVideoSurfaceDescription const& targetDescription) const;

  private:
    std::optional<std::uint64_t> m_playbackGeneration;
    std::optional<bool> m_useHdr10BaseForSdr;
};
