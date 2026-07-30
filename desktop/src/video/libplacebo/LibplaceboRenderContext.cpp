#include "video/libplacebo/LibplaceboRenderContext.h"

#include <algorithm>

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

float virtualTargetMinimumNits(
        const RenderedVideoSurfaceDescription &description) {
    if (!description.targetMinimumLuminanceKnown
            || description.targetMinimumLuminanceNits == 0.0f) {
        return PL_COLOR_HDR_BLACK;
    }

    return std::max(
        PL_COLOR_HDR_BLACK,
        PL_COLOR_SDR_WHITE
            * description.targetMinimumLuminanceNits
            / description.referenceWhiteNits);
}
}

LibplaceboRenderContext::LibplaceboRenderContext(
        const LibplaceboGraphicsContext &graphics) {
    if (!graphics.isValid())
        return;

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

    pl_frame effectiveSource = source;
    pl_color_space_infer(&effectiveSource.color);
    if (!pl_color_transfer_is_hdr(
            effectiveSource.color.transfer)) {
        // SDR transfer functions describe a relative signal even when a
        // container carries stale mastering or dynamic HDR luminance. Keep
        // source gamut information, but remove every absolute-luminance
        // candidate before anchoring encoded SDR white to libplacebo's
        // normalized 1.0. The output target owns physical reference white and
        // black level; the retained decoded frame remains unchanged.
        const pl_raw_primaries sourceMasteringPrimaries =
            effectiveSource.color.hdr.prim;
        effectiveSource.color.hdr =
            pl_hdr_metadata_empty;
        effectiveSource.color.hdr.prim =
            sourceMasteringPrimaries;
        effectiveSource.color.hdr.min_luma =
            PL_COLOR_HDR_BLACK;
        effectiveSource.color.hdr.max_luma =
            PL_COLOR_SDR_WHITE;
    }

    pl_frame target{};
    target.num_planes = 1;
    configurePlane(target.planes[0], targetTexture);
    configureRgbRepresentation(target.repr);
    target.color.primaries = PL_COLOR_PRIM_BT_709;
    target.color.transfer = PL_COLOR_TRC_LINEAR;
    // libplacebo writes linear values in a fixed coordinate system where
    // 1.0 means 203 nits. Describe the display target in that coordinate
    // system so its numerical output instead means 1.0 = active SDR white:
    //
    //   virtual peak = 203 * physical peak / active SDR white
    //                = 203 * target headroom.
    //
    // This keeps HDR source pixels and metadata untouched, gives libplacebo
    // the complete tone-mapping range, and needs no pre/post render
    // multiplier.
    target.color.hdr.min_luma =
        virtualTargetMinimumNits(targetDescription);
    target.color.hdr.max_luma =
        PL_COLOR_SDR_WHITE
        * targetDescription.targetPeakHeadroom;
    target.crop = {
        0.0f,
        0.0f,
        static_cast<float>(targetDescription.pixelSize.width()),
        static_cast<float>(targetDescription.pixelSize.height()),
    };

    pl_color_map_params colorMap =
        pl_color_map_default_params;
    colorMap.inverse_tone_mapping = false;
    if (!toneMappingEnabled) {
        colorMap.tone_mapping_function =
            &pl_tone_map_clip;
    }
    pl_render_params parameters =
        pl_render_default_params;
    parameters.color_map_params = &colorMap;
    parameters.dither_params = nullptr;
    parameters.peak_detect_params = nullptr;

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
