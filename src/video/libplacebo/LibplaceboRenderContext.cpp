#include "video/libplacebo/LibplaceboRenderContext.h"

#include <algorithm>
#include <cmath>

#include <QtCore/qassert.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/gamut_mapping.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/tone_mapping.h>

#include "graphics/GraphicsDeviceDomain.h"

namespace {
void configurePlane(pl_plane& plane, pl_tex texture) {
    plane.texture = texture;
    plane.components = 4;
    plane.component_mapping[0] = 0;
    plane.component_mapping[1] = 1;
    plane.component_mapping[2] = 2;
    plane.component_mapping[3] = 3;
}

void configureRgbRepresentation(pl_color_repr& representation) {
    representation = pl_color_repr_rgb;
    representation.alpha = PL_ALPHA_INDEPENDENT;
}

constexpr float nominalSdrMaximumNits = 100.0f;

float targetMinimumNits(RenderedVideoSurfaceDescription const& description, float targetMaximumNits) {
    if (!description.targetMinimumLuminanceKnown || description.targetMinimumLuminanceNits == 0.0f) {
        return PL_COLOR_HDR_BLACK;
    }

    float const physicalTargetMaximum = description.referenceWhiteNits * description.targetPeakHeadroom;
    return std::max(PL_COLOR_HDR_BLACK,
                    targetMaximumNits * description.targetMinimumLuminanceNits / physicalTargetMaximum);
}

pl_raw_primaries rawPrimaries(ColorPrimaries const& primaries) {
    return {
        .red = {primaries.red.x, primaries.red.y},
        .green = {primaries.green.x, primaries.green.y},
        .blue = {primaries.blue.x, primaries.blue.y},
        .white = {primaries.white.x, primaries.white.y},
    };
}

struct OutputNormalizationContext {
    float scale;
};

pl_hook_res normalizeNominalSdrOutputHook(void* privateData, pl_hook_params const* parameters) {
    auto const& context = *static_cast<OutputNormalizationContext const*>(privateData);
    pl_shader_var const variables[]{
        {.var = pl_var_float("outputNormalizationScale"), .data = &context.scale},
    };
    pl_custom_shader const shader{
        .description = "Normalize nominal SDR luminance into the surface coordinate system",
        .body = "color.rgb *= outputNormalizationScale;",
        .input = PL_SHADER_SIG_COLOR,
        .output = PL_SHADER_SIG_COLOR,
        .variables = variables,
        .num_variables = 1,
        .output_w = std::abs(pl_rect_w(parameters->dst_rect)),
        .output_h = std::abs(pl_rect_h(parameters->dst_rect)),
    };
    if (!pl_shader_custom(parameters->sh, &shader)) {
        return {.failed = true};
    }
    return {
        .output = PL_HOOK_SIG_COLOR,
        .sh = parameters->sh,
        .repr = parameters->repr,
        .color = parameters->color,
        .components = parameters->components,
        .rect = parameters->rect,
    };
}

} // namespace

LibplaceboTargetLuminance calculateLibplaceboTargetLuminance(pl_frame const& source, float targetPeakHeadroom) {
    Q_ASSERT(std::isfinite(targetPeakHeadroom) && targetPeakHeadroom >= 1.0f);
    bool const absoluteLuminanceSource =
        source.color.transfer == PL_COLOR_TRC_PQ || source.repr.sys == PL_COLOR_SYSTEM_DOLBYVISION;
    bool const nominalSdrTarget = targetPeakHeadroom <= 1.0f && absoluteLuminanceSource;
    float const coordinateWhiteNits = nominalSdrTarget ? nominalSdrMaximumNits : PL_COLOR_SDR_WHITE;
    return {
        .coordinateWhiteNits = coordinateWhiteNits,
        .maximumNits = coordinateWhiteNits * targetPeakHeadroom,
        .outputNormalizationScale = nominalSdrTarget ? PL_COLOR_SDR_WHITE / nominalSdrMaximumNits : 1.0f,
    };
}

