#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>

#include <QtTest>
#include <libplacebo/tone_mapping.h>

extern "C" {
#include <libavutil/dovi_meta.h>
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
}

#include "media/DecodedVideoFrame.h"
#include "video/libplacebo/LibplaceboColorPolicy.h"

namespace {
struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

RenderedVideoSurfaceDescription target(float headroom, bool targetPeakLuminanceKnown = true) {
    return {
        .pixelSize = {2, 2},
        .pixelFormat = RenderedVideoPixelFormat::Rgba16Float,
        .colorSpace = RenderedVideoColorSpace::LinearSrgb,
        .luminance = RenderedVideoLuminance::DisplayTargetedSdrWhiteRelative,
        .alphaMode = RenderedVideoAlphaMode::Opaque,
        .referenceWhiteNits = 203.0f,
        .targetMinimumLuminanceKnown = true,
        .targetMinimumLuminanceNits = 0.0f,
        .targetPeakLuminanceKnown = targetPeakLuminanceKnown,
        .targetPeakHeadroom = headroom,
    };
}

std::shared_ptr<DecodedVideoFrame const> makeFrame(std::uint64_t generation, std::uint64_t frameId,
                                                   enum AVColorTransferCharacteristic transfer,
                                                   std::optional<bool> hdr10CompatibleDolbyBase = std::nullopt,
                                                   std::function<void(AVFrame&)> const& configure = {}) {
    FramePtr source(av_frame_alloc());
    if (!source) {
        return {};
    }
    source->format = AV_PIX_FMT_RGB24;
    source->width = 2;
    source->height = 2;
    source->color_primaries = transfer == AVCOL_TRC_BT709 ? AVCOL_PRI_BT709 : AVCOL_PRI_BT2020;
    source->color_trc = transfer;
    source->colorspace = AVCOL_SPC_RGB;
    source->color_range = AVCOL_RANGE_JPEG;
    source->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    if (av_frame_get_buffer(source.get(), 0) < 0) {
        return {};
    }
    if (configure) {
        configure(*source);
    }

    QString error;
    return DecodedVideoFrame::clone(*source,
                                    {
                                        .playbackGeneration = generation,
                                        .decoderRevision = 1,
                                        .frameId = frameId,
                                    },
                                    {1, 24}, std::nullopt, hdr10CompatibleDolbyBase, &error);
}

void addMastering(AVFrame& frame, double minimumNits, double maximumNits) {
    AVFrameSideData* const sideData =
        av_frame_new_side_data(&frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA, sizeof(AVMasteringDisplayMetadata));
    Q_ASSERT(sideData);
    std::memset(sideData->data, 0, sideData->size);
    auto* const metadata = reinterpret_cast<AVMasteringDisplayMetadata*>(sideData->data);
    metadata->has_luminance = 1;
    metadata->min_luminance = av_d2q(minimumNits, 1'000'000);
    metadata->max_luminance = av_d2q(maximumNits, 1'000'000);
}

void addContentLight(AVFrame& frame, unsigned int maximumNits, unsigned int averageNits = 0) {
    AVFrameSideData* const sideData =
        av_frame_new_side_data(&frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL, sizeof(AVContentLightMetadata));
    Q_ASSERT(sideData);
    std::memset(sideData->data, 0, sideData->size);
    auto* const metadata = reinterpret_cast<AVContentLightMetadata*>(sideData->data);
    metadata->MaxCLL = maximumNits;
    metadata->MaxFALL = averageNits;
}

void addHdr10Plus(AVFrame& frame, int windows, bool toneMapping, int anchors, int applicationVersion = 1) {
    AVFrameSideData* const sideData =
        av_frame_new_side_data(&frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS, sizeof(AVDynamicHDRPlus));
    Q_ASSERT(sideData);
    std::memset(sideData->data, 0, sideData->size);
    auto* const metadata = reinterpret_cast<AVDynamicHDRPlus*>(sideData->data);
    metadata->itu_t_t35_country_code = 0xb5;
    metadata->application_version = static_cast<std::uint8_t>(applicationVersion);
    metadata->num_windows = static_cast<std::uint8_t>(windows);
    metadata->targeted_system_display_maximum_luminance = {600, 1};
    metadata->params[0].maxscl[0] = {1, 10};
    metadata->params[0].maxscl[1] = {1, 10};
    metadata->params[0].maxscl[2] = {1, 10};
    metadata->params[0].average_maxrgb = {1, 20};
    metadata->params[0].tone_mapping_flag = toneMapping ? 1 : 0;
    metadata->params[0].knee_point_x = {1, 2};
    metadata->params[0].knee_point_y = {2, 5};
    metadata->params[0].num_bezier_curve_anchors = static_cast<std::uint8_t>(anchors);
    for (int index = 0; index < anchors; ++index) {
        metadata->params[0].bezier_curve_anchors[index] = {3 + index, 5 + index};
    }
}

void addDolbyVision(AVFrame& frame) {
    std::size_t metadataSize = 0;
    AVDOVIMetadata* const metadata = av_dovi_metadata_alloc(&metadataSize);
    Q_ASSERT(metadata);
    AVFrameSideData* const sideData = av_frame_new_side_data(&frame, AV_FRAME_DATA_DOVI_METADATA, metadataSize);
    Q_ASSERT(sideData);
    std::memcpy(sideData->data, metadata, metadataSize);
    av_free(metadata);
}

pl_frame pqMappedFrame(float minimumNits = 0.005f, float maximumNits = 1000.0f) {
    pl_frame frame{};
    frame.color.primaries = PL_COLOR_PRIM_BT_2020;
    frame.color.transfer = PL_COLOR_TRC_PQ;
    frame.color.hdr.min_luma = minimumNits;
    frame.color.hdr.max_luma = maximumNits;
    return frame;
}

void addMappedHdr10Plus(pl_frame& frame, int anchors = 0) {
    frame.color.hdr.scene_max[0] = 600.0f;
    frame.color.hdr.scene_max[1] = 550.0f;
    frame.color.hdr.scene_max[2] = 500.0f;
    frame.color.hdr.scene_avg = 80.0f;
    frame.color.hdr.ootf.target_luma = 600.0f;
    frame.color.hdr.ootf.knee_x = 0.5f;
    frame.color.hdr.ootf.knee_y = 0.4f;
    frame.color.hdr.ootf.num_anchors = static_cast<std::uint8_t>(anchors);
    for (int index = 0; index < anchors; ++index) {
        frame.color.hdr.ootf.anchors[index] =
            0.5f + 0.4f * static_cast<float>(index + 1) / static_cast<float>(anchors + 1);
    }
}

float referenceBt2446a(float inputNits, float inputPeakNits, float outputPeakNits) {
    float const normalizedLinear = std::clamp(inputNits / inputPeakNits, 0.0f, 1.0f);
    float const gammaSignal = std::pow(normalizedLinear, 1.0f / 2.4f);
    float const rhoInput = 1.0f + 32.0f * std::pow(inputPeakNits / 10000.0f, 1.0f / 2.4f);
    float const perceptual = std::log(1.0f + (rhoInput - 1.0f) * gammaSignal) / std::log(rhoInput);
    float compressed = 0.0f;
    if (perceptual <= 0.7399f) {
        compressed = 1.0770f * perceptual;
    } else if (perceptual < 0.9909f) {
        compressed = -1.1510f * perceptual * perceptual + 2.7811f * perceptual - 0.6302f;
    } else {
        compressed = 0.5f * perceptual + 0.5f;
    }
    float const rhoOutput = 1.0f + 32.0f * std::pow(outputPeakNits / 10000.0f, 1.0f / 2.4f);
    float const outputGamma = (std::pow(rhoOutput, compressed) - 1.0f) / (rhoOutput - 1.0f);
    return outputPeakNits * std::pow(outputGamma, 2.4f);
}
} // namespace

