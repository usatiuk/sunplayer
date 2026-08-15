#define VK_USE_PLATFORM_METAL_EXT 1

#include "graphics/backends/MetalLibplaceboFrameImporter.h"

#include <array>
#include <cstdint>
#include <vector>

#import <CoreVideo/CoreVideo.h>

#include <QtCore/qlogging.h>
#include <libplacebo/vulkan.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_videotoolbox.h>
}

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

#include "diagnostics/LogCategories.h"
#include "media/DecodedVideoFrame.h"
#include "video/libplacebo/LibplaceboFrameImporter.h"

namespace {
struct PlaneFormats {
    MTLPixelFormat luma = MTLPixelFormatInvalid;
    MTLPixelFormat chroma = MTLPixelFormatInvalid;
    const char *lumaName = nullptr;
    const char *chromaName = nullptr;
    int colorDepth = 0;
    int bitShift = 0;
};

bool planeFormats(
        enum AVPixelFormat softwareFormat,
        PlaneFormats &formats) {
    switch (softwareFormat) {
    case AV_PIX_FMT_NV12:
        formats = {
            .luma = MTLPixelFormatR8Unorm,
            .chroma = MTLPixelFormatRG8Unorm,
            .lumaName = "r8",
            .chromaName = "rg8",
            .colorDepth = 8,
            .bitShift = 0,
        };
        return true;
    case AV_PIX_FMT_P010:
        formats = {
            .luma = MTLPixelFormatR16Unorm,
            .chroma = MTLPixelFormatRG16Unorm,
            .lumaName = "r16",
            .chromaName = "rg16",
            .colorDepth = 10,
            .bitShift = 6,
        };
        return true;
    default:
        return false;
    }
}

class MetalLibplaceboFrameImporter final
    : public LibplaceboHardwareFrameImporter {
public:
    MetalLibplaceboFrameImporter(
            pl_vulkan vulkan,
            id<MTLDevice> metalDevice)
        : m_gpu(vulkan->gpu),
          m_metalDevice(metalDevice) {
        Q_ASSERT(m_gpu);
        Q_ASSERT(m_metalDevice);
        const CVReturn result = CVMetalTextureCacheCreate(
            kCFAllocatorDefault,
            nullptr,
            m_metalDevice,
            nullptr,
            &m_textureCache);
        if (result != kCVReturnSuccess)
            m_textureCache = nullptr;
    }

    ~MetalLibplaceboFrameImporter() override {
        pl_gpu_finish(m_gpu);
        releaseActive();
        for (PendingFrame &pending : m_pending)
            release(pending);
        m_pending.clear();
        if (m_textureCache)
            CFRelease(m_textureCache);
    }

    bool map(
            const DecodedVideoFrame &frame,
            pl_frame &mappedFrame,
            VideoFrameImportDiagnostics &diagnostics,
            VideoFrameImportFailure &failure,
            QString *error) override {
        failure = VideoFrameImportFailure::
            NativeHardwareImportUnavailable;
        const auto fail = [error](const QString &reason) {
            if (error)
                *error = reason;
            return false;
        };

        collectCompleted();
        Q_ASSERT(!m_active.pixelBuffer);
        Q_ASSERT(!m_active.textures[0]);
        Q_ASSERT(!m_active.textures[1]);

        if (!m_textureCache) {
            return fail(QStringLiteral(
                "CoreVideo could not create a Metal texture cache"));
        }
        if (frame.storage().kind
                != VideoFrameStorageKind::VideoToolboxSurface) {
            return fail(QStringLiteral(
                "The VideoToolbox importer cannot map %1 frames")
                .arg(frame.storage().hardwareFormat));
        }

        const AVFrame &avFrame = frame.ffmpegFrame();
        if (avFrame.format != AV_PIX_FMT_VIDEOTOOLBOX
                || !avFrame.hw_frames_ctx
                || !avFrame.data[3]) {
            return fail(QStringLiteral(
                "The decoded VideoToolbox frame has incomplete native storage"));
        }
        const auto *const framesContext =
            reinterpret_cast<const AVHWFramesContext *>(
                avFrame.hw_frames_ctx->data);
        if (!framesContext
                || framesContext->format
                    != AV_PIX_FMT_VIDEOTOOLBOX) {
            return fail(QStringLiteral(
                "The decoded frame has no VideoToolbox frames context"));
        }

        PlaneFormats formats;
        if (!planeFormats(framesContext->sw_format, formats)) {
            return fail(QStringLiteral(
                "VideoToolbox direct import does not support %1 surfaces")
                .arg(frame.storage().softwareFormat));
        }

        auto pixelBuffer = reinterpret_cast<CVPixelBufferRef>(
            avFrame.data[3]);
        const OSType pixelFormat =
            CVPixelBufferGetPixelFormatType(pixelBuffer);
        if (av_map_videotoolbox_format_to_pixfmt(pixelFormat)
                    != framesContext->sw_format
                || CVPixelBufferGetPlaneCount(pixelBuffer) != 2) {
            return fail(QStringLiteral(
                "The VideoToolbox pixel buffer does not match its FFmpeg "
                "plane description"));
        }

        CFRetain(pixelBuffer);
        m_active.pixelBuffer = pixelBuffer;
        const std::array metalFormats{
            formats.luma,
            formats.chroma,
        };
        const std::array formatNames{
            formats.lumaName,
            formats.chromaName,
        };
        for (std::size_t plane = 0;
                plane < m_active.textures.size();
                ++plane) {
            const std::size_t width =
                CVPixelBufferGetWidthOfPlane(pixelBuffer, plane);
            const std::size_t height =
                CVPixelBufferGetHeightOfPlane(pixelBuffer, plane);
            if (width == 0 || height == 0) {
                releaseActive();
                return fail(QStringLiteral(
                    "The VideoToolbox pixel buffer has an empty plane"));
            }

            CVMetalTextureRef metalTexture = nullptr;
            const CVReturn textureResult =
                CVMetalTextureCacheCreateTextureFromImage(
                    kCFAllocatorDefault,
                    m_textureCache,
                    pixelBuffer,
                    nullptr,
                    metalFormats[plane],
                    width,
                    height,
                    plane,
                    &metalTexture);
            if (textureResult != kCVReturnSuccess
                    || !metalTexture) {
                releaseActive();
                return fail(QStringLiteral(
                    "CoreVideo could not expose VideoToolbox plane %1 as Metal")
                    .arg(plane));
            }
            m_active.metalTextures[plane] = metalTexture;

            id<MTLTexture> texture =
                CVMetalTextureGetTexture(metalTexture);
            if (!texture
                    || texture.device != m_metalDevice
                    || texture.pixelFormat != metalFormats[plane]
                    || texture.width != width
                    || texture.height != height) {
                releaseActive();
                return fail(QStringLiteral(
                    "CoreVideo exposed an incompatible Metal plane texture"));
            }

            const pl_fmt format =
                pl_find_named_fmt(m_gpu, formatNames[plane]);
            if (!format || !(format->caps & PL_FMT_CAP_SAMPLEABLE)) {
                releaseActive();
                return fail(QStringLiteral(
                    "Libplacebo cannot sample VideoToolbox plane format %1")
                    .arg(QString::fromLatin1(formatNames[plane])));
            }

            pl_tex_params parameters{};
            parameters.w = static_cast<int>(width);
            parameters.h = static_cast<int>(height);
            parameters.format = format;
            parameters.sampleable = true;
            parameters.import_handle = PL_HANDLE_MTL_TEX;
            parameters.shared_mem.handle.handle =
                (__bridge void *)texture;
            parameters.debug_tag = plane == 0
                ? "SunPlayer VideoToolbox luma plane"
                : "SunPlayer VideoToolbox chroma plane";
            m_active.textures[plane] =
                pl_tex_create(m_gpu, &parameters);
            if (!m_active.textures[plane]
                    || !m_active.textures[plane]
                        ->params.sampleable) {
                releaseActive();
                return fail(QStringLiteral(
                    "Libplacebo could not import VideoToolbox plane %1")
                    .arg(plane));
            }
        }

        pl_frame_from_avframe(&mappedFrame, &avFrame);
        if (mappedFrame.num_planes != 2) {
            mappedFrame = {};
            releaseActive();
            return fail(QStringLiteral(
                "The VideoToolbox surface did not describe two planes"));
        }
        mappedFrame.planes[0].texture = m_active.textures[0];
        mappedFrame.planes[1].texture = m_active.textures[1];
        mappedFrame.repr.bits.sample_depth =
            m_active.textures[0]
                ->params.format->component_depth[0];
        mappedFrame.repr.bits.color_depth = formats.colorDepth;
        mappedFrame.repr.bits.bit_shift = formats.bitShift;
#ifdef PL_HAVE_LAV_DOLBY_VISION
        m_doviMetadata = {};
        if (const AVFrameSideData *dovi = av_frame_get_side_data(
                &avFrame, AV_FRAME_DATA_DOVI_METADATA)) {
            if (dovi->size < sizeof(AVDOVIMetadata)) {
                mappedFrame = {};
                releaseActive();
                failure = VideoFrameImportFailure::General;
                return fail(QStringLiteral(
                    "The decoded Dolby Vision metadata is truncated"));
            }
            pl_map_avdovi_metadata(
                &mappedFrame.color,
                &mappedFrame.repr,
                &m_doviMetadata,
                reinterpret_cast<const AVDOVIMetadata *>(
                    dovi->data));
        }
#endif

        diagnostics = {
            .storageKind = frame.storage().kind,
            .path = VideoFrameImportPath::DirectHardwareSurface,
            .hardwareFormat = frame.storage().hardwareFormat,
            .softwareFormat = frame.storage().softwareFormat,
            .sourceDescription = frame.signal().summary(),
            .metadataPath = describeLibplaceboMetadataPath(
                frame, &mappedFrame),
            .nativeResource = QStringLiteral(
                "VideoToolbox %1 CVPixelBuffer · Metal %2/%3 planes")
                .arg(
                    frame.storage().softwareFormat.toUpper(),
                    QString::fromLatin1(formats.lumaName).toUpper(),
                    QString::fromLatin1(formats.chromaName).toUpper()),
            .synchronizationMode = QStringLiteral(
                "Retained AVFrame/CVPixelBuffer and CVMetalTexture planes · "
                "libplacebo GPU-completion polling"),
            .knownCpuDownloadsPerFrame = 0,
            .knownCpuUploadsPerFrame = 0,
            .knownGpuCopiesPerFrame = 0,
            .fallbackReason = {},
            .failure = VideoFrameImportFailure::None,
        };
        if (error)
            error->clear();
        return true;
    }

    void unmap(pl_frame &) override {
        Q_ASSERT(m_active.pixelBuffer);
        m_pending.push_back(m_active);
        m_active = {};
    }

private:
    struct PendingFrame {
        CVPixelBufferRef pixelBuffer = nullptr;
        std::array<CVMetalTextureRef, 2> metalTextures{};
        std::array<pl_tex, 2> textures{};
    };

    void collectCompleted() {
        auto pending = m_pending.begin();
        while (pending != m_pending.end()) {
            bool inUse = false;
            for (pl_tex texture : pending->textures)
                inUse = inUse || pl_tex_poll(m_gpu, texture, 0);
            if (inUse) {
                ++pending;
                continue;
            }
            release(*pending);
            pending = m_pending.erase(pending);
        }
    }

    void releaseActive() {
        release(m_active);
        m_active = {};
    }

    void release(PendingFrame &frame) {
        for (pl_tex &texture : frame.textures)
            pl_tex_destroy(m_gpu, &texture);
        for (CVMetalTextureRef &texture : frame.metalTextures) {
            if (texture)
                CFRelease(texture);
            texture = nullptr;
        }
        if (frame.pixelBuffer)
            CFRelease(frame.pixelBuffer);
        frame.pixelBuffer = nullptr;
    }

    pl_gpu m_gpu = nullptr;
    id<MTLDevice> m_metalDevice = nil;
    CVMetalTextureCacheRef m_textureCache = nullptr;
    PendingFrame m_active;
    std::vector<PendingFrame> m_pending;
#ifdef PL_HAVE_LAV_DOLBY_VISION
    struct pl_dovi_metadata m_doviMetadata{};
#endif
};
}

std::unique_ptr<LibplaceboHardwareFrameImporter>
createMetalLibplaceboFrameImporter(
        pl_vulkan vulkan,
        id<MTLDevice> metalDevice) {
    if (!vulkan || !vulkan->gpu || !metalDevice)
        return {};
    return std::make_unique<MetalLibplaceboFrameImporter>(
        vulkan, metalDevice);
}