LibplaceboRenderContext::LibplaceboRenderContext(LibplaceboGraphicsContext const& graphics) {
    if (!graphics.isValid()) {
        return;
    }

    m_renderer = pl_renderer_create(graphics.log, graphics.gpu);
}

LibplaceboRenderContext::~LibplaceboRenderContext() { pl_renderer_destroy(&m_renderer); }

bool LibplaceboRenderContext::isValid() const { return m_renderer; }

QString LibplaceboRenderContext::policyDescription(bool toneMappingEnabled) {
    return toneMappingEnabled ? QStringLiteral("Spline tone map · perceptual gamut map · "
                                               "inverse mapping off · peak detection off · dither off")
                              : QStringLiteral("Clip tone map · perceptual gamut map · "
                                               "inverse mapping off · peak detection off · dither off");
}

bool LibplaceboRenderContext::render(pl_frame const& source, pl_tex targetTexture,
                                     RenderedVideoSurfaceDescription const& targetDescription, bool toneMappingEnabled,
                                     QString* error) {
    return renderWithPolicy(source, targetTexture, targetDescription,
                            toneMappingEnabled ? LibplaceboToneMappingFunction::Spline
                                               : LibplaceboToneMappingFunction::Clip,
                            PL_HDR_METADATA_ANY, std::nullopt, error);
}

bool LibplaceboRenderContext::renderDecoded(pl_frame const& source, pl_tex targetTexture,
                                            RenderedVideoSurfaceDescription const& targetDescription,
                                            LibplaceboColorPolicyDecision const& colorPolicy, QString* error) {
    return renderWithPolicy(source, targetTexture, targetDescription, colorPolicy.toneMapping, colorPolicy.metadata,
                            colorPolicy.effectiveSourceMaximumNits, error);
}