class LibplaceboColorPolicyTest final : public QObject {
    Q_OBJECT

  private slots:
    void preservesRelativeSourcesAndBoundsMissingPq();
    void resolvesBaseMetadataInContentSpecificOrder();
    void usesOnlyPinnedRepresentableHdr10PlusOotfs();
    void keepsDolbyMetadataWithinTheMappedRepresentation();
    void keepsDualFormatRepresentationStable();
    void bt2446aMatchesTheIndependentReferenceEetf();
    void st2094_40MatchesAnIndependentBezierVector();
};

void LibplaceboColorPolicyTest::preservesRelativeSourcesAndBoundsMissingPq() {
    LibplaceboColorPolicy policy;
    std::shared_ptr<DecodedVideoFrame const> const sdr = makeFrame(1, 1, AVCOL_TRC_BT709);
    std::shared_ptr<DecodedVideoFrame const> const hlg = makeFrame(2, 1, AVCOL_TRC_ARIB_STD_B67);
    std::shared_ptr<DecodedVideoFrame const> const pq = makeFrame(3, 1, AVCOL_TRC_SMPTE2084);
    QVERIFY(sdr);
    QVERIFY(hlg);
    QVERIFY(pq);
    pl_frame mapped = pqMappedFrame();

    QVERIFY(policy.shouldMapDolbyVision(*sdr, target(1.0f)));
    LibplaceboColorPolicyDecision const sdrDecision = policy.resolve(*sdr, mapped, target(1.0f));
    QCOMPARE(sdrDecision.toneMapping, LibplaceboToneMappingFunction::Clip);
    QCOMPARE(sdrDecision.metadata, PL_HDR_METADATA_NONE);
    QVERIFY(!sdrDecision.useAbsoluteTargetLuminance);

    QVERIFY(policy.shouldMapDolbyVision(*hlg, target(1.0f)));
    LibplaceboColorPolicyDecision const hlgDecision = policy.resolve(*hlg, mapped, target(1.0f));
    QCOMPARE(hlgDecision.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(hlgDecision.metadata, PL_HDR_METADATA_ANY);
    QVERIFY(!hlgDecision.useAbsoluteTargetLuminance);

    QVERIFY(policy.shouldMapDolbyVision(*pq, target(4.0f)));
    LibplaceboColorPolicyDecision const hdrDecision = policy.resolve(*pq, mapped, target(4.0f));
    QCOMPARE(hdrDecision.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(hdrDecision.metadata, PL_HDR_METADATA_HDR10);
    QCOMPARE(hdrDecision.provenance, LibplaceboSourceMetadataProvenance::PqCompatibilityFallback);
    QCOMPARE(hdrDecision.effectiveSourceMaximumNits,
             std::optional<float>(LibplaceboColorPolicy::pqCompatibilityMaximumNits));
    QVERIFY(hdrDecision.useAbsoluteTargetLuminance);
    QVERIFY(hdrDecision.description().contains(QStringLiteral("explicit PQ compatibility fallback 1000 nits")));
}

void LibplaceboColorPolicyTest::resolvesBaseMetadataInContentSpecificOrder() {
    LibplaceboColorPolicy policy;
    std::shared_ptr<DecodedVideoFrame const> const maxCll =
        makeFrame(10, 1, AVCOL_TRC_SMPTE2084, std::nullopt, [](AVFrame& frame) {
            addMastering(frame, 0.005, 4000.0);
            addContentLight(frame, 600, 200);
        });
    QVERIFY(maxCll);
    pl_frame mapped = pqMappedFrame(0.005f, 4000.0f);
    QVERIFY(policy.shouldMapDolbyVision(*maxCll, target(1.0f)));
    LibplaceboColorPolicyDecision decision = policy.resolve(*maxCll, mapped, target(1.0f));
    QCOMPARE(decision.toneMapping, LibplaceboToneMappingFunction::Bt2446a);
    QCOMPARE(decision.metadata, PL_HDR_METADATA_HDR10);
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::MaxCll);
    QCOMPARE(decision.effectiveSourceMaximumNits, std::optional<float>(600.0f));
    QVERIFY(decision.useAbsoluteTargetLuminance);
    QVERIFY(decision.qualification.contains(QStringLiteral("4000")));

    std::shared_ptr<DecodedVideoFrame const> const mastering =
        makeFrame(11, 1, AVCOL_TRC_SMPTE2084, std::nullopt, [](AVFrame& frame) { addMastering(frame, 0.005, 4000.0); });
    QVERIFY(mastering);
    QVERIFY(policy.shouldMapDolbyVision(*mastering, target(1.0f)));
    decision = policy.resolve(*mastering, mapped, target(1.0f));
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::MasteringDisplay);
    QCOMPARE(decision.effectiveSourceMaximumNits, std::optional<float>(4000.0f));

    std::shared_ptr<DecodedVideoFrame const> const missing = makeFrame(12, 1, AVCOL_TRC_SMPTE2084);
    QVERIFY(missing);
    QVERIFY(policy.shouldMapDolbyVision(*missing, target(1.0f)));
    decision = policy.resolve(*missing, pqMappedFrame(0.0f, 0.0f), target(1.0f));
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::PqCompatibilityFallback);
    QCOMPARE(decision.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(decision.effectiveSourceMaximumNits,
             std::optional<float>(LibplaceboColorPolicy::pqCompatibilityMaximumNits));
    QVERIFY(decision.useAbsoluteTargetLuminance);
    QVERIFY(decision.description().contains(QStringLiteral("explicit PQ compatibility fallback")));

    std::shared_ptr<DecodedVideoFrame const> const invalidStatic =
        makeFrame(13, 1, AVCOL_TRC_SMPTE2084, std::nullopt, [](AVFrame& frame) {
            addMastering(frame, 2000.0, 1000.0);
            addContentLight(frame, 10'001);
        });
    QVERIFY(invalidStatic);
    QVERIFY(policy.shouldMapDolbyVision(*invalidStatic, target(1.0f)));
    decision = policy.resolve(*invalidStatic, mapped, target(1.0f));
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::PqCompatibilityFallback);

    LibplaceboColorPolicyDecision const hdrMetadataDecision = policy.resolve(*maxCll, mapped, target(4.0f));
    QCOMPARE(hdrMetadataDecision.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(hdrMetadataDecision.metadata, PL_HDR_METADATA_HDR10);
    QCOMPARE(hdrMetadataDecision.provenance, LibplaceboSourceMetadataProvenance::MaxCll);
    QCOMPARE(hdrMetadataDecision.effectiveSourceMaximumNits, std::optional<float>(600.0f));
    QVERIFY(hdrMetadataDecision.useAbsoluteTargetLuminance);

    LibplaceboColorPolicyDecision const unknownTargetDecision = policy.resolve(*maxCll, mapped, target(4.0f, false));
    QCOMPARE(unknownTargetDecision.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(unknownTargetDecision.metadata, PL_HDR_METADATA_HDR10);
    QCOMPARE(unknownTargetDecision.provenance, LibplaceboSourceMetadataProvenance::MaxCll);
    QCOMPARE(unknownTargetDecision.effectiveSourceMaximumNits, std::optional<float>(600.0f));
    QVERIFY(!unknownTargetDecision.useAbsoluteTargetLuminance);
    QVERIFY(unknownTargetDecision.qualification.contains(QStringLiteral("physical target luminance unavailable")));
}

void LibplaceboColorPolicyTest::usesOnlyPinnedRepresentableHdr10PlusOotfs() {
    LibplaceboColorPolicy policy;
    auto evaluate = [&policy](std::uint64_t generation, int windows, int rawAnchors, int mappedAnchors,
                              int applicationVersion = 1, float targetHeadroom = 1.0f,
                              bool targetPeakLuminanceKnown = true) {
        std::shared_ptr<DecodedVideoFrame const> frame =
            makeFrame(generation, 1, AVCOL_TRC_SMPTE2084, std::nullopt,
                      [=](AVFrame& source) { addHdr10Plus(source, windows, true, rawAnchors, applicationVersion); });
        Q_ASSERT(frame);
        pl_frame mapped = pqMappedFrame();
        addMappedHdr10Plus(mapped, mappedAnchors);
        policy.shouldMapDolbyVision(*frame, target(targetHeadroom, targetPeakLuminanceKnown));
        return policy.resolve(*frame, mapped, target(targetHeadroom, targetPeakLuminanceKnown));
    };

    LibplaceboColorPolicyDecision const ootf = evaluate(20, 1, 2, 2);
    QCOMPARE(ootf.toneMapping, LibplaceboToneMappingFunction::St2094_40);
    QCOMPARE(ootf.metadata, PL_HDR_METADATA_HDR10PLUS);
    QCOMPARE(ootf.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusOotf);
    QCOMPARE(ootf.selectedSourceAverageNits, std::optional<float>(80.0f));
    QVERIFY(ootf.useAbsoluteTargetLuminance);
    QVERIFY(ootf.description().contains(QStringLiteral("selected average 80 nits")));
    QVERIFY(ootf.description().contains(QStringLiteral("absolute target luminance")));
    QVERIFY(ootf.description().contains(QStringLiteral("203/target-white coordinate normalization")));

    LibplaceboColorPolicyDecision const hdrOotf = evaluate(30, 1, 2, 2, 1, 4.0f);
    QCOMPARE(hdrOotf.toneMapping, LibplaceboToneMappingFunction::St2094_40);
    QCOMPARE(hdrOotf.metadata, PL_HDR_METADATA_HDR10PLUS);
    QCOMPARE(hdrOotf.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusOotf);
    QVERIFY(hdrOotf.useAbsoluteTargetLuminance);

    LibplaceboColorPolicyDecision const unknownTargetOotf = evaluate(31, 1, 2, 2, 1, 4.0f, false);
    QCOMPARE(unknownTargetOotf.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(unknownTargetOotf.metadata, PL_HDR_METADATA_HDR10PLUS);
    QCOMPARE(unknownTargetOotf.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusScene);
    QVERIFY(!unknownTargetOotf.useAbsoluteTargetLuminance);
    QVERIFY(unknownTargetOotf.qualification.contains(QStringLiteral("physical target luminance unavailable")));
    QVERIFY(unknownTargetOotf.qualification.contains(QStringLiteral("source OOTF not applied")));

    LibplaceboColorPolicyDecision const zeroAnchor = evaluate(21, 1, 0, 0);
    QCOMPARE(zeroAnchor.toneMapping, LibplaceboToneMappingFunction::Bt2446a);
    QCOMPARE(zeroAnchor.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusScene);
    QVERIFY(zeroAnchor.qualification.contains(QStringLiteral("zero-anchor")));

    LibplaceboColorPolicyDecision const hdrZeroAnchor = evaluate(32, 1, 0, 0, 1, 4.0f);
    QCOMPARE(hdrZeroAnchor.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(hdrZeroAnchor.metadata, PL_HDR_METADATA_HDR10PLUS);
    QCOMPARE(hdrZeroAnchor.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusScene);
    QVERIFY(hdrZeroAnchor.qualification.contains(QStringLiteral("zero-anchor")));

    LibplaceboColorPolicyDecision const multipleWindows = evaluate(22, 2, 2, 2, 0);
    QCOMPARE(multipleWindows.toneMapping, LibplaceboToneMappingFunction::Bt2446a);
    QCOMPARE(multipleWindows.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusScene);
    QVERIFY(multipleWindows.qualification.contains(QStringLiteral("local-window")));

    std::shared_ptr<DecodedVideoFrame const> const localOnlyOotf =
        makeFrame(221, 1, AVCOL_TRC_SMPTE2084, std::nullopt, [](AVFrame& source) {
            addHdr10Plus(source, 2, false, 0, 0);
            AVFrameSideData* const sideData = av_frame_get_side_data(&source, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
            Q_ASSERT(sideData);
            auto* const metadata = reinterpret_cast<AVDynamicHDRPlus*>(sideData->data);
            metadata->params[1].tone_mapping_flag = 1;
            metadata->params[1].knee_point_x = {1, 2};
            metadata->params[1].knee_point_y = {1, 2};
            metadata->params[1].num_bezier_curve_anchors = 1;
            metadata->params[1].bezier_curve_anchors[0] = {1, 2};
        });
    QVERIFY(localOnlyOotf);
    pl_frame localOnlyMapped = pqMappedFrame();
    addMappedHdr10Plus(localOnlyMapped);
    policy.shouldMapDolbyVision(*localOnlyOotf, target(1.0f));
    LibplaceboColorPolicyDecision const localOnlyDecision =
        policy.resolve(*localOnlyOotf, localOnlyMapped, target(1.0f));
    QCOMPARE(localOnlyDecision.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusScene);
    QVERIFY(localOnlyDecision.qualification.contains(QStringLiteral("local-window")));

    LibplaceboColorPolicyDecision const invalidVersionOneWindows = evaluate(25, 2, 2, 2);
    QCOMPARE(invalidVersionOneWindows.provenance, LibplaceboSourceMetadataProvenance::PqCompatibilityFallback);

    LibplaceboColorPolicyDecision const tooManyVersionOneAnchors = evaluate(26, 1, 10, 10);
    QCOMPARE(tooManyVersionOneAnchors.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusScene);
    QVERIFY(tooManyVersionOneAnchors.qualification.contains(QStringLiteral("unsupported")));

    LibplaceboColorPolicyDecision const maximumVersionZeroAnchors = evaluate(28, 1, 15, 15, 0);
    QCOMPARE(maximumVersionZeroAnchors.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusOotf);

    std::shared_ptr<DecodedVideoFrame const> const zeroKnee =
        makeFrame(29, 1, AVCOL_TRC_SMPTE2084, std::nullopt, [](AVFrame& source) {
            addHdr10Plus(source, 1, true, 2);
            AVFrameSideData* const sideData = av_frame_get_side_data(&source, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
            Q_ASSERT(sideData);
            auto* const metadata = reinterpret_cast<AVDynamicHDRPlus*>(sideData->data);
            metadata->params[0].knee_point_x = {0, 1};
            metadata->params[0].knee_point_y = {0, 1};
        });
    QVERIFY(zeroKnee);
    pl_frame zeroKneeMapped = pqMappedFrame();
    addMappedHdr10Plus(zeroKneeMapped, 2);
    zeroKneeMapped.color.hdr.ootf.knee_x = 0.0f;
    zeroKneeMapped.color.hdr.ootf.knee_y = 0.0f;
    policy.shouldMapDolbyVision(*zeroKnee, target(1.0f));
    LibplaceboColorPolicyDecision const zeroKneeDecision = policy.resolve(*zeroKnee, zeroKneeMapped, target(1.0f));
    QCOMPARE(zeroKneeDecision.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusOotf);

    std::shared_ptr<DecodedVideoFrame const> const malformedOotf =
        makeFrame(23, 1, AVCOL_TRC_SMPTE2084, std::nullopt, [](AVFrame& source) { addHdr10Plus(source, 1, true, 2); });
    QVERIFY(malformedOotf);
    pl_frame malformedMapped = pqMappedFrame();
    addMappedHdr10Plus(malformedMapped, 2);
    malformedMapped.color.hdr.ootf.anchors[0] = std::numeric_limits<float>::quiet_NaN();
    policy.shouldMapDolbyVision(*malformedOotf, target(1.0f));
    LibplaceboColorPolicyDecision const malformedDecision =
        policy.resolve(*malformedOotf, malformedMapped, target(1.0f));
    QCOMPARE(malformedDecision.toneMapping, LibplaceboToneMappingFunction::Bt2446a);
    QCOMPARE(malformedDecision.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusScene);
    QVERIFY(malformedDecision.qualification.contains(QStringLiteral("unavailable")));

    std::shared_ptr<DecodedVideoFrame const> const descendingOotf =
        makeFrame(27, 1, AVCOL_TRC_SMPTE2084, std::nullopt, [](AVFrame& source) { addHdr10Plus(source, 1, true, 2); });
    QVERIFY(descendingOotf);
    pl_frame descendingMapped = pqMappedFrame();
    addMappedHdr10Plus(descendingMapped, 2);
    descendingMapped.color.hdr.ootf.anchors[0] = 0.8f;
    descendingMapped.color.hdr.ootf.anchors[1] = 0.7f;
    policy.shouldMapDolbyVision(*descendingOotf, target(1.0f));
    LibplaceboColorPolicyDecision const descendingDecision =
        policy.resolve(*descendingOotf, descendingMapped, target(1.0f));
    QCOMPARE(descendingDecision.provenance, LibplaceboSourceMetadataProvenance::Hdr10PlusScene);
    QVERIFY(descendingDecision.qualification.contains(QStringLiteral("unavailable")));

    std::shared_ptr<DecodedVideoFrame const> const malformedScene =
        makeFrame(24, 1, AVCOL_TRC_SMPTE2084, std::nullopt, [](AVFrame& source) {
            addHdr10Plus(source, 1, false, 0);
            addContentLight(source, 450);
        });
    QVERIFY(malformedScene);
    pl_frame malformedSceneMapped = pqMappedFrame();
    addMappedHdr10Plus(malformedSceneMapped);
    malformedSceneMapped.color.hdr.scene_avg = 700.0f;
    policy.shouldMapDolbyVision(*malformedScene, target(1.0f));
    LibplaceboColorPolicyDecision const malformedSceneDecision =
        policy.resolve(*malformedScene, malformedSceneMapped, target(1.0f));
    QCOMPARE(malformedSceneDecision.provenance, LibplaceboSourceMetadataProvenance::MaxCll);
    QCOMPARE(malformedSceneDecision.effectiveSourceMaximumNits, std::optional<float>(450.0f));
}

void LibplaceboColorPolicyTest::keepsDolbyMetadataWithinTheMappedRepresentation() {
    LibplaceboColorPolicy policy;
    std::shared_ptr<DecodedVideoFrame const> const source =
        makeFrame(30, 1, AVCOL_TRC_SMPTE2084, false, [](AVFrame& frame) {
            addDolbyVision(frame);
            addHdr10Plus(frame, 1, true, 2);
            addContentLight(frame, 4000, 1000);
        });
    QVERIFY(source);
    QVERIFY(policy.shouldMapDolbyVision(*source, target(1.0f)));

    struct pl_dovi_metadata dovi{};
    pl_frame mapped = pqMappedFrame(0.001f, 1200.0f);
    mapped.repr.sys = PL_COLOR_SYSTEM_DOLBYVISION;
    mapped.repr.dovi = &dovi;
    mapped.color.hdr.max_pq_y = pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, 900.0f);
    mapped.color.hdr.avg_pq_y = pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, 120.0f);

    LibplaceboColorPolicyDecision decision = policy.resolve(*source, mapped, target(1.0f));
    QCOMPARE(decision.metadata, PL_HDR_METADATA_CIE_Y);
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::DolbyVisionLevel1);
    QVERIFY(!decision.effectiveSourceMaximumNits);
    QVERIFY(decision.qualification.contains(QStringLiteral("concurrent HDR10+")));

    mapped.color.hdr.avg_pq_y = pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, 950.0f);
    decision = policy.resolve(*source, mapped, target(1.0f));
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::DolbyVisionSourceRange);

    decision = policy.resolve(*source, mapped, target(4.0f));
    QCOMPARE(decision.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(decision.metadata, PL_HDR_METADATA_HDR10);
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::DolbyVisionSourceRange);
    QCOMPARE(decision.effectiveSourceMaximumNits, std::optional<float>(1200.0f));
    QVERIFY(decision.useAbsoluteTargetLuminance);
    QVERIFY(decision.qualification.contains(QStringLiteral("concurrent HDR10+")));

    mapped.color.hdr.max_pq_y = 0.0f;
    mapped.color.hdr.avg_pq_y = 0.0f;
    decision = policy.resolve(*source, mapped, target(1.0f));
    QCOMPARE(decision.metadata, PL_HDR_METADATA_HDR10);
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::DolbyVisionSourceRange);
    QCOMPARE(decision.effectiveSourceMaximumNits, std::optional<float>(1200.0f));
    QVERIFY(decision.effectiveSourceMaximumNits != std::optional<float>(4000.0f));

    mapped.color.hdr.min_luma = std::numeric_limits<float>::quiet_NaN();
    mapped.color.hdr.max_luma = 0.0f;
    decision = policy.resolve(*source, mapped, target(1.0f));
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::PqCompatibilityFallback);

    std::shared_ptr<DecodedVideoFrame const> const unspecifiedSource =
        makeFrame(31, 1, AVCOL_TRC_UNSPECIFIED, false, [](AVFrame& frame) { addDolbyVision(frame); });
    QVERIFY(unspecifiedSource);
    mapped = pqMappedFrame(0.001f, 1200.0f);
    mapped.repr.sys = PL_COLOR_SYSTEM_DOLBYVISION;
    mapped.repr.dovi = &dovi;
    decision = policy.resolve(*unspecifiedSource, mapped, target(4.0f));
    QCOMPARE(decision.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(decision.metadata, PL_HDR_METADATA_HDR10);
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::DolbyVisionSourceRange);
    QVERIFY(decision.useAbsoluteTargetLuminance);

    decision = policy.resolve(*unspecifiedSource, mapped, target(4.0f, false));
    QCOMPARE(decision.toneMapping, LibplaceboToneMappingFunction::Spline);
    QCOMPARE(decision.metadata, PL_HDR_METADATA_HDR10);
    QCOMPARE(decision.provenance, LibplaceboSourceMetadataProvenance::DolbyVisionSourceRange);
    QVERIFY(!decision.useAbsoluteTargetLuminance);
    QVERIFY(decision.qualification.contains(QStringLiteral("physical target luminance unavailable")));
}

