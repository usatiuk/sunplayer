#include "graphics/backends/D3D11LibplaceboFrameImporter.h"

#include <array>
#include <cstdint>
#include <limits>

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgiformat.h>
#include <libplacebo/d3d11.h>
#include <wrl/client.h>

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
using Microsoft::WRL::ComPtr;

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

        ComPtr<ID3D11DeviceContext> immediateContext;
        m_d3d11->device->GetImmediateContext(&immediateContext);
        if (immediateContext) {
            immediateContext.As(&m_context);
        }
        ComPtr<ID3D11Multithread> multithread;
        if (m_context && SUCCEEDED(m_context.As(&multithread))) {
            m_multithreadProtected = multithread->GetMultithreadProtected();
        }
    }

    ~D3D11LibplaceboFrameImporter() override { resetCache(); }

    bool map(DecodedVideoFrame const& frame, bool mapDolbyVision, pl_frame& mappedFrame,
             VideoFrameImportDiagnostics& diagnostics, VideoFrameImportFailure& failure, QString* error) override {
        failure = VideoFrameImportFailure::NativeHardwareImportUnavailable;
        auto const fail = [error](QString const& reason) {
            if (error) {
                *error = reason;
            }
            return false;
        };

        if (!m_d3d11 || !m_d3d11->device || !m_context) {
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
                QStringLiteral("D3D11 import does not support %1 surfaces").arg(frame.storage().softwareFormat));
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
            arraySlice >= static_cast<int>(description.ArraySize)) {
            return fail(QStringLiteral("The decoded D3D11 surface cannot be copied safely"));
        }

        VideoFrameGeometry const& geometry = frame.geometry();
        UINT const copyLeft = static_cast<UINT>(geometry.crop.left());
        UINT const copyTop = static_cast<UINT>(geometry.crop.top());
        UINT const copyWidth = static_cast<UINT>(geometry.visibleSize.width());
        UINT const copyHeight = static_cast<UINT>(geometry.visibleSize.height());
        if (((copyLeft | copyTop | copyWidth | copyHeight) & 1U) != 0) {
            return fail(QStringLiteral("The decoded D3D11 visible rectangle is not chroma-aligned"));
        }
        if (copyLeft + copyWidth > description.Width || copyTop + copyHeight > description.Height) {
            return fail(QStringLiteral("The decoded D3D11 surface is smaller than its aligned visible rectangle"));
        }

        pl_frame preparedFrame{};
        pl_frame_from_avframe(&preparedFrame, &avFrame);
        if (preparedFrame.num_planes != 2) {
            return fail(QStringLiteral("The decoded D3D11 surface did not describe two planes"));
        }
        preparedFrame.crop = {
            .x0 = 0.0f,
            .y0 = 0.0f,
            .x1 = static_cast<float>(geometry.visibleSize.width()),
            .y1 = static_cast<float>(geometry.visibleSize.height()),
        };
#ifdef PL_HAVE_LAV_DOLBY_VISION
        m_doviMetadata = {};
        if (AVFrameSideData const* dovi =
                mapDolbyVision ? av_frame_get_side_data(&avFrame, AV_FRAME_DATA_DOVI_METADATA) : nullptr) {
            if (dovi->size < sizeof(AVDOVIMetadata)) {
                failure = VideoFrameImportFailure::General;
                return fail(QStringLiteral("The decoded Dolby Vision metadata is truncated"));
            }
            pl_map_avdovi_metadata(&preparedFrame.color, &preparedFrame.repr, &m_doviMetadata,
                                   reinterpret_cast<AVDOVIMetadata const*>(dovi->data));
        }
