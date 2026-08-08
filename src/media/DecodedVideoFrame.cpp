#include "media/DecodedVideoFrame.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

extern "C" {
#include <libavutil/display.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
}

namespace {
QString ffmpegName(char const* name) {
    return name && *name ? QString::fromLatin1(name) : QStringLiteral("unspecified");
}

QString pixelFormatName(enum AVPixelFormat format) { return ffmpegName(av_get_pix_fmt_name(format)); }

VideoFrameStorageKind storageKind(enum AVPixelFormat format) {
    switch (format) {
    case AV_PIX_FMT_D3D11:
        return VideoFrameStorageKind::D3D11Surface;
    case AV_PIX_FMT_VULKAN:
        return VideoFrameStorageKind::VulkanImage;
    case AV_PIX_FMT_DRM_PRIME:
        return VideoFrameStorageKind::DrmPrime;
    case AV_PIX_FMT_VAAPI:
        return VideoFrameStorageKind::VaapiSurface;
    case AV_PIX_FMT_VIDEOTOOLBOX:
        return VideoFrameStorageKind::VideoToolboxSurface;
    default:
        break;
    }

    AVPixFmtDescriptor const* descriptor = av_pix_fmt_desc_get(format);
    return descriptor && descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL ? VideoFrameStorageKind::OtherHardwareSurface
                                                                     : VideoFrameStorageKind::SoftwarePlanes;
}

int componentDepth(AVPixFmtDescriptor const* descriptor) {
    if (!descriptor) {
        return 0;
    }
    int depth = 0;
    for (int index = 0; index < descriptor->nb_components; ++index) {
        depth = std::max(depth, static_cast<int>(descriptor->comp[index].depth));
    }
    return depth;
}

std::optional<std::int64_t> timestamp(std::int64_t value) {
    return value == AV_NOPTS_VALUE ? std::nullopt : std::optional<std::int64_t>(value);
}

bool checkedVisibleSize(AVFrame const& frame, QMargins& crop, QSize& visibleSize) {
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    std::uint64_t const horizontalCrop = frame.crop_left + frame.crop_right;
    std::uint64_t const verticalCrop = frame.crop_top + frame.crop_bottom;
    if (horizontalCrop >= static_cast<std::uint64_t>(frame.width) ||
        verticalCrop >= static_cast<std::uint64_t>(frame.height) ||
        frame.crop_left > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        frame.crop_top > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        frame.crop_right > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        frame.crop_bottom > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    crop = {
        static_cast<int>(frame.crop_left),
        static_cast<int>(frame.crop_top),
        static_cast<int>(frame.crop_right),
        static_cast<int>(frame.crop_bottom),
    };
    visibleSize = {
        frame.width - static_cast<int>(horizontalCrop),
        frame.height - static_cast<int>(verticalCrop),
    };
    return !visibleSize.isEmpty();
}
} // namespace

bool VideoFrameRational::isValid() const { return numerator > 0 && denominator > 0; }

bool VideoFrameIdentity::isValid() const { return playbackGeneration != 0 && decoderRevision != 0 && frameId != 0; }

bool VideoFrameTiming::isValid() const { return timeBase.isValid() && (!duration || *duration > 0); }

std::optional<std::int64_t> VideoFrameTiming::ptsMicroseconds() const {
    if (!isValid() || !pts) {
        return std::nullopt;
    }
    return av_rescale_q(*pts, {timeBase.numerator, timeBase.denominator}, AV_TIME_BASE_Q);
}

std::optional<std::int64_t> VideoFrameTiming::durationMicroseconds() const {
    if (!isValid() || !duration) {
        return std::nullopt;
    }
    std::int64_t const converted = av_rescale_q(*duration, {timeBase.numerator, timeBase.denominator}, AV_TIME_BASE_Q);
    return converted > 0 ? std::optional<std::int64_t>(converted) : std::nullopt;
}

bool VideoFrameGeometry::isValid() const {
    return !codedSize.isEmpty() && !visibleSize.isEmpty() && crop.left() >= 0 && crop.top() >= 0 && crop.right() >= 0 &&
           crop.bottom() >= 0 && visibleSize.width() == codedSize.width() - crop.left() - crop.right() &&
           visibleSize.height() == codedSize.height() - crop.top() - crop.bottom() && sampleAspectRatio.isValid() &&
           std::isfinite(rotationDegrees);
}

bool VideoFrameStorageDescription::isHardware() const { return kind != VideoFrameStorageKind::SoftwarePlanes; }

bool VideoFrameStorageDescription::isValid() const {
    return !softwareFormat.isEmpty() &&
           (isHardware() ? !hardwareFormat.isEmpty() && graphicsDeviceGeneration && *graphicsDeviceGeneration != 0
                         : hardwareFormat.isEmpty() && !graphicsDeviceGeneration);
}

bool VideoFrameStorageDescription::isCompatibleWithGraphicsDevice(std::uint64_t generation) const {
    if (!isValid() || generation == 0) {
        return false;
    }
    return !isHardware() || *graphicsDeviceGeneration == generation;
}

bool VideoSignalDescription::isValid() const {
    return !pixelFormat.isEmpty() && !colorPrimaries.isEmpty() && !transferFunction.isEmpty() &&
           !matrixCoefficients.isEmpty() && !colorRange.isEmpty() && !chromaLocation.isEmpty() && componentDepth > 0;
}

QString VideoSignalDescription::summary() const {
    return QStringLiteral("%1 · %2 · %3-bit · matrix %4 · range %5 · chroma %6 · "
                          "retained FFmpeg-decoded AVFrame")
        .arg(colorPrimaries, transferFunction, QString::number(componentDepth), matrixCoefficients, colorRange,
             chromaLocation);
}

std::shared_ptr<DecodedVideoFrame const>
DecodedVideoFrame::clone(AVFrame const& frame, VideoFrameIdentity const& identity, VideoFrameRational const& timeBase,
                         std::optional<std::uint64_t> graphicsDeviceGeneration, QString* error) {
    auto const fail = [error](QString const& reason) {
        if (error) {
            *error = reason;
        }
        return std::shared_ptr<DecodedVideoFrame const>();
    };

    if (!identity.isValid()) {
        return fail(QStringLiteral("Decoded frame identity is invalid"));
    }
    if (!timeBase.isValid()) {
        return fail(QStringLiteral("Decoded frame time base is invalid"));
    }

    auto const format = static_cast<enum AVPixelFormat>(frame.format);
    AVPixFmtDescriptor const* storageDescriptor = av_pix_fmt_desc_get(format);
    if (!storageDescriptor) {
        return fail(QStringLiteral("Decoded frame pixel format is unavailable"));
    }

    VideoFrameGeometry geometry;
    geometry.codedSize = {frame.width, frame.height};
    if (!checkedVisibleSize(frame, geometry.crop, geometry.visibleSize)) {
        return fail(QStringLiteral("Decoded frame geometry or crop is invalid"));
    }
    if (frame.sample_aspect_ratio.num > 0 && frame.sample_aspect_ratio.den > 0) {
        geometry.sampleAspectRatio = {
            frame.sample_aspect_ratio.num,
            frame.sample_aspect_ratio.den,
        };
        geometry.sampleAspectRatioKnown = true;
    }
    if (AVFrameSideData const* matrix = av_frame_get_side_data(&frame, AV_FRAME_DATA_DISPLAYMATRIX)) {
        if (matrix->size >= 9 * sizeof(std::int32_t)) {
            double const rotation = av_display_rotation_get(reinterpret_cast<std::int32_t const*>(matrix->data));
            if (std::isfinite(rotation)) {
                geometry.rotationDegrees = rotation;
                geometry.displayMatrixPresent = true;
            }
        }
    }
    if (!geometry.isValid()) {
        return fail(QStringLiteral("Decoded frame geometry is invalid"));
    }

    VideoFrameStorageDescription storage;
    storage.kind = storageKind(format);
    AVPixFmtDescriptor const* signalDescriptor = storageDescriptor;
    if (storage.isHardware()) {
        storage.hardwareFormat = pixelFormatName(format);
        storage.graphicsDeviceGeneration = graphicsDeviceGeneration;
        auto const* framesContext =
            frame.hw_frames_ctx ? reinterpret_cast<AVHWFramesContext const*>(frame.hw_frames_ctx->data) : nullptr;
        signalDescriptor = framesContext ? av_pix_fmt_desc_get(framesContext->sw_format) : nullptr;
        if (!framesContext || !signalDescriptor) {
            return fail(QStringLiteral("Decoded hardware frame has no valid "
                                       "software-plane description"));
        }
        storage.softwareFormat = pixelFormatName(framesContext->sw_format);
    } else {
        storage.softwareFormat = pixelFormatName(format);
    }
    if (!storage.isValid()) {
        return fail(QStringLiteral("Decoded frame storage description is invalid"));
    }

    VideoFrameTiming timing;
    timing.pts = timestamp(frame.best_effort_timestamp);
    if (!timing.pts) {
        timing.pts = timestamp(frame.pts);
    }
    if (frame.duration > 0) {
        timing.duration = frame.duration;
    }
    timing.timeBase = timeBase;
    if (!timing.isValid()) {
        return fail(QStringLiteral("Decoded frame timing is invalid"));
    }

    VideoSignalDescription signal;
    signal.pixelFormat = storage.softwareFormat;
    signal.colorPrimaries = ffmpegName(av_color_primaries_name(frame.color_primaries));
    signal.transferFunction = ffmpegName(av_color_transfer_name(frame.color_trc));
    signal.matrixCoefficients = ffmpegName(av_color_space_name(frame.colorspace));
    signal.colorRange = ffmpegName(av_color_range_name(frame.color_range));
    signal.chromaLocation = ffmpegName(av_chroma_location_name(frame.chroma_location));
    signal.componentDepth = componentDepth(signalDescriptor);
    signal.interlaced = frame.flags & AV_FRAME_FLAG_INTERLACED;
    if (!signal.isValid()) {
        return fail(QStringLiteral("Decoded frame signal description is invalid"));
    }

    AVFrame* retainedFrame = av_frame_clone(&frame);
    if (!retainedFrame) {
        return fail(QStringLiteral("Could not retain decoded frame storage"));
    }

    if (error) {
        error->clear();
    }
    return std::shared_ptr<DecodedVideoFrame const>(
        new DecodedVideoFrame(retainedFrame, identity, timing, geometry, storage, signal));
}

DecodedVideoFrame::DecodedVideoFrame(AVFrame* frame, VideoFrameIdentity identity, VideoFrameTiming timing,
                                     VideoFrameGeometry geometry, VideoFrameStorageDescription storage,
                                     VideoSignalDescription signal)
    : m_frame(frame), m_identity(identity), m_timing(std::move(timing)), m_geometry(std::move(geometry)),
      m_storage(std::move(storage)), m_signal(std::move(signal)) {}

DecodedVideoFrame::~DecodedVideoFrame() { av_frame_free(&m_frame); }

VideoFrameIdentity const& DecodedVideoFrame::identity() const { return m_identity; }

VideoFrameTiming const& DecodedVideoFrame::timing() const { return m_timing; }

VideoFrameGeometry const& DecodedVideoFrame::geometry() const { return m_geometry; }

VideoFrameStorageDescription const& DecodedVideoFrame::storage() const { return m_storage; }

VideoSignalDescription const& DecodedVideoFrame::signal() const { return m_signal; }

AVFrame const& DecodedVideoFrame::ffmpegFrame() const { return *m_frame; }
