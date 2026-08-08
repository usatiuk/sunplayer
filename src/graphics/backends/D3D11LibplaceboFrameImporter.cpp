#include "graphics/backends/D3D11LibplaceboFrameImporter.h"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgiformat.h>
#include <libplacebo/d3d11.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

#include "media/DecodedVideoFrame.h"
#include "video/libplacebo/LibplaceboFrameImporter.h"

namespace {
struct PlaneFormats {
    DXGI_FORMAT resource = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT luma = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT chroma = DXGI_FORMAT_UNKNOWN;
    char const* resourceName = nullptr;
    int colorDepth = 0;
    int bitShift = 0;
};

bool planeFormats(enum AVPixelFormat softwareFormat, PlaneFormats& formats) {
    switch (softwareFormat) {
    case AV_PIX_FMT_NV12:
        formats = {
            .resource = DXGI_FORMAT_NV12,
            .luma = DXGI_FORMAT_R8_UNORM,
            .chroma = DXGI_FORMAT_R8G8_UNORM,
            .resourceName = "NV12",
            .colorDepth = 8,
            .bitShift = 0,
        };
        return true;
    case AV_PIX_FMT_P010:
        formats = {
            .resource = DXGI_FORMAT_P010,
            .luma = DXGI_FORMAT_R16_UNORM,
            .chroma = DXGI_FORMAT_R16G16_UNORM,
            .resourceName = "P010",
            .colorDepth = 10,
            .bitShift = 6,
        };
        return true;
    case AV_PIX_FMT_P012:
        formats = {
            .resource = DXGI_FORMAT_P016,
            .luma = DXGI_FORMAT_R16_UNORM,
            .chroma = DXGI_FORMAT_R16G16_UNORM,
            .resourceName = "P016 (P012 samples)",
            .colorDepth = 12,
            .bitShift = 4,
        };
        return true;
    case AV_PIX_FMT_P016:
        formats = {
            .resource = DXGI_FORMAT_P016,
            .luma = DXGI_FORMAT_R16_UNORM,
            .chroma = DXGI_FORMAT_R16G16_UNORM,
            .resourceName = "P016",
            .colorDepth = 16,
            .bitShift = 0,
        };
        return true;
    default:
        return false;
    }
}

class D3D11LibplaceboFrameImporter final : public LibplaceboHardwareFrameImporter {
  public:
    explicit D3D11LibplaceboFrameImporter(pl_gpu gpu) : m_gpu(gpu), m_d3d11(pl_d3d11_get(gpu)) {
        if (!m_d3d11 || !m_d3d11->device) {
            return;
        }

        ID3D11DeviceContext* context = nullptr;
        m_d3d11->device->GetImmediateContext(&context);
        ID3D11Multithread* multithread = nullptr;
        const HRESULT queryResult =
            context ? context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&multithread))
                    : E_NOINTERFACE;
        if (SUCCEEDED(queryResult) && multithread) {
            m_multithreadProtected = multithread->GetMultithreadProtected();
            multithread->Release();
        }
        if (context) {
            context->Release();
        }
    }

    ~D3D11LibplaceboFrameImporter() override { resetCache(); }

    bool map(DecodedVideoFrame const& frame, pl_frame& mappedFrame, VideoFrameImportDiagnostics& diagnostics,
             VideoFrameImportFailure& failure, QString* error) override {
        failure = VideoFrameImportFailure::NativeHardwareImportUnavailable;
        auto const fail = [error](QString const& reason) {
            if (error) {
                *error = reason;
            }
            return false;
        };

        if (!m_d3d11 || !m_d3d11->device) {
            return fail(QStringLiteral("The libplacebo D3D11 device is unavailable"));
        }
        if (!m_multithreadProtected) {
            return fail(QStringLiteral("The shared D3D11 immediate context is not "
                                       "multithread-protected"));
        }
        if (frame.storage().kind != VideoFrameStorageKind::D3D11Surface) {
            return fail(QStringLiteral("The D3D11 importer cannot map %1 frames").arg(frame.storage().hardwareFormat));
        }

        AVFrame const& avFrame = frame.ffmpegFrame();
        if (avFrame.format != AV_PIX_FMT_D3D11 || !avFrame.hw_frames_ctx || !avFrame.data[0]) {
            return fail(QStringLiteral("The decoded D3D11 frame has incomplete native storage"));
        }

        auto const* framesContext = reinterpret_cast<AVHWFramesContext const*>(avFrame.hw_frames_ctx->data);
        if (!framesContext || framesContext->format != AV_PIX_FMT_D3D11) {
            return fail(QStringLiteral("The decoded frame has no D3D11 frames context"));
        }

        PlaneFormats formats;
        if (!planeFormats(framesContext->sw_format, formats)) {
            return fail(
                QStringLiteral("D3D11 direct import does not support %1 surfaces").arg(frame.storage().softwareFormat));
        }

        auto* const texture = reinterpret_cast<ID3D11Texture2D*>(avFrame.data[0]);
        auto const sliceValue = reinterpret_cast<std::intptr_t>(avFrame.data[1]);
        if (sliceValue < 0 || sliceValue > std::numeric_limits<int>::max()) {
            return fail(QStringLiteral("The decoded D3D11 texture-array slice is invalid"));
        }
        int const arraySlice = static_cast<int>(sliceValue);

        ID3D11Device* textureDevice = nullptr;
        texture->GetDevice(&textureDevice);
        bool const sameDevice = textureDevice == m_d3d11->device;
        if (textureDevice) {
            textureDevice->Release();
        }
        if (!sameDevice) {
            return fail(QStringLiteral("The decoded D3D11 surface belongs to a different device"));
        }

        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (description.Usage != D3D11_USAGE_DEFAULT || description.Format != formats.resource ||
            description.MipLevels != 1 || description.SampleDesc.Count != 1 || description.Width == 0 ||
            description.Height == 0 || description.ArraySize == 0 || avFrame.width <= 0 || avFrame.height <= 0 ||
            static_cast<UINT>(avFrame.width) > description.Width ||
            static_cast<UINT>(avFrame.height) > description.Height ||
            arraySlice >= static_cast<int>(description.ArraySize) ||
            !(description.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
            return fail(QStringLiteral("The decoded D3D11 surface cannot be sampled directly"));
        }

        if (texture != m_cachedTexture || description.Format != m_cachedFormat || description.Width != m_cachedWidth ||
            description.Height != m_cachedHeight || description.ArraySize != m_slicePlanes.size()) {
            resetCache();
            m_cachedTexture = texture;
            m_cachedFormat = description.Format;
            m_cachedWidth = description.Width;
            m_cachedHeight = description.Height;
            m_slicePlanes.resize(description.ArraySize);
        }

        std::array<pl_tex, 2>& planes = m_slicePlanes[static_cast<std::size_t>(arraySlice)];
        if (!planes[0] || !planes[1]) {
            pl_d3d11_wrap_params luma{};
            luma.tex = texture;
            luma.array_slice = arraySlice;
            luma.fmt = formats.luma;
            luma.w = static_cast<int>(description.Width);
            luma.h = static_cast<int>(description.Height);
            planes[0] = pl_d3d11_wrap(m_gpu, &luma);

            pl_d3d11_wrap_params chroma{};
            chroma.tex = texture;
            chroma.array_slice = arraySlice;
            chroma.fmt = formats.chroma;
            chroma.w = static_cast<int>((description.Width + 1) / 2);
            chroma.h = static_cast<int>((description.Height + 1) / 2);
            planes[1] = pl_d3d11_wrap(m_gpu, &chroma);
            if (!planes[0] || !planes[1]) {
                pl_tex_destroy(m_gpu, &planes[0]);
                pl_tex_destroy(m_gpu, &planes[1]);
                return fail(QStringLiteral("Libplacebo could not wrap the decoded D3D11 planes"));
            }
        }

        pl_frame_from_avframe(&mappedFrame, &avFrame);
        if (mappedFrame.num_planes != 2) {
            mappedFrame = {};
            return fail(QStringLiteral("The decoded D3D11 surface did not describe two planes"));
        }
        mappedFrame.planes[0].texture = planes[0];
        mappedFrame.planes[1].texture = planes[1];
        mappedFrame.repr.bits.sample_depth = planes[0]->params.format->component_depth[0];
        mappedFrame.repr.bits.color_depth = formats.colorDepth;
        mappedFrame.repr.bits.bit_shift = formats.bitShift;
#ifdef PL_HAVE_LAV_DOLBY_VISION
        m_doviMetadata = {};
        if (AVFrameSideData const* dovi = av_frame_get_side_data(&avFrame, AV_FRAME_DATA_DOVI_METADATA)) {
            if (dovi->size < sizeof(AVDOVIMetadata)) {
                mappedFrame = {};
                failure = VideoFrameImportFailure::General;
                return fail(QStringLiteral("The decoded Dolby Vision metadata is truncated"));
            }
            pl_map_avdovi_metadata(&mappedFrame.color, &mappedFrame.repr, &m_doviMetadata,
                                   reinterpret_cast<AVDOVIMetadata const*>(dovi->data));
        }
#endif

        diagnostics = {
            .storageKind = frame.storage().kind,
            .path = VideoFrameImportPath::DirectHardwareSurface,
            .hardwareFormat = frame.storage().hardwareFormat,
            .softwareFormat = frame.storage().softwareFormat,
            .sourceDescription = frame.signal().summary(),
            .metadataPath = describeLibplaceboMetadataPath(frame, &mappedFrame),
            .nativeResource =
                QStringLiteral("DXGI %1 · array slice %2 · %3/%4 plane views")
                    .arg(QString::fromLatin1(formats.resourceName), QString::number(arraySlice),
                         formats.luma == DXGI_FORMAT_R8_UNORM ? QStringLiteral("R8") : QStringLiteral("R16"),
                         formats.chroma == DXGI_FORMAT_R8G8_UNORM ? QStringLiteral("R8G8") : QStringLiteral("R16G16")),
            .synchronizationMode = QStringLiteral("Shared D3D11 immediate context · "
                                                  "ID3D11Multithread protection · retained AVFrame slice"),
            .knownCpuDownloadsPerFrame = 0,
            .knownCpuUploadsPerFrame = 0,
            .knownGpuCopiesPerFrame = 0,
            .fallbackReason = {},
        };
        if (error) {
            error->clear();
        }
        return true;
    }

    void unmap(pl_frame&) override {
        // The retained DecodedVideoFrame reserves the decoder-pool slice.
        // Cached pl_tex wrappers own only D3D11 resource/view references.
    }

  private:
    void resetCache() {
        for (std::array<pl_tex, 2>& planes : m_slicePlanes) {
            pl_tex_destroy(m_gpu, &planes[0]);
            pl_tex_destroy(m_gpu, &planes[1]);
        }
        m_slicePlanes.clear();
        m_cachedTexture = nullptr;
        m_cachedFormat = DXGI_FORMAT_UNKNOWN;
        m_cachedWidth = 0;
        m_cachedHeight = 0;
    }

    pl_gpu m_gpu = nullptr;
    pl_d3d11 m_d3d11 = nullptr;
    bool m_multithreadProtected = false;
    ID3D11Texture2D* m_cachedTexture = nullptr;
    DXGI_FORMAT m_cachedFormat = DXGI_FORMAT_UNKNOWN;
    UINT m_cachedWidth = 0;
    UINT m_cachedHeight = 0;
    std::vector<std::array<pl_tex, 2>> m_slicePlanes;
#ifdef PL_HAVE_LAV_DOLBY_VISION
    struct pl_dovi_metadata m_doviMetadata{};
#endif
};
} // namespace

std::unique_ptr<LibplaceboHardwareFrameImporter> createD3D11LibplaceboFrameImporter(pl_gpu gpu) {
    if (!gpu) {
        return {};
    }
    return std::make_unique<D3D11LibplaceboFrameImporter>(gpu);
}
