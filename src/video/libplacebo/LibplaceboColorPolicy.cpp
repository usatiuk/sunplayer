#include "video/libplacebo/LibplaceboColorPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixfmt.h>
}

namespace {
constexpr float maximumPqNits = 10000.0f;

template <typename Payload> Payload const* sideDataPayload(AVFrame const& frame, enum AVFrameSideDataType type) {
    AVFrameSideData const* const sideData = av_frame_get_side_data(&frame, type);
    return sideData && sideData->size >= sizeof(Payload) ? reinterpret_cast<Payload const*>(sideData->data) : nullptr;
}

bool finiteInRange(double value, double minimum, double maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool validMaximum(float value) { return finiteInRange(value, std::numeric_limits<float>::min(), maximumPqNits); }

bool sdrLikeTarget(RenderedVideoSurfaceDescription const& description) {
    return description.renderingMode == VideoRenderingMode::SdrCompatibility;
}

bool validMappedOotf(pl_hdr_bezier const& ootf, std::uint8_t maximumAnchors) {
    if (!validMaximum(ootf.target_luma) || !finiteInRange(ootf.knee_x, 0.0, 1.0) ||
        !finiteInRange(ootf.knee_y, 0.0, 1.0) || ootf.num_anchors == 0 || ootf.num_anchors > maximumAnchors) {
        return false;
    }
    for (std::uint8_t index = 0; index < ootf.num_anchors; ++index) {
        if (!finiteInRange(ootf.anchors[index], 0.0, 1.0)) {
            return false;
        }
    }
    return true;
}

struct Hdr10PlusEvidence {
    bool present = false;
    bool valid = false;
    bool sourceOotfPresent = false;
    bool pinnedOotfRepresentable = false;
    bool zeroAnchorOotf = false;
    bool multipleWindows = false;
    bool invalidVersionOneWindows = false;
};

Hdr10PlusEvidence hdr10PlusEvidence(DecodedVideoFrame const& frame, pl_frame const& mappedFrame) {
    Hdr10PlusEvidence result;
    AVDynamicHDRPlus const* const metadata =
        sideDataPayload<AVDynamicHDRPlus>(frame.ffmpegFrame(), AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
    result.present = metadata;
    if (!metadata) {
        return result;
    }

    result.multipleWindows = metadata->num_windows > 1;
    result.invalidVersionOneWindows = metadata->application_version == 1 && metadata->num_windows != 1;
    if (!frame.hasHdr10PlusMetadata()) {
        return result;
    }

    AVHDRPlusColorTransformParams const& global = metadata->params[0];
    result.valid = true;
    result.sourceOotfPresent = global.tone_mapping_flag == 1;
    result.zeroAnchorOotf = result.sourceOotfPresent && global.num_bezier_curve_anchors == 0;
    result.pinnedOotfRepresentable =
        !result.multipleWindows && result.sourceOotfPresent &&
        validMappedOotf(mappedFrame.color.hdr.ootf, metadata->application_version == 0 ? 15 : 9);
    return result;
}

void appendQualification(QString& destination, QString const& addition) {
    if (addition.isEmpty()) {
        return;
    }
    if (!destination.isEmpty()) {
        destination += QStringLiteral("; ");
    }
    destination += addition;
}

QString hdr10PlusLimitation(Hdr10PlusEvidence const& evidence) {
    if (!evidence.present) {
        return {};
    }
    if (evidence.invalidVersionOneWindows) {
        return QStringLiteral("HDR10+ application-version 1 local windows invalid");
    }
    if (!evidence.valid) {
        return QStringLiteral("HDR10+ metadata invalid");
    }
    if (evidence.multipleWindows) {
        return QStringLiteral("local-window HDR10+ metadata unsupported by pinned libplacebo");
    }
    if (evidence.zeroAnchorOotf) {
        return QStringLiteral("zero-anchor OOTF unsupported by pinned libplacebo");
    }
    if (evidence.sourceOotfPresent && !evidence.pinnedOotfRepresentable) {
        return QStringLiteral("source OOTF unavailable to pinned libplacebo");
    }
    return {};
}

std::optional<float> contentMaximumNits(AVFrame const& frame) {
    AVContentLightMetadata const* const metadata =
        sideDataPayload<AVContentLightMetadata>(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (!metadata || metadata->MaxCLL == 0 || metadata->MaxCLL > static_cast<unsigned int>(maximumPqNits)) {
        return std::nullopt;
    }
    return static_cast<float>(metadata->MaxCLL);
}

std::optional<float> masteringMaximumNits(AVFrame const& frame) {
    AVMasteringDisplayMetadata const* const metadata =
        sideDataPayload<AVMasteringDisplayMetadata>(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (!metadata || !metadata->has_luminance) {
        return std::nullopt;
    }

    double const minimum = av_q2d(metadata->min_luminance);
    double const maximum = av_q2d(metadata->max_luminance);
    if (!finiteInRange(minimum, 0.0, maximumPqNits) ||
        !finiteInRange(maximum, std::numeric_limits<double>::min(), maximumPqNits) || minimum > maximum) {
        return std::nullopt;
    }
    return static_cast<float>(maximum);
}

std::optional<float> hdr10PlusMaximumNits(pl_hdr_metadata const& metadata) {
    for (float const component : metadata.scene_max) {
        if (!finiteInRange(component, 0.0, maximumPqNits)) {
            return std::nullopt;
        }
    }
    float const maximum = std::max({metadata.scene_max[0], metadata.scene_max[1], metadata.scene_max[2]});
    return validMaximum(maximum) ? std::optional<float>(maximum) : std::nullopt;
}

std::optional<float> sourceAverageNits(pl_hdr_metadata const& metadata, enum pl_hdr_metadata_type type) {
    float average = 0.0f;
    switch (type) {
    case PL_HDR_METADATA_HDR10PLUS:
        average = metadata.scene_avg;
        break;
    case PL_HDR_METADATA_CIE_Y:
        average = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, metadata.avg_pq_y);
        break;
    default:
        return std::nullopt;
    }
    return validMaximum(average) ? std::optional<float>(average) : std::nullopt;
}

QString formattedNits(float value) { return QString::number(value, 'f', value < 10.0f ? 2 : 0); }

QString provenanceDescription(LibplaceboColorPolicyDecision const& decision) {
    switch (decision.provenance) {
    case LibplaceboSourceMetadataProvenance::None:
        return QStringLiteral("relative SDR signal");
    case LibplaceboSourceMetadataProvenance::ExistingSelection:
        return QStringLiteral("existing metadata selection");
    case LibplaceboSourceMetadataProvenance::Hdr10PlusOotf:
        return QStringLiteral("HDR10+ source OOTF");
    case LibplaceboSourceMetadataProvenance::Hdr10PlusScene:
        return QStringLiteral("HDR10+ scene maximum");
    case LibplaceboSourceMetadataProvenance::DolbyVisionLevel1:
        return QStringLiteral("Dolby Vision L1 maximum");
    case LibplaceboSourceMetadataProvenance::DolbyVisionSourceRange:
        return QStringLiteral("Dolby Vision source range");
    case LibplaceboSourceMetadataProvenance::MaxCll:
        return QStringLiteral("MaxCLL %1 nits").arg(formattedNits(*decision.effectiveSourceMaximumNits));
    case LibplaceboSourceMetadataProvenance::MasteringDisplay:
        return QStringLiteral("mastering maximum %1 nits").arg(formattedNits(*decision.effectiveSourceMaximumNits));
    case LibplaceboSourceMetadataProvenance::PqCompatibilityFallback:
        return QStringLiteral("explicit PQ compatibility fallback %1 nits")
            .arg(formattedNits(*decision.effectiveSourceMaximumNits));
    }
    return {};
}

LibplaceboColorPolicyDecision genericDecision(LibplaceboToneMappingFunction toneMapping,
                                              LibplaceboSourceMetadataProvenance provenance,
                                              QString qualification = {}) {
    return {
        .toneMapping = toneMapping,
        .metadata = toneMapping == LibplaceboToneMappingFunction::Clip ? PL_HDR_METADATA_NONE : PL_HDR_METADATA_ANY,
        .provenance = provenance,
        .qualification = std::move(qualification),
    };
}

LibplaceboColorPolicyDecision pqCompatibilityFallback(LibplaceboToneMappingFunction toneMapping,
                                                      QString qualification = {}) {
    return {
        .toneMapping = toneMapping,
        .metadata = PL_HDR_METADATA_HDR10,
        .provenance = LibplaceboSourceMetadataProvenance::PqCompatibilityFallback,
        .effectiveSourceMaximumNits = LibplaceboColorPolicy::pqCompatibilityMaximumNits,
        .qualification = std::move(qualification),
    };
}

LibplaceboColorPolicyDecision hdr10PlusOotfDecision(pl_frame const& mappedFrame) {
    return {
        .toneMapping = LibplaceboToneMappingFunction::St2094_40,
        .metadata = PL_HDR_METADATA_HDR10PLUS,
        .provenance = LibplaceboSourceMetadataProvenance::Hdr10PlusOotf,
        .selectedSourceAverageNits = sourceAverageNits(mappedFrame.color.hdr, PL_HDR_METADATA_HDR10PLUS),
    };
}
} // namespace

QString LibplaceboColorPolicyDecision::description() const {
    if (toneMapping == LibplaceboToneMappingFunction::Spline &&
        provenance == LibplaceboSourceMetadataProvenance::ExistingSelection && qualification.isEmpty()) {
        return QStringLiteral("Spline tone map · perceptual gamut map · inverse mapping off · "
                              "peak detection off · dither off");
    }
    if (toneMapping == LibplaceboToneMappingFunction::Clip && provenance == LibplaceboSourceMetadataProvenance::None &&
        qualification.isEmpty()) {
        return QStringLiteral("Clip tone map · perceptual gamut map · inverse mapping off · "
                              "peak detection off · dither off");
    }

    QString toneMappingName;
    switch (toneMapping) {
    case LibplaceboToneMappingFunction::Clip:
        toneMappingName = QStringLiteral("Clip tone map");
        break;
    case LibplaceboToneMappingFunction::Spline:
        toneMappingName = QStringLiteral("Spline tone map");
        break;
    case LibplaceboToneMappingFunction::Bt2446a:
        toneMappingName = QStringLiteral("BT.2446A EETF");
        break;
    case LibplaceboToneMappingFunction::St2094_40:
        toneMappingName = QStringLiteral("ST 2094-40 EETF");
        break;
    }

    QString result = QStringLiteral("%1 · %2").arg(toneMappingName, provenanceDescription(*this));
    if (selectedSourceAverageNits) {
        result += QStringLiteral(" · selected average %1 nits").arg(formattedNits(*selectedSourceAverageNits));
    }
    if (!qualification.isEmpty()) {
        result += QStringLiteral(" · %1").arg(qualification);
    }
    result += QStringLiteral(" · perceptual gamut map · inverse mapping off · peak detection off · dither off");
    return result;
}

bool LibplaceboColorPolicy::shouldMapDolbyVision(DecodedVideoFrame const& frame, bool preferHdr10Plus) {
    std::uint64_t const generation = frame.identity().playbackGeneration;
    if (m_playbackGeneration != generation || m_preferHdr10Plus != preferHdr10Plus) {
        m_playbackGeneration = generation;
        m_preferHdr10Plus = preferHdr10Plus;
        m_useHdr10Base = false;
    }
    if (!preferHdr10Plus || !frame.dolbyVisionBaseIsHdr10Compatible().value_or(false)) {
        return true;
    }
    if (frame.dynamicRange() == VideoDynamicRange::DolbyVision && frame.hasHdr10PlusMetadata()) {
        m_useHdr10Base = true;
    }
    return !m_useHdr10Base;
}

LibplaceboColorPolicyDecision
LibplaceboColorPolicy::resolve(DecodedVideoFrame const& frame, pl_frame const& mappedFrame,
                               RenderedVideoSurfaceDescription const& targetDescription) const {
    Q_ASSERT(targetDescription.isValid());
    AVFrame const& source = frame.ffmpegFrame();
    bool const mappedDolbyVision = mappedFrame.repr.sys == PL_COLOR_SYSTEM_DOLBYVISION && mappedFrame.repr.dovi;
    if (frame.dynamicRange() == VideoDynamicRange::Sdr) {
        return genericDecision(LibplaceboToneMappingFunction::Clip, LibplaceboSourceMetadataProvenance::None);
    }
    if (!mappedDolbyVision && source.color_trc != AVCOL_TRC_SMPTE2084) {
        return genericDecision(LibplaceboToneMappingFunction::Spline,
                               LibplaceboSourceMetadataProvenance::ExistingSelection);
    }

    bool const sdrTarget = sdrLikeTarget(targetDescription);
    LibplaceboToneMappingFunction const toneMapping =
        sdrTarget ? LibplaceboToneMappingFunction::Bt2446a : LibplaceboToneMappingFunction::Spline;
    Hdr10PlusEvidence const dynamic = hdr10PlusEvidence(frame, mappedFrame);
    std::optional<float> const dynamicMaximum = hdr10PlusMaximumNits(mappedFrame.color.hdr);

    if (mappedDolbyVision) {
        QString ignoredBaseGuidance = hdr10PlusLimitation(dynamic);
        if (dynamic.valid) {
            appendQualification(ignoredBaseGuidance,
                                QStringLiteral("concurrent HDR10+ guidance ignored for mapped Dolby representation"));
        }

        std::optional<float> const level1Maximum = [&mappedFrame]() -> std::optional<float> {
            if (!pl_hdr_metadata_contains(&mappedFrame.color.hdr, PL_HDR_METADATA_CIE_Y)) {
                return std::nullopt;
            }
            float const maximum = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, mappedFrame.color.hdr.max_pq_y);
            std::optional<float> const average = sourceAverageNits(mappedFrame.color.hdr, PL_HDR_METADATA_CIE_Y);
            return validMaximum(maximum) && average && *average <= maximum ? std::optional<float>(maximum)
                                                                           : std::nullopt;
        }();
        if (level1Maximum) {
            return {
                .toneMapping = toneMapping,
                .metadata = PL_HDR_METADATA_CIE_Y,
                .provenance = LibplaceboSourceMetadataProvenance::DolbyVisionLevel1,
                .selectedSourceAverageNits = sourceAverageNits(mappedFrame.color.hdr, PL_HDR_METADATA_CIE_Y),
                .qualification = ignoredBaseGuidance,
            };
        }

        float const minimum = mappedFrame.color.hdr.min_luma;
        float const maximum = mappedFrame.color.hdr.max_luma;
        if (finiteInRange(minimum, 0.0, maximumPqNits) && validMaximum(maximum) && minimum <= maximum) {
            return {
                .toneMapping = toneMapping,
                .metadata = PL_HDR_METADATA_HDR10,
                .provenance = LibplaceboSourceMetadataProvenance::DolbyVisionSourceRange,
                .effectiveSourceMaximumNits = maximum,
                .qualification = ignoredBaseGuidance,
            };
        }
        QString qualification = QStringLiteral("mapped Dolby source range unavailable");
        if (!ignoredBaseGuidance.isEmpty()) {
            qualification += QStringLiteral("; %1").arg(ignoredBaseGuidance);
        }
        return pqCompatibilityFallback(toneMapping, qualification);
    }

    std::optional<float> const maxCll = contentMaximumNits(source);
    std::optional<float> const masteringMaximum = masteringMaximumNits(source);
    QString metadataQualification = hdr10PlusLimitation(dynamic);
    if (dynamic.pinnedOotfRepresentable) {
        return hdr10PlusOotfDecision(mappedFrame);
    }
    if (dynamic.valid && dynamicMaximum &&
        pl_hdr_metadata_contains(&mappedFrame.color.hdr, PL_HDR_METADATA_HDR10PLUS) &&
        validMaximum(mappedFrame.color.hdr.scene_avg) && mappedFrame.color.hdr.scene_avg <= *dynamicMaximum) {
        QString qualification = metadataQualification;
        if (!qualification.isEmpty()) {
            appendQualification(qualification, QStringLiteral("global scene values used"));
        }
        return {
            .toneMapping = toneMapping,
            .metadata = PL_HDR_METADATA_HDR10PLUS,
            .provenance = LibplaceboSourceMetadataProvenance::Hdr10PlusScene,
            .selectedSourceAverageNits = sourceAverageNits(mappedFrame.color.hdr, PL_HDR_METADATA_HDR10PLUS),
            .qualification = qualification,
        };
    }

    QString staticQualification = metadataQualification;
    if (dynamic.present) {
        if (staticQualification.isEmpty()) {
            staticQualification = QStringLiteral("HDR10+ scene values unavailable after import");
        }
        appendQualification(staticQualification, QStringLiteral("static metadata used"));
    }
    if (maxCll) {
        if (masteringMaximum && std::abs(*maxCll - *masteringMaximum) > 0.5f) {
            appendQualification(
                staticQualification,
                QStringLiteral("mastering maximum %1 nits retained").arg(formattedNits(*masteringMaximum)));
        }
        return {
            .toneMapping = toneMapping,
            .metadata = PL_HDR_METADATA_HDR10,
            .provenance = LibplaceboSourceMetadataProvenance::MaxCll,
            .effectiveSourceMaximumNits = maxCll,
            .qualification = staticQualification,
        };
    }
    if (masteringMaximum) {
        return {
            .toneMapping = toneMapping,
            .metadata = PL_HDR_METADATA_HDR10,
            .provenance = LibplaceboSourceMetadataProvenance::MasteringDisplay,
            .effectiveSourceMaximumNits = masteringMaximum,
            .qualification = staticQualification,
        };
    }

    QString fallbackQualification = metadataQualification;
    if (dynamic.present) {
        if (fallbackQualification.isEmpty()) {
            fallbackQualification = QStringLiteral("HDR10+ scene values unavailable after import");
        }
        appendQualification(fallbackQualification, QStringLiteral("PQ fallback used"));
    }
    return pqCompatibilityFallback(toneMapping, fallbackQualification);
}
