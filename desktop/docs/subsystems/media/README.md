# Media subsystem

## Status

Sunroom integrates the official vcpkg FFmpeg 8.1.2 package with `avformat`,
`avcodec`, and core `avutil`. The Windows package includes D3D11VA, D3D12VA,
DXVA2, and Media Foundation support; the application currently exercises only
software decoding. Hardware-device creation and D3D11 frame import are the next
media/video-rendering slice.

The synchronous `decodeFirstVideoFrame()` operation opens a local file,
discovers the best video stream, decodes the first presentable frame, makes its
stream metadata self-contained, and returns an immutable
`DecodedVideoFrame`. `MediaSession` runs that operation on a worker with a
`std::stop_token`; an FFmpeg interrupt callback and decode-loop checks make
ordinary local-file work cooperatively cancellable. The caller still supplies
the complete frame identity.

This is an integration slice, not the eventual continuous decoder: it has no
packet queue, seeking, track discovery model, or source-stall recovery.
Uninterruptible mounted-filesystem kernel waits remain outside the guarantee
and may require helper-process containment.

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

`DecodedVideoSource` publishes the current immutable frame on the presentation
thread and derives its display aspect ratio from visible geometry and effective
sample aspect ratio. The presentation engine fits that ratio inside the
page-provided viewport. Quarter-turn orientation adjusts the fitted ratio;
rotated-content output still needs a dedicated fixture and capture.

## Next implementation

Continuous decoding will reuse this frame contract while adding:

1. Persistent demux/decoder worker ownership and bounded packet/frame queues.
2. Complete stream discovery and normalized session metadata.
3. Standard FFmpeg hardware-config negotiation.
4. A D3D11VA hardware context created from Sunroom's video-capable shared
   D3D11 device.
5. Explicit hardware rejection and software-decoder fallback diagnostics.

The first-frame operation is already owned by `MediaSession` through one
persistent, latest-request worker. Continuous decoding should evolve that
worker into bounded demux/decode queues rather than create a parallel
mini-player API.

## Verification

The dependency test loads the three FFmpeg DLLs through CTest, verifies pinned
major versions, confirms D3D11VA and native H.264/HEVC decoders, and checks that
Vulkan and swscale remain disabled.

The decoded-frame unit test releases the originating `AVFrame` and verifies the
Sunroom wrapper still owns valid software pixels and semantic snapshots.

The first pipeline fixture is a pinned, hashed, lossless 4×4 PPM image. A
second analytically generated fixture is a three-frame Matroska/FFV1 stream
with `yuv420p`, BT.709 primaries/transfer/matrix, limited range, left chroma
location, 4 fps timing, and SAR 32:27. Tests verify its exact decoded YUV
samples and tolerant linear-RGB libplacebo output. It proves deterministic
compressed software decode and YUV conversion, not hardware decoding.
