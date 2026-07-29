# Media subsystem

## Status

Sunroom integrates the official vcpkg FFmpeg 8.1.2 package with `avformat`,
`avcodec`, and core `avutil`. The Windows package includes D3D11VA, D3D12VA,
DXVA2, and Media Foundation support; the application currently exercises only
software decoding. Hardware-device creation and D3D11 frame import are the next
media/video-rendering slice.

The current synchronous `decodeFirstVideoFrame()` boundary opens a local file,
discovers the best video stream, decodes the first presentable frame, makes its
stream metadata self-contained, and returns an immutable
`DecodedVideoFrame`. It is an integration proof, not the eventual continuous
decoder: it has no cancellation, worker ownership, packet queue, seeking, or
source-stall recovery. The caller supplies the complete frame identity; the
helper does not invent session generations or reusable frame IDs.

## Dependency boundary

The manifest requests:

```json
{
  "name": "ffmpeg",
  "default-features": false,
  "features": ["avcodec", "avformat"]
}
```

This deliberately excludes FFmpeg tools, `avfilter`, `avdevice`, `swscale`,
`swresample`, Vulkan, vendor SDKs, and external codec libraries. Native FFmpeg
demuxers and software decoders remain available. libplacebo owns video
conversion and scaling. Add `swresample` when the audio-output format
conversion boundary is implemented, and add codec libraries only for a
documented coverage or performance requirement.

The dependency is dynamically linked under the project clang-cl vcpkg triplet.
`avutil`, `avcodec`, and `avformat` DLLs are staged explicitly beside linked
build-tree targets and installed beside the application. This prevents loader
dialogs and avoids relying on CMake's transitive-runtime discovery for the
port's variable-style interface.

The selected FFmpeg configuration remains LGPL-oriented: GPL, version-3-only,
nonfree, x264, x265, and fdk-aac features are not enabled. Shipping still
requires notices, corresponding-source/build-recipe compliance, and an
explicit codec-patent review where applicable.

## Decoded-frame contract

`DecodedVideoFrame` retains a referenced `AVFrame`; it does not take ownership
of an FFmpeg decoder-pool texture or copy pixels by default. The retained frame
keeps software planes, native surfaces, hardware contexts, and exact side data
alive.

The Sunroom wrapper snapshots:

* Playback generation, decoder revision, and frame identity.
* PTS, duration, and time base.
* Coded/visible geometry, crop, aspect ratio, and rotation.
* Software or known hardware storage kind.
* Pixel-format and signal diagnostics.

Stream-level scalar color defaults, rotation, mastering-display, content-light,
and HDR10+ metadata are copied onto the private frame when the decoder did not
propagate them. Effective sample aspect ratio is resolved through FFmpeg while
the format and stream contexts remain alive.
Published frames do not retain or expose mutable format, stream, or decoder
contexts.

Hardware frame descriptions require the graphics-device generation that
created them and derive their signal format and component depth from
`AVHWFramesContext::sw_format`, not the opaque hardware pixel format. A frame
from a stale generation cannot be imported after device recreation.

The first-frame render fixture has square pixels. The presentation pipeline
does not yet apply decoded sample aspect ratio or orientation to an
aspect-preserving content rectangle; that geometry seam remains part of the
first Player-page/session slice.

## Next implementation

Continuous decoding will reuse this frame contract while adding:

1. Cancellable local-source ownership and stream discovery results.
2. Decoder worker and bounded packet/frame queues.
3. Standard FFmpeg hardware-config negotiation.
4. A D3D11VA hardware context created from Sunroom's video-capable shared
   D3D11 device.
5. Explicit hardware rejection and software-decoder fallback diagnostics.

The first-frame helper should disappear into that session rather than grow a
parallel mini-player API.

## Verification

The dependency test loads the three FFmpeg DLLs through CTest, verifies pinned
major versions, confirms D3D11VA and native H.264/HEVC decoders, and checks that
Vulkan and swscale remain disabled.

The decoded-frame unit test releases the originating `AVFrame` and verifies the
Sunroom wrapper still owns valid software pixels and semantic snapshots.

The first pipeline fixture is a pinned, hashed, lossless 4×4 PPM image. A real
FFmpeg demuxer and decoder produce RGB24, then the production libplacebo
software importer and QRhi compositor capture its known pixels. This proves
the integration boundary but is not yet representative compressed-video,
timeline, or hardware-decoding coverage.
