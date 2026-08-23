# FFmpeg Windows dependency and frame-import research

* Date: 2026-07-29
* Status: Dependency and software mapping validated. The initial D3D11VA
  zero-copy conclusion was superseded by the
  [2026-08-23 edge-corruption diagnosis](2026-08-23-windows-d3d11va-edge-corruption.md).

## Official vcpkg package

At SunPlayer's pinned vcpkg baseline
`9d7f79f56ae1a9b4704d6a7fb8237e347a974133`, the official FFmpeg port resolves
to 8.1.2#3.

The first media slice needs only:

```json
{
  "name": "ffmpeg",
  "default-features": false,
  "features": ["avcodec", "avformat"]
}
```

`avutil` is always present. The Windows port enables D3D11VA, D3D12VA, DXVA2,
and Media Foundation independent of manifest features. No Vulkan SDK, vendor
decode SDK, external codec library, or overlay port is required for D3D11VA.

`swscale` is unnecessary because libplacebo owns video format conversion and
scaling. `swresample` should be added when decoded audio must be converted to
the chosen sink format. FFmpeg's `ass` feature is unrelated to SunPlayer's
planned direct libass subtitle renderer.

The official CMake integration exposes configuration-aware include and library
variables, but not runtime-aware imported component targets or a portable DLL
list. Qt also ships a case-variant `FindFFmpeg.cmake` with different behavior.
SunPlayer therefore performs vcpkg FFmpeg discovery before Qt, wraps each Windows
component's Debug/Release import library and DLL in an imported target, and
contains those targets behind `sunplayer_ffmpeg`.

## Software-frame mapping

Pinned libplacebo 7.360.1 provides
`libplacebo/utils/libav.h`. `pl_map_avframe_ex()` retains the source `AVFrame`,
maps its color/crop/side-data semantics, uploads software planes, and uses a
caller-owned `pl_tex[4]` array intended for reuse. `pl_unmap_avframe()` releases
transient state but does not destroy those reusable textures.

The helper implementation is emitted once from a C translation unit with
`PL_LIBAV_IMPLEMENTATION=1`; C++ users include declarations with the macro set
to zero.

The helper directly supports software frames plus DRM PRIME, VAAPI-derived DRM,
and Vulkan hardware frames. The pinned implementation has no
`AV_PIX_FMT_D3D11` branch, so Windows requires a native importer.

## D3D11VA frame contract

For an FFmpeg D3D11 frame:

* `AVFrame::data[0]` identifies the `ID3D11Texture2D`.
* `AVFrame::data[1]` identifies its texture-array slice.
* `hw_frames_ctx` retains the pool and reports the software plane format,
  normally NV12, P010, P012, or P016.

The implemented D3D11VA hardware context uses SunPlayer's graphics-domain device
rather than creating a second adapter/device. Decoder surfaces remain ordinary
copy sources; only SunPlayer's exact-size destination requests shader-resource
binding.

The initial native importer wrapped each decoder plane and array slice directly
through `pl_d3d11_wrap()`. That proved the format mapping but was not a safe
playback default because decoder textures can be padded. The corrected importer
copies the active chroma-aligned rectangle into a cached exact-size D3D11
texture, then wraps these same plane formats:

| Storage | Luma view | Chroma view |
| --- | --- | --- |
| NV12 | R8_UNORM | R8G8_UNORM |
| P010/P012/P016 | R16_UNORM | R16G16_UNORM |

P010, P012, and P016 require explicit 10-, 12-, and 16-bit effective color
depth. P010 and P012 use six- and four-bit high-bit storage shifts.

## Validated shared-device synchronization

QRhi, libplacebo, and FFmpeg will share one D3D11 immediate context. FFmpeg's
hardware-context lock callbacks protect FFmpeg calls only; they do not
automatically serialize Qt or libplacebo.

The implemented Windows policy is:

1. The graphics domain creates the D3D11 device with video support.
2. It enables `ID3D11Multithread` protection on the immediate context.
3. It imports that device/context into QRhi and gives the same device to
   libplacebo and FFmpeg.
4. The retained `AVFrame` reserves its pool slice.
5. The importer orders one exact-size GPU copy from the retained decoder slice
   before libplacebo reads the copy on the same immediate context.
6. The frame reference remains valid while that copy is ordered.

The graphics domain also exposes one recursive execution scope used by
FFmpeg's hardware-context callbacks and the engine's QRhi/libplacebo resource,
command, and teardown phases. Device-independent source selection, geometry,
and display policy execute outside that scope. This provides explicit
sequence-level ordering on top of D3D11's per-call multithread protection. The
retained `AVFrame` keeps the texture-array slice reserved while the copy is
ordered; cached libplacebo views refer only to SunPlayer's exact-size texture.

Pinned H.264/NV12 and Main10 HEVC/P010 scenarios validate device and slice
checks, `R8_UNORM`/`R8G8_UNORM` and `R16_UNORM`/`R16G16_UNORM` plane wrapping,
one same-device input copy, zero input CPU transfers, and four-edge output
agreement with software decode. P012/P016 capture, continuous decode
contention, affected production-file playback, and device-loss recovery still
require verification.

## Sources

* [Pinned vcpkg FFmpeg manifest](https://github.com/microsoft/vcpkg/blob/9d7f79f56ae1a9b4704d6a7fb8237e347a974133/ports/ffmpeg/vcpkg.json)
* [Pinned vcpkg FFmpeg portfile](https://github.com/microsoft/vcpkg/blob/9d7f79f56ae1a9b4704d6a7fb8237e347a974133/ports/ffmpeg/portfile.cmake)
* [FFmpeg D3D11VA context](https://ffmpeg.org/doxygen/8.0/hwcontext__d3d11va_8h_source.html)
* [FFmpeg hardware-context API](https://ffmpeg.org/doxygen/8.0/hwcontext_8h.html)
* [Pinned libplacebo FFmpeg helper](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/utils/libav.h)
* [Qt D3D11 native handles](https://doc.qt.io/qt-6/qrhid3d11nativehandles.html)
* [Qt QRhi threading and ownership](https://doc.qt.io/qt-6/qrhi.html)
* [ID3D11Multithread](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nn-d3d11_4-id3d11multithread)
