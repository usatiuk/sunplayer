#include "video/LibplaceboDiagnosticVideoProducer.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QtCore/qlogging.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/tone_mapping.h>
#include <rhi/qrhi.h>

#include "graphics/GraphicsDeviceDomain.h"
#include "video/DiagnosticVideoSource.h"

namespace {
constexpr float libplaceboReferenceWhiteNits =
    PL_COLOR_SDR_WHITE;
constexpr float tau = 6.28318530718f;

float smoothStep(float value) {
    return value * value * (3.0f - 2.0f * value);
}

float linearToSrgb(float value) {
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

void configurePlane(pl_plane &plane, pl_tex texture) {
    plane.texture = texture;
    plane.components = 4;
    plane.component_mapping[0] = 0;
    plane.component_mapping[1] = 1;
    plane.component_mapping[2] = 2;
    plane.component_mapping[3] = 3;
}

void configureRgbRepresentation(pl_color_repr &representation) {
    representation = pl_color_repr_rgb;
    representation.alpha = PL_ALPHA_INDEPENDENT;
}
}

LibplaceboDiagnosticVideoProducer::
LibplaceboDiagnosticVideoProducer(
        GraphicsDeviceDomain &graphicsDevice,
        const DiagnosticVideoSource &source,
        VideoTargetReadback readback)
    : m_rhi(graphicsDevice.rhi()),
      m_gpu(graphicsDevice.libplaceboContext().gpu),
      m_source(source),
      m_target(graphicsDevice.createVideoTarget({
          .producerApi = VideoProducerApi::Libplacebo,
          .readback = readback,
      })) {
    const LibplaceboGraphicsContext &context =
        graphicsDevice.libplaceboContext();
    if (!m_gpu || !context.log) {
        m_failureReason =
            QStringLiteral("Libplacebo graphics context is unavailable");
        return;
    }
    if (!m_target) {
        m_failureReason =
            QStringLiteral(
                "The graphics backend does not provide a libplacebo target");
        return;
    }

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

    m_renderer = pl_renderer_create(context.log, m_gpu);
    if (!m_renderer) {
        m_failureReason =
            QStringLiteral("Could not create the libplacebo renderer");
    }
}

LibplaceboDiagnosticVideoProducer::
~LibplaceboDiagnosticVideoProducer() {
    pl_renderer_destroy(&m_renderer);
    pl_tex_destroy(m_gpu, &m_sourceTexture);
}

VideoOperationResult
LibplaceboDiagnosticVideoProducer::ensureSurface(
        const RenderedVideoSurfaceState &requestedState) {
    Q_ASSERT(requestedState.isValid());

    if (!m_renderer || !m_target)
        return unavailable(m_failureReason.isEmpty()
            ? QStringLiteral("Libplacebo renderer is unavailable")
            : m_failureReason);

    const VideoTargetUpdate update =
        m_target->ensureTarget(requestedState.description);
    if (update == VideoTargetUpdate::DeviceLost)
        return VideoOperationResult::DeviceLost;
    if (update == VideoTargetUpdate::Unavailable) {
        return unavailable(
            m_target->diagnostics().fallbackReason);
    }

    if (update == VideoTargetUpdate::Created
            || update == VideoTargetUpdate::Resized) {
        m_completedState.reset();
        m_pendingState.reset();
    }
    const QSize sourceSize = m_source.inputFrameSize();
    if (!m_sourceTexture || m_sourceSize != sourceSize) {
        if (!createSourceTexture(sourceSize)) {
            return deviceLost()
                ? VideoOperationResult::DeviceLost
                : VideoOperationResult::Unavailable;
        }
    }

    m_failureReason.clear();
    return VideoOperationResult::Ready;
}

bool LibplaceboDiagnosticVideoProducer::needsRender(
        const RenderedVideoSurfaceState &requestedState) const {
    return !m_completedState
        || !m_completedState->isReusableFor(requestedState);
}

VideoOperationResult
LibplaceboDiagnosticVideoProducer::render(
        QRhiCommandBuffer &commandBuffer,
        const RenderedVideoSurfaceState &requestedState) {
    Q_ASSERT(requestedState.isValid());
    Q_ASSERT(m_renderer);
    Q_ASSERT(m_sourceTexture);
    Q_ASSERT(m_target);
    Q_ASSERT(!m_pendingState);

    const VideoOperationResult beginResult =
        m_target->beginProducerAccess(commandBuffer);
    if (beginResult != VideoOperationResult::Ready)
        return beginResult;

    const float referenceWhiteNits =
        requestedState.description.referenceWhiteNits;
    m_referenceWhiteScale =
        libplaceboReferenceWhiteNits
        / referenceWhiteNits;
    m_targetSize =
        requestedState.description.pixelSize;
    const SourceUploadKey uploadKey =
        sourceUploadKey(referenceWhiteNits);
    bool sourceReady = true;
    if (!m_uploadedSourceKey
            || *m_uploadedSourceKey != uploadKey) {
        updateSourcePixels(referenceWhiteNits);
        pl_tex_transfer_params transfer{};
        transfer.tex = m_sourceTexture;
        transfer.row_pitch =
            static_cast<std::size_t>(m_sourceSize.width())
            * 4 * sizeof(float);
        transfer.ptr = m_sourcePixels.data();
        sourceReady = pl_tex_upload(m_gpu, &transfer);
        if (sourceReady)
            m_uploadedSourceKey = uploadKey;
    }

    bool rendered = false;
    if (sourceReady) {
        pl_frame image{};
        image.num_planes = 1;
        configurePlane(image.planes[0], m_sourceTexture);
        configureRgbRepresentation(image.repr);
        image.crop = {
            0.0f,
            0.0f,
            static_cast<float>(m_sourceSize.width()),
            static_cast<float>(m_sourceSize.height()),
        };

        const float sourcePeak =
            m_source.sourcePeakHeadroom();
        if (sourcePeak > 1.0f) {
            image.color = pl_color_space_hdr10;
            image.color.hdr = pl_hdr_metadata_hdr10;
            image.color.hdr.min_luma = PL_COLOR_HDR_BLACK;
            image.color.hdr.max_luma =
                sourcePeak * referenceWhiteNits;
            image.color.hdr.max_cll =
                image.color.hdr.max_luma;
            image.color.hdr.max_fall =
                image.color.hdr.max_luma * 0.5f;
        } else {
            image.color = pl_color_space_srgb;
            image.color.hdr.min_luma =
                PL_COLOR_HDR_BLACK;
            image.color.hdr.max_luma =
                referenceWhiteNits;
        }

        pl_frame target{};
        target.num_planes = 1;
        configurePlane(
            target.planes[0],
            m_target->libplaceboRenderTarget());
        configureRgbRepresentation(target.repr);
        target.color.primaries = PL_COLOR_PRIM_BT_709;
        target.color.transfer = PL_COLOR_TRC_LINEAR;
        const RenderedVideoSurfaceDescription &targetDescription =
            requestedState.description;
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
        if (!m_source.toneMappingEnabled()) {
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
        rendered = pl_render_image(
            m_renderer, &image, &target, &parameters);
    }

    const VideoOperationResult endResult =
        m_target->endProducerAccess(commandBuffer);
    if (endResult == VideoOperationResult::DeviceLost
            || deviceLost()) {
        m_failureReason =
            QStringLiteral("Graphics device lost during libplacebo rendering");
        return VideoOperationResult::DeviceLost;
    }
    if (endResult != VideoOperationResult::Ready) {
        return unavailable(QStringLiteral(
            "Could not complete libplacebo producer access"));
    }
    if (!sourceReady) {
        return unavailable(QStringLiteral(
            "Libplacebo could not upload the analytic source texture"));
    }
    if (!rendered) {
        const pl_render_errors errors =
            pl_renderer_get_errors(m_renderer);
        return unavailable(QStringLiteral(
            "Libplacebo rejected the analytic render (error flags 0x%1)")
            .arg(
                static_cast<unsigned int>(errors.errors),
                0,
                16));
    }

    m_failureReason.clear();
    m_pendingState = requestedState;
    return VideoOperationResult::Ready;
}

VideoOperationResult
LibplaceboDiagnosticVideoProducer::prepareForComposition(
        QRhiCommandBuffer &commandBuffer) {
    if (!m_target)
        return VideoOperationResult::Unavailable;
    return m_target->prepareForComposition(commandBuffer);
}

void LibplaceboDiagnosticVideoProducer::submissionAccepted() {
    if (m_target)
        m_target->submissionAccepted();
}

void LibplaceboDiagnosticVideoProducer::submissionAborted() {
    if (m_target)
        m_target->submissionAborted();
}

void LibplaceboDiagnosticVideoProducer::commitPendingRender() {
    if (!m_pendingState)
        return;
    m_completedState = std::move(m_pendingState);
    m_pendingState.reset();
}

void LibplaceboDiagnosticVideoProducer::discardPendingRender() {
    m_pendingState.reset();
}

QRhiTexture &
LibplaceboDiagnosticVideoProducer::textureForComposition() const {
    Q_ASSERT(m_target);
    return m_target->textureForComposition();
}

std::uint64_t
LibplaceboDiagnosticVideoProducer::
compositionTextureRevision() const {
    return m_target
        ? m_target->compositionTextureRevision()
        : 0;
}

RenderedVideoProducerDiagnostics
LibplaceboDiagnosticVideoProducer::diagnostics() const {
    RenderedVideoProducerDiagnostics result;
    result.producerName =
        m_source.sourcePeakHeadroom() > 1.0f
        ? QStringLiteral(
            "libplacebo target-relative BT.2020 / PQ")
        : QStringLiteral(
            "libplacebo analytic sRGB");
    result.inputPath =
        m_sourceSize.isEmpty()
        ? QStringLiteral("Fixed-size CPU RGBA32F diagnostic upload")
        : QStringLiteral(
            "Fixed-size CPU RGBA32F upload · %1×%2")
            .arg(m_sourceSize.width())
            .arg(m_sourceSize.height());
    result.knownInputCpuTransfersPerInputFrame = 1;
    if (m_target) {
        result.target = m_target->diagnostics();
    } else {
        result.target.outputPath =
            VideoOutputPath::Unavailable;
        result.target.synchronizationMode =
            QStringLiteral("Not active");
        result.target.fallbackReason =
            QStringLiteral("Libplacebo target is unavailable");
    }
    if (!m_failureReason.isEmpty()) {
        result.target = {};
        result.target.outputPath =
            VideoOutputPath::Unavailable;
        result.target.synchronizationMode =
            QStringLiteral("Not active");
        result.target.fallbackReason = m_failureReason;
    }
    Q_ASSERT(result.isValid());
    return result;
}

bool LibplaceboDiagnosticVideoProducer::createSourceTexture(
        const QSize &sourceSize) {
    Q_ASSERT(!sourceSize.isEmpty());
    pl_tex_destroy(m_gpu, &m_sourceTexture);
    m_sourceSize = {};
    m_uploadedSourceKey.reset();

    const pl_fmt format =
        pl_find_named_fmt(m_gpu, "rgba32f");
    if (!format) {
        unavailable(QStringLiteral(
            "Libplacebo does not expose an rgba32f source format"));
        return false;
    }

    pl_tex_params parameters{};
    parameters.w = sourceSize.width();
    parameters.h = sourceSize.height();
    parameters.format = format;
    parameters.sampleable = true;
    parameters.host_writable = true;
    m_sourceTexture = pl_tex_create(m_gpu, &parameters);
    if (!m_sourceTexture) {
        unavailable(QStringLiteral(
            "Could not create the libplacebo analytic source texture"));
        return false;
    }

    m_sourceSize = sourceSize;
    const std::size_t pixelCount =
        static_cast<std::size_t>(m_sourceSize.width())
        * static_cast<std::size_t>(m_sourceSize.height());
    m_sourcePixels.resize(pixelCount * 4);
    m_encodedPatternColumns.resize(
        static_cast<std::size_t>(m_sourceSize.width()));
    return true;
}

LibplaceboDiagnosticVideoProducer::SourceUploadKey
LibplaceboDiagnosticVideoProducer::sourceUploadKey(
        float referenceWhiteNits) const {
    const float sourcePeak =
        m_source.sourcePeakHeadroom();
    return {
        .pixelSize = m_sourceSize,
        .sourcePeakHeadroom = sourcePeak,
        .phase = m_source.phase(),
        // Only the target-relative PQ diagnostic encodes its active reference
        // white into the source signal. SDR input pixels remain unchanged.
        .encodingReferenceWhiteNits =
            sourcePeak > 1.0f ? referenceWhiteNits : 0.0f,
    };
}

void LibplaceboDiagnosticVideoProducer::updateSourcePixels(
        float referenceWhiteNits) {
    Q_ASSERT(!m_sourceSize.isEmpty());
    const int width = m_sourceSize.width();
    const int height = m_sourceSize.height();
    const float peak = m_source.sourcePeakHeadroom();
    const bool hdr = peak > 1.0f;
    Q_ASSERT(
        m_sourcePixels.size()
        == static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height) * 4);
    Q_ASSERT(
        m_encodedPatternColumns.size()
        == static_cast<std::size_t>(width));

    const auto encode =
        [hdr, referenceWhiteNits](float value) {
        const float nonNegative = std::max(0.0f, value);
        // Diagnostic pattern values are defined in active-reference-white
        // units. A decoded PQ frame will instead retain its fixed absolute
        // source signal when the display target changes.
        return hdr
            ? pl_hdr_rescale(
                PL_HDR_NORM,
                PL_HDR_PQ,
                nonNegative
                    * referenceWhiteNits
                    / libplaceboReferenceWhiteNits)
            : linearToSrgb(
                std::clamp(
                    nonNegative,
                    0.0f,
                    1.0f));
    };

    for (int x = 0; x < width; ++x) {
        const float patternX =
            (static_cast<float>(x) + 0.5f)
            / static_cast<float>(width);
        const float ramp =
            0.02f
            + (peak - 0.02f)
                * smoothStep(patternX);
        const float spectrumPosition =
            patternX + m_source.phase();
        const float stepValue =
            std::floor(patternX * 8.0f) / 7.0f;
        std::array<float, 5> &column =
            m_encodedPatternColumns[
                static_cast<std::size_t>(x)];
        column[0] = encode(ramp);
        column[1] = encode(
            (0.5f + 0.5f * std::cos(
                tau * spectrumPosition)) * ramp);
        column[2] = encode(
            (0.5f + 0.5f * std::cos(
                tau * (spectrumPosition + 0.33f))) * ramp);
        column[3] = encode(
            (0.5f + 0.5f * std::cos(
                tau * (spectrumPosition + 0.67f))) * ramp);
        column[4] = encode(peak * stepValue);
    }

    for (int y = 0; y < height; ++y) {
        const float patternY =
            (static_cast<float>(y) + 0.5f)
            / static_cast<float>(height);
        const bool separator =
            std::abs(patternY - 0.33f) < 0.008f
            || std::abs(patternY - 0.66f) < 0.008f;
        for (int x = 0; x < width; ++x) {
            const std::array<float, 5> &column =
                m_encodedPatternColumns[
                    static_cast<std::size_t>(x)];
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;
            if (!separator) {
                if (patternY < 0.33f) {
                    red = column[0];
                    green = column[0];
                    blue = column[0];
                } else if (patternY < 0.66f) {
                    red = column[1];
                    green = column[2];
                    blue = column[3];
                } else {
                    red = column[4];
                    green = column[4];
                    blue = column[4];
                }
            }
            const std::size_t offset =
                (static_cast<std::size_t>(y)
                 * static_cast<std::size_t>(width)
                 + static_cast<std::size_t>(x)) * 4;
            m_sourcePixels[offset] = red;
            m_sourcePixels[offset + 1] = green;
            m_sourcePixels[offset + 2] = blue;
            m_sourcePixels[offset + 3] = 1.0f;
        }
    }
}

bool LibplaceboDiagnosticVideoProducer::deviceLost() const {
    return m_rhi.isDeviceLost()
        || (m_gpu && pl_gpu_is_failed(m_gpu));
}

VideoOperationResult
LibplaceboDiagnosticVideoProducer::unavailable(
        const QString &reason) {
    m_failureReason = reason.isEmpty()
        ? QStringLiteral("Libplacebo video path is unavailable")
        : reason;
    qWarning().noquote() << m_failureReason;
    return VideoOperationResult::Unavailable;
}

pl_hook_res
LibplaceboDiagnosticVideoProducer::applyReferenceWhiteScale(
        void *privateData,
        const pl_hook_params *parameters) {
    auto &self = *static_cast<
        LibplaceboDiagnosticVideoProducer *>(privateData);
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