#endif

        if (description.Format != m_cachedFormat || copyWidth != m_cachedWidth || copyHeight != m_cachedHeight) {
            resetCache();

            D3D11_TEXTURE2D_DESC copyDescription{};
            copyDescription.Width = copyWidth;
            copyDescription.Height = copyHeight;
            copyDescription.MipLevels = 1;
            copyDescription.ArraySize = 1;
            copyDescription.Format = description.Format;
            copyDescription.SampleDesc.Count = 1;
            copyDescription.Usage = D3D11_USAGE_DEFAULT;
            copyDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT const createResult =
                m_d3d11->device->CreateTexture2D(&copyDescription, nullptr, m_copyTexture.GetAddressOf());
            if (FAILED(createResult) || !m_copyTexture) {
                resetCache();
                return fail(QStringLiteral("Could not create the exact-size D3D11 video texture (0x%1)")
                                .arg(static_cast<unsigned long>(createResult), 8, 16, QLatin1Char('0')));
            }

            pl_d3d11_wrap_params luma{};
            luma.tex = m_copyTexture.Get();
            luma.fmt = formats.luma;
            luma.w = static_cast<int>(copyWidth);
            luma.h = static_cast<int>(copyHeight);
            m_copyPlanes[0] = pl_d3d11_wrap(m_gpu, &luma);

            pl_d3d11_wrap_params chroma{};
            chroma.tex = m_copyTexture.Get();
            chroma.fmt = formats.chroma;
            chroma.w = static_cast<int>(copyWidth / 2U);
            chroma.h = static_cast<int>(copyHeight / 2U);
            m_copyPlanes[1] = pl_d3d11_wrap(m_gpu, &chroma);
            if (!m_copyPlanes[0] || !m_copyPlanes[1]) {
                resetCache();
                return fail(QStringLiteral("Libplacebo could not wrap the exact-size D3D11 video planes"));
            }

            m_cachedFormat = description.Format;
            m_cachedWidth = copyWidth;
            m_cachedHeight = copyHeight;
        }

        D3D11_BOX const sourceBox{
            .left = copyLeft,
            .top = copyTop,
            .front = 0,
            .right = copyLeft + copyWidth,
            .bottom = copyTop + copyHeight,
            .back = 1,
        };
        m_context->CopySubresourceRegion1(m_copyTexture.Get(), 0, 0, 0, 0, texture, static_cast<UINT>(arraySlice),
                                          &sourceBox, D3D11_COPY_DISCARD);

        preparedFrame.planes[0].texture = m_copyPlanes[0];
        preparedFrame.planes[1].texture = m_copyPlanes[1];
        preparedFrame.repr.bits.sample_depth = m_copyPlanes[0]->params.format->component_depth[0];
        preparedFrame.repr.bits.color_depth = formats.colorDepth;
        preparedFrame.repr.bits.bit_shift = formats.bitShift;
        mappedFrame = preparedFrame;

        diagnostics = {
            .storageKind = frame.storage().kind,
            .path = VideoFrameImportPath::SameDeviceGpuCopy,
            .hardwareFormat = frame.storage().hardwareFormat,
            .softwareFormat = frame.storage().softwareFormat,
            .sourceDescription = frame.signal().summary(),
            .metadataPath = describeLibplaceboMetadataPath(frame, &mappedFrame),
            .nativeResource =
                QStringLiteral("DXGI %1 · array slice %2 · crop %3,%4 → %5x%6 exact-size %7/%8 plane views")
                    .arg(QString::fromLatin1(formats.resourceName), QString::number(arraySlice),
                         QString::number(copyLeft), QString::number(copyTop), QString::number(copyWidth),
                         QString::number(copyHeight),
                         formats.luma == DXGI_FORMAT_R8_UNORM ? QStringLiteral("R8") : QStringLiteral("R16"),
                         formats.chroma == DXGI_FORMAT_R8G8_UNORM ? QStringLiteral("R8G8") : QStringLiteral("R16G16")),
            .synchronizationMode = QStringLiteral("Shared D3D11 immediate context · "
                                                  "ordered GPU copy · ID3D11Multithread protection"),
            .knownCpuDownloadsPerFrame = 0,
            .knownCpuUploadsPerFrame = 0,
            .knownGpuCopiesPerFrame = 1,
            .fallbackReason = QStringLiteral("Decoder textures may expose undefined padding during filtered sampling"),
        };
        if (error) {
            error->clear();
        }
        return true;
    }

    void unmap(pl_frame&) override {
        // Copy and sampling are ordered on the shared immediate context. The
        // exact-size texture and its plane views are reused by the next frame.
    }

  private:
    void resetCache() {
        pl_tex_destroy(m_gpu, &m_copyPlanes[0]);
        pl_tex_destroy(m_gpu, &m_copyPlanes[1]);
        m_copyTexture.Reset();
        m_cachedFormat = DXGI_FORMAT_UNKNOWN;
        m_cachedWidth = 0;
        m_cachedHeight = 0;
    }

    pl_gpu m_gpu = nullptr;
    pl_d3d11 m_d3d11 = nullptr;
    ComPtr<ID3D11DeviceContext1> m_context;
    bool m_multithreadProtected = false;
    ComPtr<ID3D11Texture2D> m_copyTexture;
    std::array<pl_tex, 2> m_copyPlanes{};
    DXGI_FORMAT m_cachedFormat = DXGI_FORMAT_UNKNOWN;
    UINT m_cachedWidth = 0;
    UINT m_cachedHeight = 0;
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
