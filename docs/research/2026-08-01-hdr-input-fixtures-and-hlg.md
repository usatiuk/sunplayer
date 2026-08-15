# HDR input fixtures and HLG target behavior

Date: 2026-08-01

## Question

How can SunPlayer validate real FFmpeg/libplacebo input handling for HDR10/PQ,
HLG, HDR10+, and Dolby Vision without copyrighted media, a second parser, or a
custom HDR implementation, and what should V1 do about libplacebo's HLG target
semantics?

## Current project evidence

* One production FFmpeg operation opens, demuxes, and decodes the selected
  video. The retained final `AVFrame` is passed directly to libplacebo.
* The D3D11 path already captures analytic SDR/PQ rendering through the
  production RGBA16F target and final compositor.
* Synthetic decoder tests prove frame-local HDR10+ precedence and malformed
  Dolby Vision failure reporting, but no decoded dynamic-HDR media fixture is
  checked in.
* Libplacebo 7.360.1 currently receives a virtual destination maximum of
  `203 * physicalPeak / referenceWhite` so its normalized output range matches
  SunPlayer's display-relative working headroom.

## Pinned library facts

Libplacebo 7.360.1's default color-map parameters select spline tone mapping
and perceptual gamut mapping. SunPlayer currently disables inverse tone mapping,
peak detection, and dithering but inherits the first two choices from that
default structure. The pinned source explicitly sets an HLG source's maximum
luminance to an HDR destination's maximum during map inference, then uses that
maximum to derive the HLG OOTF system gamma.

Therefore, the current virtual destination makes HLG's OOTF respond to
display-relative headroom expressed in libplacebo's 203-nit coordinates, not
only to the monitor's reported physical peak. This is observable policy, not a
memory-safety or decode failure.

Primary source:

* [libplacebo 7.360.1 color-space inference](https://github.com/haasn/libplacebo/blob/v7.360.1/src/colorspace.c)
* [libplacebo 7.360.1 color-map parameters](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/shaders/colorspace.h)

## Fixture experiment

FFmpeg 8.1.2's libx265 encoder generated four-frame 10-bit BT.2020 HEVC patch
streams with static HDR10 mastering/content-light metadata or HLG transfer
signalling. Small generation-only tools then injected dynamic metadata:

* `hdr10plus_tool` 1.7.2 injected a hand-authored two-scene Profile B JSON
  sequence. Its extraction command recovered four frame-local records and the
  intended scene change.
* `dovi_tool` 2.3.3 generated and injected a generic Profile 8.1 RPU sequence
  over the HDR10 base layer.
* FFprobe 8.1.2 decoded mastering metadata, content-light metadata, frame-local
  HDR10+ side data, raw Dolby Vision RPU data, and parsed Dolby Vision metadata
  from the resulting HEVC elementary streams.

The encoded HEVC streams are deterministic across repeated generation with the
pinned tools. Matroska remuxes were not byte-stable because the muxer generated
container identifiers, so the checked-in acceptance fixtures are the tiny raw
streams. Existing Matroska fixtures independently cover real container routing
and seeking; the raw streams still cross production `avformat` demux and
`avcodec` decode while preserving every relevant SPS, SEI, and RPU payload.

Two clean runs of the manifest commands produced identical fixture hashes:

| Fixture | SHA-256 |
| --- | --- |
| Static PQ | `0179f288a48cfa5e57cd10563f21fb1574f2d375d07dfdba53c6313b8728f3c0` |
| HLG | `cc9130061a0f3dfab4457fe82bac8154cb918df57e4bd369c396080d68dec713` |
| HDR10+ | `83f1bb09698f1e6e3e7f19ecf364c89c17e486e72b7ea34a555548a81c4db20c` |
| Dolby Vision Profile 8.1 | `3bd33c21897a8cbc4628819df84de3571caeb0a46fcef1427145fc162f49eca8` |

Both tools document injection into HEVC, and `dovi_tool` documents generating
Profile 8.1 RPU data from a small JSON configuration:

* [hdr10plus_tool commands](https://github.com/quietvoid/hdr10plus_tool)
* [dovi_tool generation and injection](https://github.com/quietvoid/dovi_tool)
* [FFmpeg HDR10+ side-data API](https://ffmpeg.org/doxygen/8.0/hdr__dynamic__metadata_8h.html)

The validated Windows release artifacts are generation tools only and do not
become SunPlayer build or runtime dependencies:

| Tool | Release | SHA-256 |
| --- | --- | --- |
| dovi_tool | 2.3.3 x86_64-pc-windows-msvc | `37ae198f2a535c910befad39fc09c21cded76bf3ef2d5459d542e58c2c158311` |
| hdr10plus_tool | 1.7.2 x86_64-pc-windows-msvc | `82b2d560073941b14c6511a431f429e33e134e5caefb60d7e8f6f6e6da8e16ba` |

## V1 policy consequence

SunPlayer will keep the current display-relative HLG behavior for the first
format-acceptance milestone and characterize it with production-boundary
captures at multiple reference-white/headroom targets. It will not add a
second HLG stage or patch libplacebo merely to create a separate physical-peak
coordinate.

This is intentionally a limited claim: HLG decodes and adapts through the same
renderer, and its response to available headroom is regression-tested, but it
is not an absolute-reference HLG monitoring mode. If later physical
measurements or interoperability tests demonstrate an unacceptable result,
the next action is a focused upstream API proposal separating HLG's physical
OOTF peak from destination working coordinates.

Peak detection remains disabled. Because no temporal peak-detection or frame-
mixing state is active, the milestone will not add a no-op seek/reset hook.

## Test consequences

* Check in only tiny analytically generated HEVC streams and their source
  metadata JSON/manifests, not the generation tools or nondeterministic remuxes.
* Decode each fixture through the production FFmpeg boundary and render through
  the persistent libplacebo/QRhi D3D11 path.
* Use known neutral patches for static-PQ numeric checks and property checks
  for HLG/dynamic HDR: finite output, monotonic neutral ordering, target bounds,
  target response, frame-local metadata changes, and honest mapping/fallback
  diagnostics.
* Retain the existing synthetic metadata test because it independently proves
  stream fallback versus frame-local precedence without relying on one encoded
  sample.
