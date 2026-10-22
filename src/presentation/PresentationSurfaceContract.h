#pragma once

#include <algorithm>
#include <cstdint>

#include "platform/ColorPrimaries.h"

class QWindow;
struct DisplayState;

enum class PresentationOutputEncoding {
    Srgb = 0,
    Gamma22 = 1,
    LinearSrgb = 2,
    Bt2020Pq = 3,
};

// Pixel interpretation, independent of native declaration APIs and video mapping.
struct PresentationSurfaceContract {
    static constexpr float pqReferenceWhiteNits = 203.0f;
    static constexpr float pqPeakNits = 10'000.0f;
    static constexpr float pqMaximumHeadroom = pqPeakNits / pqReferenceWhiteNits;

    bool operator==(PresentationSurfaceContract const&) const = default;
    PresentationOutputEncoding encoding = PresentationOutputEncoding::Srgb;
    // Only Gamma22 has a variable basis. All other encodings define theirs.
    ColorPrimaries sdrPrimaries = srgbColorPrimaries;

    ColorPrimaries primaries() const {
        return encoding == PresentationOutputEncoding::Bt2020Pq  ? bt2020ColorPrimaries
               : encoding == PresentationOutputEncoding::Gamma22 ? sdrPrimaries
                                                                 : srgbColorPrimaries;
    }
    bool hdr10Required() const { return encoding == PresentationOutputEncoding::Bt2020Pq; }
    float constrainTargetHeadroom(float headroom) const {
        return encoding == PresentationOutputEncoding::LinearSrgb ? headroom
               : hdr10Required()                                  ? std::min(headroom, pqMaximumHeadroom)
                                                                  : 1.0f;
    }
};

// Optional native declaration owner. Qt owns the ordinary Windows/macOS path;
// the presentation engine always owns rendering resources and reconciliation.
class PresentationSurfaceController {
  public:
    enum class Preparation { Ready, Pending, Rejected };
    virtual ~PresentationSurfaceController() = default;

    virtual PresentationSurfaceContract desiredContract(DisplayState const& display,
                                                        std::uint64_t graphicsDeviceGeneration) = 0;
    // Pending completion requests a window update. A ready declaration is
    // applied with matching pixels without dispatching native events in between.
    virtual Preparation prepareFrame(QWindow& window, PresentationSurfaceContract const& contract, float headroom,
                                     ColorPrimaries const& videoPrimaries) = 0;
    virtual void applyPrepared(QWindow& window) = 0;
    virtual void rejectTarget(std::uint64_t graphicsDeviceGeneration, PresentationSurfaceContract const& contract,
                              char const* reason) = 0;
};