void LibplaceboColorPolicyTest::keepsDualFormatRepresentationStable() {
    LibplaceboColorPolicy policy;
    auto const dualFrame = [](std::uint64_t generation, std::uint64_t frameId, std::optional<bool> compatible,
                              bool includeHdr10Plus) {
        return makeFrame(generation, frameId, AVCOL_TRC_SMPTE2084, compatible, [=](AVFrame& frame) {
            addDolbyVision(frame);
            if (includeHdr10Plus) {
                addHdr10Plus(frame, 1, true, 2);
            }
        });
    };

    std::shared_ptr<DecodedVideoFrame const> const first = dualFrame(40, 1, true, true);
    std::shared_ptr<DecodedVideoFrame const> const laterWithoutDynamicMetadata = dualFrame(40, 2, true, false);
    QVERIFY(first);
    QVERIFY(laterWithoutDynamicMetadata);
    QCOMPARE(policy.shouldMapDolbyVision(*first, target(1.0f)), false);
    QCOMPARE(policy.shouldMapDolbyVision(*laterWithoutDynamicMetadata, target(1.0f)), false);
    QCOMPARE(policy.shouldMapDolbyVision(*laterWithoutDynamicMetadata, target(4.0f)), true);
    QCOMPARE(policy.shouldMapDolbyVision(*laterWithoutDynamicMetadata, target(1.0f)), false);

    std::shared_ptr<DecodedVideoFrame const> const unknown = dualFrame(41, 1, std::nullopt, true);
    QVERIFY(unknown);
    QCOMPARE(policy.shouldMapDolbyVision(*unknown, target(1.0f)), true);

    std::shared_ptr<DecodedVideoFrame const> const lateMetadata = dualFrame(42, 1, true, false);
    std::shared_ptr<DecodedVideoFrame const> const laterOotf = dualFrame(42, 2, true, true);
    QVERIFY(lateMetadata);
    QVERIFY(laterOotf);
    QCOMPARE(policy.shouldMapDolbyVision(*lateMetadata, target(1.0f)), true);
    QCOMPARE(policy.shouldMapDolbyVision(*laterOotf, target(1.0f)), true);

    std::shared_ptr<DecodedVideoFrame const> const sceneOnly =
        makeFrame(43, 1, AVCOL_TRC_SMPTE2084, true, [](AVFrame& frame) {
            addDolbyVision(frame);
            addHdr10Plus(frame, 1, false, 0);
        });
    QVERIFY(sceneOnly);
    QCOMPARE(policy.shouldMapDolbyVision(*sceneOnly, target(1.0f)), true);

    std::shared_ptr<DecodedVideoFrame const> const descendingRawOotf =
        makeFrame(45, 1, AVCOL_TRC_SMPTE2084, true, [](AVFrame& frame) {
            addDolbyVision(frame);
            addHdr10Plus(frame, 1, true, 2);
            AVFrameSideData* const sideData = av_frame_get_side_data(&frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
            Q_ASSERT(sideData);
            auto* const metadata = reinterpret_cast<AVDynamicHDRPlus*>(sideData->data);
            metadata->params[0].bezier_curve_anchors[0] = {4, 5};
            metadata->params[0].bezier_curve_anchors[1] = {3, 5};
        });
    QVERIFY(descendingRawOotf);
    QCOMPARE(policy.shouldMapDolbyVision(*descendingRawOotf, target(1.0f)), true);

    std::shared_ptr<DecodedVideoFrame const> const hdrFirst = dualFrame(44, 1, true, true);
    std::shared_ptr<DecodedVideoFrame const> const sdrLater = dualFrame(44, 2, true, false);
    QVERIFY(hdrFirst);
    QVERIFY(sdrLater);
    QCOMPARE(policy.shouldMapDolbyVision(*hdrFirst, target(4.0f)), true);
    QCOMPARE(policy.shouldMapDolbyVision(*sdrLater, target(1.0f)), false);
}

void LibplaceboColorPolicyTest::bt2446aMatchesTheIndependentReferenceEetf() {
    auto verifyRange = [](float outputPeakNits) {
        pl_tone_map_params parameters{};
        parameters.function = &pl_tone_map_bt2446a;
        parameters.constants = pl_color_map_default_params.tone_constants;
        parameters.input_scaling = PL_HDR_NITS;
        parameters.output_scaling = PL_HDR_NITS;
        parameters.input_min = 0.0f;
        parameters.input_max = 1000.0f;
        parameters.output_min = 0.0f;
        parameters.output_max = outputPeakNits;
        pl_tone_map_params_infer(&parameters);

        float previous = -1.0f;
        constexpr std::array samples{0.0f, 10.0f, 50.0f, 100.0f, 203.0f, 400.0f, 1000.0f};
        for (float const input : samples) {
            float const actual = pl_tone_map_sample(input, &parameters);
            float const expected = referenceBt2446a(input, 1000.0f, outputPeakNits);
            QVERIFY(std::isfinite(actual));
            QVERIFY(actual >= previous);
            QVERIFY(actual <= outputPeakNits * 1.0001f);
            QVERIFY2(std::abs(actual - expected) <= 0.08f,
                     qPrintable(QStringLiteral("BT.2446A %1→%2 nits: expected %3, got %4")
                                    .arg(input)
                                    .arg(outputPeakNits)
                                    .arg(expected)
                                    .arg(actual)));
            previous = actual;
        }
    };

    verifyRange(100.0f);
    verifyRange(203.0f);
}

void LibplaceboColorPolicyTest::st2094_40MatchesAnIndependentBezierVector() {
    auto const sample = [](float anchor, float authoredTargetNits, float outputMaximumNits, float inputNits) {
        pl_tone_map_params parameters{};
        parameters.function = &pl_tone_map_st2094_40;
        parameters.constants = pl_color_map_default_params.tone_constants;
        parameters.input_scaling = PL_HDR_NITS;
        parameters.output_scaling = PL_HDR_NITS;
        parameters.input_min = 0.0f;
        parameters.input_max = 1000.0f;
        parameters.output_min = 0.0f;
        parameters.output_max = outputMaximumNits;
        parameters.hdr.ootf.target_luma = authoredTargetNits;
        parameters.hdr.ootf.knee_x = 0.5f;
        parameters.hdr.ootf.knee_y = 0.25f;
        parameters.hdr.ootf.num_anchors = 1;
        parameters.hdr.ootf.anchors[0] = anchor;
        pl_tone_map_params_infer(&parameters);
        return pl_tone_map_sample(inputNits, &parameters);
    };

    constexpr std::array inputs{0.0f, 250.0f, 500.0f, 750.0f, 1000.0f};
    constexpr std::array expected{0.0f, 75.0f, 150.0f, 318.75f, 600.0f};
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        float const actual = sample(0.25f, 600.0f, 600.0f, inputs[index]);
        QVERIFY2(std::abs(actual - expected[index]) <= 0.02f,
                 qPrintable(QStringLiteral("ST 2094-40 %1 nits: expected %2, got %3")
                                .arg(inputs[index])
                                .arg(expected[index])
                                .arg(actual)));
    }
    QVERIFY(std::abs(sample(0.75f, 600.0f, 600.0f, 750.0f) - 431.25f) <= 0.02f);

    float previous = -1.0f;
    for (float const input : inputs) {
        float const actual = sample(0.25f, 600.0f, 300.0f, input);
        QVERIFY(std::isfinite(actual));
        QVERIFY(actual >= previous);
        QVERIFY(actual <= 300.01f);
        previous = actual;
    }
    float const authoredTargetNormalized = sample(0.25f, 600.0f, 600.0f, 750.0f) / 600.0f;
    float const adaptedTargetNormalized = sample(0.25f, 600.0f, 300.0f, 750.0f) / 300.0f;
    QVERIFY(adaptedTargetNormalized > authoredTargetNormalized);

    float const adaptedFrom600Nits = sample(0.25f, 600.0f, 300.0f, 750.0f);
    float const authoredFor300Nits = sample(0.25f, 300.0f, 300.0f, 750.0f);
    QVERIFY(std::abs(adaptedFrom600Nits - 215.625f) <= 0.02f);
    QVERIFY(std::abs(authoredFor300Nits - 159.375f) <= 0.02f);
    QVERIFY(adaptedFrom600Nits > authoredFor300Nits);
}

QTEST_APPLESS_MAIN(LibplaceboColorPolicyTest)
#include "tst_LibplaceboColorPolicy.moc"
