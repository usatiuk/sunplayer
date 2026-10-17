#include "graphics/backends/VulkanLibplaceboFrameImporter.h"

#include <cstdint>
#include <limits>

#include <QStringList>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
}

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

#include "video/libplacebo/LibplaceboFrameImporter.h"

namespace {
class VulkanLibplaceboFrameImporter final : public LibplaceboHardwareFrameImporter {
  public:
    explicit VulkanLibplaceboFrameImporter(pl_gpu gpu) : m_gpu(gpu) { Q_ASSERT(m_gpu); }

    ~VulkanLibplaceboFrameImporter() override { releaseRetired(true); }

    bool map(DecodedVideoFrame const& frame, bool mapDolbyVision, pl_frame& mappedFrame,
             VideoFrameImportDiagnostics& diagnostics, VideoFrameImportFailure& failure, QString* error) override {
        failure = VideoFrameImportFailure::NativeHardwareImportUnavailable;
        auto const fail = [error](QString const& reason) {
            if (error) {
                *error = reason;
            }
            return false;
        };
        releaseRetired(false);
        if (!(m_gpu->import_caps.tex & PL_HANDLE_DMA_BUF)) {
            return fail(QStringLiteral("The Vulkan device cannot import DRM PRIME dma-buf textures"));
        }
        AVFrame const& avFrame = frame.ffmpegFrame();
        if (frame.storage().kind != VideoFrameStorageKind::VaapiSurface || avFrame.format != AV_PIX_FMT_VAAPI ||
            !avFrame.hw_frames_ctx) {
            return fail(QStringLiteral("The Vulkan importer requires a retained VAAPI frame"));
        }

        // The helper exports separate DRM layers with READ | DIRECT, waits for
        // VA decode, imports the dma-bufs, and retains the source AVFrame.
        pl_avframe_params parameters{};
        parameters.frame = &avFrame;
        parameters.map_dovi = mapDolbyVision;
        if (!pl_map_avframe_ex(m_gpu, &mappedFrame, &parameters)) {
            return fail(QStringLiteral("VAAPI direct DRM PRIME export/import failed for %1; "
                                       "the driver, plane format or modifier is unsupported (see libplacebo log)")
                            .arg(frame.storage().softwareFormat));
        }

        AVFrame const* const mapped = pl_get_mapped_avframe(&mappedFrame);
        Q_ASSERT(mapped && mapped->format == AV_PIX_FMT_DRM_PRIME && mapped->data[0]);
        auto const& descriptor = *reinterpret_cast<AVDRMFrameDescriptor const*>(mapped->data[0]);
        QStringList planes;
        for (int index = 0; index < descriptor.nb_layers; ++index) {
            auto const& layer = descriptor.layers[index];
            auto const& plane = layer.planes[0];
            auto const& object = descriptor.objects[plane.object_index];
            planes.append(QStringLiteral("fourcc=0x%1 modifier=0x%2 pitch=%3 offset=%4")
                              .arg(layer.format, 0, 16)
                              .arg(object.format_modifier, 0, 16)
                              .arg(plane.pitch)
                              .arg(plane.offset));
        }
        diagnostics = {
            .storageKind = frame.storage().kind,
            .path = VideoFrameImportPath::DirectHardwareSurface,
            .hardwareFormat = frame.storage().hardwareFormat,
            .softwareFormat = frame.storage().softwareFormat,
            .sourceDescription = frame.signal().summary(),
            .metadataPath = describeLibplaceboMetadataPath(frame, &mappedFrame),
            .nativeResource = QStringLiteral("VAAPI %1 → DRM PRIME · %2")
                                  .arg(frame.storage().softwareFormat.toUpper(), planes.join(QStringLiteral(" · "))),
            .synchronizationMode = QStringLiteral("FFmpeg VA decode-completion wait · retained DRM PRIME AVFrame · "
                                                  "libplacebo GPU-completion polling"),
            .knownCpuDownloadsPerFrame = 0,
            .knownCpuUploadsPerFrame = 0,
            .knownGpuCopiesPerFrame = 0,
            .fallbackReason = {},
            .failure = VideoFrameImportFailure::None,
        };
        failure = VideoFrameImportFailure::None;
        if (error) {
            error->clear();
        }
        return true;
    }

    void unmap(pl_frame& mappedFrame) override {
        // Bound native ownership to the active mapping plus one prior mapping.
        // Dropping the AVFrame earlier lets the decoder reuse a busy surface.
        releaseRetired(true);
        m_retired = mappedFrame;
        mappedFrame = {};
    }

  private:
    void releaseRetired(bool wait) {
        if (!m_retired.user_data) {
            return;
        }
        for (int plane = 0; plane < m_retired.num_planes; ++plane) {
            if (wait) {
                while (pl_tex_poll(m_gpu, m_retired.planes[plane].texture, std::numeric_limits<std::uint64_t>::max())) {
                }
            } else if (pl_tex_poll(m_gpu, m_retired.planes[plane].texture, 0)) {
                return;
            }
        }
        pl_unmap_avframe(m_gpu, &m_retired);
    }

    pl_gpu m_gpu;
    pl_frame m_retired{};
};
} // namespace

std::unique_ptr<LibplaceboHardwareFrameImporter> createVulkanLibplaceboFrameImporter(pl_gpu gpu) {
    return std::make_unique<VulkanLibplaceboFrameImporter>(gpu);
}
