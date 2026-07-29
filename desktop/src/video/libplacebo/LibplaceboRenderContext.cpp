#include "video/libplacebo/LibplaceboRenderContext.h"

#include <QtCore/qassert.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/tone_mapping.h>

#include "graphics/GraphicsDeviceDomain.h"

namespace {
void configurePlane(pl_plane &plane, pl_tex texture) {
    plane.texture = texture;
    plane.components = 4;
    plane.component_mapping[0] = 0;
    plane.component_mapping[1] = 1;
    plane.component_mapping[2] = 2;
    plane.component_mapping[3] = 3;
}

void configureRgbRepresentation(
        pl_color_repr &representation) {
    representation = pl_color_repr_rgb;
    representation.alpha = PL_ALPHA_INDEPENDENT;
}
}

LibplaceboRenderContext::LibplaceboRenderContext(
        const LibplaceboGraphicsContext &graphics) {
    if (!graphics.isValid())
        return;

    m_referenceWhiteScaleVariable.var =
        pl_var_float("sunroomReferenceWhiteScale");
    m_referenceWhiteScaleVariable.data =
        &m_referenceWhiteScale;
    m_referenceWhiteScaleVariable.dynamic = true;
    m_referenceWhiteHook.stages = PL_HOOK_PRE_OUTPUT;
    m_referenceWhiteHook.input = PL_HOOK_SIG_COLOR;
    m_referenceWhiteHook.priv = this;
    m_referenceWhiteHook.hook =
        applyReferenceWhiteScale;
    m_referenceWhiteHook.signature =
        UINT64_C(0x73756e726f6f6d01);

    m_renderer =
        pl_renderer_create(graphics.log, graphics.gpu);
}

LibplaceboRenderContext::~LibplaceboRenderContext() {
    pl_renderer_destroy(&m_renderer);
}

bool LibplaceboRenderContext::isValid() const {
    return m_renderer;
}

bool LibplaceboRenderContext::render(
        const pl_frame &source,
        pl_tex targetTexture,
        const RenderedVideoSurfaceDescription &targetDescription,
        bool toneMappingEnabled,
        QString *error) {
    Q_ASSERT(isValid());
    Q_ASSERT(targetTexture);
    Q_ASSERT(targetDescription.isValid());
    if (error)
        error->clear();

    m_referenceWhiteScale =
        PL_COLOR_SDR_WHITE
        / targetDescription.referenceWhiteNits;
    m_targetSize = targetDescription.pixelSize;

    pl_frame effectiveSource = source;
    pl_color_space_infer(&effectiveSource.color);
    if (!pl_color_space_is_hdr(&effectiveSource.color)) {
        // SDR is a relative signal: decoded reference white maps to the
        // active display's SDR white. HDR transfers and explicitly HDR
        // metadata remain source-absolute.
        effectiveSource.color.hdr.min_luma =
            PL_COLOR_HDR_BLACK;
        effectiveSource.color.hdr.max_luma =
            targetDescription.referenceWhiteNits;
        effectiveSource.color.hdr.max_cll = 0.0f;
        effectiveSource.color.hdr.max_fall = 0.0f;
    }

    pl_frame target{};
    target.num_planes = 1;
    configurePlane(target.planes[0], targetTexture);
    configureRgbRepresentation(target.repr);
    target.color.primaries = PL_COLOR_PRIM_BT_709;
    target.color.transfer = PL_COLOR_TRC_LINEAR;
    // libplacebo reserves numeric zero for unknown metadata. Preserve
    // Sunroom's physical zero and translate it only at this API boundary.
    target.color.hdr.min_luma =
        targetDescription.targetMinimumLuminanceKnown
        ? (targetDescription.targetMinimumLuminanceNits == 0.0f
            ? PL_COLOR_HDR_BLACK
            : targetDescription.targetMinimumLuminanceNits)
        : 0.0f;
    target.color.hdr.max_luma =
        targetDescription.referenceWhiteNits
        * targetDescription.targetPeakHeadroom;
    target.crop = {
        0.0f,
        0.0f,
        static_cast<float>(targetDescription.pixelSize.width()),
        static_cast<float>(targetDescription.pixelSize.height()),
    };

    pl_color_map_params colorMap =
        pl_color_map_default_params;
    if (!toneMappingEnabled) {
        colorMap.tone_mapping_function =
            &pl_tone_map_clip;
    }
    pl_render_params parameters =
        pl_render_default_params;
    parameters.color_map_params = &colorMap;
    parameters.dither_params = nullptr;
    parameters.peak_detect_params = nullptr;
    const pl_hook *hooks[] = {
        &m_referenceWhiteHook,
    };
    parameters.hooks = hooks;
    parameters.num_hooks = 1;

    if (pl_render_image(
            m_renderer,
            &effectiveSource,
            &target,
            &parameters)) {
        return true;
    }

    const pl_render_errors errors =
        pl_renderer_get_errors(m_renderer);
    if (error) {
        *error = QStringLiteral(
            "Libplacebo rejected the video render "
            "(error flags 0x%1)")
            .arg(
                static_cast<unsigned int>(errors.errors),
                0,
                16);
    }
    return false;
}

pl_hook_res
LibplaceboRenderContext::applyReferenceWhiteScale(
        void *privateData,
        const pl_hook_params *parameters) {
    auto &self = *static_cast<
        LibplaceboRenderContext *>(privateData);
    Q_ASSERT(parameters);
    Q_ASSERT(parameters->stage == PL_HOOK_PRE_OUTPUT);
    Q_ASSERT(parameters->sh);

    pl_custom_shader shader{};
    shader.description =
        "Sunroom reference-white normalization";
    shader.body =
        "color.rgb *= vec3(sunroomReferenceWhiteScale);";
    shader.input = PL_SHADER_SIG_COLOR;
    shader.output = PL_SHADER_SIG_COLOR;
    shader.output_w = self.m_targetSize.width();
    shader.output_h = self.m_targetSize.height();
    shader.variables =
        &self.m_referenceWhiteScaleVariable;
    shader.num_variables = 1;

    pl_hook_res result{};
    result.failed =
        !pl_shader_custom(parameters->sh, &shader);
    if (result.failed)
        return result;

    result.output = PL_HOOK_SIG_COLOR;
    result.sh = parameters->sh;
    result.repr = parameters->repr;
    result.color = parameters->color;
    result.components = parameters->components;
    result.rect = parameters->rect;
    return result;
}