bool LibplaceboRenderContext::renderWithPolicy(pl_frame const& source, pl_tex targetTexture,
                                               RenderedVideoSurfaceDescription const& targetDescription,
                                               LibplaceboToneMappingFunction toneMapping,
                                               enum pl_hdr_metadata_type metadata,
                                               std::optional<float> effectiveSourceMaximumNits, QString* error) {
    Q_ASSERT(isValid());
    Q_ASSERT(targetTexture);
    Q_ASSERT(targetDescription.isValid());
    if (error) {
        error->clear();
    }

    pl_frame effectiveSource = source;
    // Source ICC transforms remain outside the accepted rendering policy.
    // Clear both libplacebo representations on the render-local copy so an
    // LCMS-enabled system build cannot silently change cross-platform output.
    effectiveSource.icc = nullptr;
    effectiveSource.profile = {};
    if (effectiveSourceMaximumNits) {
        effectiveSource.color.hdr.max_luma = *effectiveSourceMaximumNits;
        if (!std::isfinite(effectiveSource.color.hdr.min_luma) || effectiveSource.color.hdr.min_luma <= 0.0f ||
            effectiveSource.color.hdr.min_luma > *effectiveSourceMaximumNits) {
            effectiveSource.color.hdr.min_luma = PL_COLOR_HDR_BLACK;
        }
    }
    pl_color_space_infer(&effectiveSource.color);
    if (!pl_color_transfer_is_hdr(effectiveSource.color.transfer)) {
        // SDR transfer functions describe a relative signal even when a
        // container carries stale mastering or dynamic HDR luminance. Keep
        // source gamut information, but remove every absolute-luminance
        // candidate before anchoring encoded SDR white to libplacebo's
        // normalized 1.0. The output target owns physical reference white and
        // black level; the retained decoded frame remains unchanged.
        pl_raw_primaries const sourceMasteringPrimaries = effectiveSource.color.hdr.prim;
        effectiveSource.color.hdr = pl_hdr_metadata_empty;
        effectiveSource.color.hdr.prim = sourceMasteringPrimaries;
        effectiveSource.color.hdr.min_luma = PL_COLOR_HDR_BLACK;
        effectiveSource.color.hdr.max_luma = PL_COLOR_SDR_WHITE;
    }

    pl_frame target{};
    target.num_planes = 1;
    configurePlane(target.planes[0], targetTexture);
    configureRgbRepresentation(target.repr);
    // SunPlayer's FP16 composition surface always uses linear BT.709
    // coordinates. Raw target primaries are a separate physical gamut
    // boundary and must not change that coordinate basis.
    target.color.primaries = PL_COLOR_PRIM_BT_709;
    target.color.transfer = PL_COLOR_TRC_LINEAR;
    target.color.hdr.prim = targetDescription.targetPrimariesKnown ? rawPrimaries(targetDescription.targetPrimaries)
                                                                   : *pl_raw_primaries_get(PL_COLOR_PRIM_BT_709);
    Q_ASSERT(pl_primaries_valid(&target.color.hdr.prim));
    LibplaceboTargetLuminance const targetLuminance =
        calculateLibplaceboTargetLuminance(effectiveSource, targetDescription.targetPeakHeadroom);
    target.color.hdr.min_luma = targetMinimumNits(targetDescription, targetLuminance.maximumNits);
    target.color.hdr.max_luma = targetLuminance.maximumNits;
    target.crop = {
        0.0f,
        0.0f,
        static_cast<float>(targetDescription.pixelSize.width()),
        static_cast<float>(targetDescription.pixelSize.height()),
    };

    pl_color_map_params colorMap = pl_color_map_default_params;
    colorMap.gamut_mapping = &pl_gamut_map_perceptual;
    switch (toneMapping) {
    case LibplaceboToneMappingFunction::Clip:
        colorMap.tone_mapping_function = &pl_tone_map_clip;
        break;
    case LibplaceboToneMappingFunction::Spline:
        colorMap.tone_mapping_function = &pl_tone_map_spline;
        break;
    case LibplaceboToneMappingFunction::Bt2446a:
        colorMap.tone_mapping_function = &pl_tone_map_bt2446a;
        break;
    case LibplaceboToneMappingFunction::St2094_40:
        colorMap.tone_mapping_function = &pl_tone_map_st2094_40;
        break;
    }
    colorMap.metadata = metadata;
    colorMap.inverse_tone_mapping = false;
    pl_render_params parameters = pl_render_default_params;
    parameters.color_map_params = &colorMap;
    parameters.dither_params = nullptr;
    parameters.peak_detect_params = nullptr;
    // Libplacebo's linear output unit is always nits / 203. A PQ/Dolby source
    // mapped to nominal 100-nit SDR needs one fixed coordinate conversion so
    // surface 1.0 remains the active platform reference white. HDR targets and
    // relative SDR/HLG sources need no producer normalization.
    OutputNormalizationContext outputNormalization{
        .scale = targetLuminance.outputNormalizationScale,
    };
    pl_hook const outputNormalizationHook{
        .stages = PL_HOOK_PRE_OUTPUT,
        .input = PL_HOOK_SIG_COLOR,
        .priv = &outputNormalization,
        .hook = normalizeNominalSdrOutputHook,
        .signature = 0x53554e5344524e4dULL,
    };
    pl_hook const* hooks[]{&outputNormalizationHook};
    if (targetLuminance.outputNormalizationScale != 1.0f) {
        parameters.hooks = hooks;
        parameters.num_hooks = 1;
    }

    if (pl_render_image(m_renderer, &effectiveSource, &target, &parameters)) {
        return true;
    }

    pl_render_errors const errors = pl_renderer_get_errors(m_renderer);
    if (error) {
        *error = QStringLiteral("Libplacebo rejected the video render "
                                "(error flags 0x%1)")
                     .arg(static_cast<unsigned int>(errors.errors), 0, 16);
    }
    return false;
}
