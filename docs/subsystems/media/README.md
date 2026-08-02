# Media subsystem

## Status

Sunroom integrates FFmpeg through one shared media boundary: the official
vcpkg FFmpeg 8.1.2 package on Windows and macOS, and system FFmpeg 8.0.1 on
Linux. All
provide `avformat`, `avcodec`, core `avutil`, `swresample`, and zlib-backed
Matroska track decompression. The Windows package includes D3D11VA, D3D12VA,
DXVA2, and Media Foundation support. Sunroom exercises D3D11VA for supported
Windows video streams and keeps software decoding as an explicit fallback.
The macOS package exposes VideoToolbox; Sunroom exercises H.264/NV12 and
Main10 HEVC/P010 hardware decode and direct Metal-plane import on Apple
Silicon.
Linux currently uses the same production operation with software video decode;
the system FFmpeg exposes VAAPI/DRM capability, but native-frame import is not
implemented yet.

The production `decodeMediaFrames()` operation accepts one restartable request,
opens a local file, discovers selected video/audio streams and every embedded
subtitle track, and continuously emits immutable media values.
`decodeFirstVideoFrame()` is now only a focused-test
adapter over that same implementation, so hardware negotiation, metadata,
timestamp, EOF, and fallback behavior do not diverge.

Each operation has two FFmpeg owners:

* A demux worker exclusively owns `AVFormatContext`.
* Its caller/decoder worker exclusively owns `AVCodecContext` and the hardware
  frame pool.

They exchange only selected-video `AVPacket`s through a channel bounded to 64
packets and four encoded megabytes. One packet larger than that byte budget is
allowed only when the channel is otherwise empty. End of input is ordered
behind the last packet and causes one null packet to be sent followed by
`avcodec_receive_frame()` until `AVERROR_EOF`. Send-side `EAGAIN` retains and
retries the exact packet after receiving output; a double-EAGAIN no-progress
state is reported rather than spun.

The Windows graphics domain creates an initialized FFmpeg D3D11VA context from
the same application-owned D3D11 device used by QRhi and libplacebo. The
continuous operation enumerates the selected decoder's hardware
configurations, requests the matching hardware pixel format, and records the
graphics-device generation on returned hardware frames. Its extra hardware
frame reserve covers three queued frames, the current selected frame, a
decoder output blocked on the queue, and the producer's transient prior
mapping during a frame switch. A configured hardware failure retries in
software only before any frame has been published; a later decoder failure is
visible rather than silently replaying from the beginning.

On Linux the graphics capability currently reports software decode. The
software `AVFrame` path is shared with Windows and renders through the Vulkan
libplacebo target. VAAPI/DRM PRIME negotiation and native image import remain
one future acceleration slice rather than a second media pipeline.

On macOS the graphics domain creates an FFmpeg VideoToolbox device capability
tagged with the Metal-domain generation. Hardware frames retain their
`AVHWFramesContext` and `CVPixelBuffer`; the native importer exposes NV12 as
R8/RG8 Metal planes and P010 as R16/RG16 planes on QRhi's `MTLDevice`.
Libplacebo samples those planes directly, while nonblocking GPU-completion
polling controls release. Unsupported hardware formats use the same bounded
whole-operation software restart as Windows rather than a per-frame download.

If the retained hardware surface later cannot be imported by the active
graphics backend, `MediaSession` consumes at most one software-only re-decode
for that open and records the import reason. A repeated typed failure becomes a
session error. Graphics-device recreation cancels or supersedes in-flight work
and published frames, then re-decodes from the captured logical position after
the replacement domain supplies its capability. Software `AVFrame` storage is
generation-independent, but restarting the active pipeline prevents later
seeks or fallback from retaining an obsolete graphics capability.

The operation and both bounded channels are cooperatively cancellable through
`std::stop_token`. The FFmpeg interrupt state outlives format teardown, and
every blocking queue wait includes stop or generation invalidation.
For an explicit targeted start, including a seek to zero, the operation
can request demux positioning separately from presentation-target filtering.
A seekable source disables unselected streams, translates the normalized
target from the stable exact origin into the selected stream time base, and
calls `avformat_seek_file()` with a keyframe-constrained range ending at the
requested timestamp. Initial open, and a nonseekable restart at zero, read
naturally. The target remains explicit in the latter case so negative-timestamp
preroll is still filtered. Positioning occurs after probing and stream
selection but before `AVFormatContext` moves to the demux worker. Fresh
demux/codec contexts need no explicit flush. Decoded preroll is retained
through codec dependency processing and filtered at the playback boundary.
The absolute target uses one checked 64-bit rescale and addition; it is not
passed through an incremental rational clock helper.

Uninterruptible mounted-filesystem kernel waits remain outside the guarantee
and may require helper-process containment.

The production session uses `decodeMediaFrames()`: it opens and probes one
source once, routes referenced packets for selected video and audio streams
under one shared count/byte budget, decodes both on their own workers, and
converts audio through libswresample. The active graphics capability passes
through the same hardware-capable packet-level video decoder, so codec setup,
D3D11VA negotiation, metadata, frame ownership, drain, and whole-operation
software fallback do not fork. The deterministic synchronized regression uses
software frames, while production may select hardware decode. Neither path
opens a second format context for audio.

The shared A/V packet router defaults to 8,192 packets and 512 MiB. This queue
is after FFmpeg source reads and demuxing: it is bounded encoded-packet
backpressure, not a byte-level source cache. The deliberately generous reserve
lets the single FFmpeg operation read well ahead through interleaved audio and
video on typical high-bitrate UHD media.
Remote URL input, duration-aware read-ahead, source-stall behavior, and the
evidence-gated custom-AVIO direction are documented in
[media input and source buffering](../media-io/README.md).

At open time, FFmpeg's public duration fields are treated as provisional
durations and `start_time` is never subtracted from them. At successful EOF,
the selected A/V operation finalizes duration from the maximum observed
normalized stream endpoint. This avoids a second file read and handles
containers whose declared duration includes a leading empty timeline interval.

Selected subtitle packets share the same aggregate packet budget and are
decoded by their own worker. Track discovery, selection, embedded-font
delivery, text/ASS output, and copied bitmap compositions are described in the
[subtitle subsystem](../subtitles/README.md). Remote input and source-stall
recovery remain unimplemented.

## Dependency boundary

The Windows and macOS vcpkg manifest requests:

```json
{
  "name": "ffmpeg",
  "default-features": false,
  "features": ["avcodec", "avformat", "swresample", "zlib"]
}
```

This deliberately excludes FFmpeg tools, `avfilter`, `avdevice`, `swscale`,
Vulkan, vendor SDKs, and external codec libraries beyond zlib. zlib is required
for Matroska track `ContentCompression`; it is not a second media reader or a
Sunroom-authored codec path. Native FFmpeg demuxers and software decoders remain
available. libplacebo owns video conversion and
scaling; libswresample owns audio sample-format, rate, layout, and
planar/interleaved conversion. Add codec libraries only for a documented
coverage or performance requirement.

On Windows the dependency is dynamically linked under the project clang-cl
vcpkg triplet. On macOS it is built under stock `arm64-osx` and linked into the
development bundle; final deployment ownership is deferred to packaging.
`avutil`, `swresample`, `avcodec`, and `avformat` DLLs are staged explicitly
beside linked build-tree targets and installed beside the application. This
prevents loader dialogs and avoids relying on CMake's transitive-runtime
discovery for the port's variable-style interface.

The selected FFmpeg configuration remains LGPL-oriented: GPL, version-3-only,
nonfree, x264, x265, and fdk-aac features are not enabled. Shipping still
requires notices, corresponding-source/build-recipe compliance, and an
explicit codec-patent review where applicable.

Linux discovers the corresponding system libraries with pkg-config and checks
their accepted ABI-major ranges at configure and dependency-test time. It does
not require the Windows package's disabled-Vulkan feature shape; system FFmpeg
may expose VAAPI and DRM even though Sunroom does not yet consume that hardware
path.

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

The retained decoded frame is the authoritative color boundary. The supported
FFmpeg builds already fill ordinary scalar and static side data from
decoder/stream state.
Sunroom therefore removed its blanket metadata copier. It snapshots only
global HDR10+ data that FFmpeg does not reliably propagate to every frame;
missing HDR10+ is attached before the decoded frame is retained and handed to
libplacebo. Effective sample
aspect ratio prefers the decoded frame and then the snapshotted stream/codec
default while those contexts remain alive. Published frames do not retain or
expose mutable format, stream, or decoder contexts.

Embedded source ICC bytes remain alive through the retained `AVFrame` and are
preserved and diagnosed. Before rendering, Sunroom clears both libplacebo ICC
representations on the render-local frame copy on every platform. This keeps
the accepted no-source-ICC-transform policy independent of whether the linked
libplacebo was built with LCMS support. Profile parsing and source-ICC rendering
remain deferred.

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

1. Complete the FFmpeg/libplacebo SDR, PQ, HLG, HDR10+, and Dolby Vision
   acceptance fixtures and display-target experiments.
2. Normalize complete stream, chapter, attachment, and provisional/final
   session duration state.
3. Dispatch subtitle packets without letting one selected stream prevent
   progress for another.
4. Add packet byte/duration and decode-time diagnostics.
5. Add VAAPI/DRM PRIME hardware-device negotiation and native import behind
   the existing graphics-domain contract. VideoToolbox NV12/P010 negotiation
   and import are complete for the initial macOS scope.

## Verification

The dependency test verifies the selected FFmpeg ABI majors including
libswresample and the required native decoders. On Windows it also loads the
four staged DLLs, confirms D3D11VA, and checks the intentionally minimal vcpkg
feature shape. On Linux it links the system libraries and verifies the exposed
VAAPI/DRM pixel-format and device APIs without claiming native-frame import.
On macOS it verifies the pinned ABI, native H.264/HEVC decoders,
VideoToolbox device/pixel-format support, and the intentionally minimal vcpkg
feature shape.

The decoded-frame unit test releases the originating `AVFrame` and verifies the
Sunroom wrapper still owns valid software pixels and semantic snapshots.

The first pipeline fixture is a pinned, hashed, lossless 4×4 PPM image. A
second analytically generated fixture is a three-frame Matroska/FFV1 stream
with `yuv420p`, BT.709 primaries/transfer/matrix, limited range, left chroma
location, 4 fps timing, and SAR 32:27. Tests verify its exact decoded YUV
samples and tolerant linear-RGB libplacebo output. It proves deterministic
compressed software decode and YUV conversion. A third pinned 640×360
Matroska/H.264 fixture runs on the real D3D11VA decoder, returns a retained NV12
texture-array slice from the shared graphics device, and is compared against
software decode after production libplacebo rendering. The test asserts that
the hardware input path performs no CPU transfer or GPU copy and that the
direct output target performs no copy or CPU transfer. Both three-frame
fixtures are also drained continuously through the production packet/decode
state machine; the D3D11VA case retains all three hardware frames
simultaneously within the declared surface budget. A controlled failed
post-selection result at the fallback-policy boundary verifies software retry
and preserved reason. Queue tests verify hard capacity, backpressure,
generation reset, and stop wakeup. A separate twelve-frame FFV1 fixture drives
the production session beyond mailbox capacity and verifies pause-induced
decoder backpressure, resume/refill, due-frame dropping, complete drain, end,
and replay.

On Apple M2/macOS 26, the H.264 fixture decodes through VideoToolbox as NV12
and the Main10 HEVC/PQ fixture decodes as P010. Both import retained CoreVideo
Metal planes directly, report zero input transfers/copies and zero output
copies, and agree with software decode at representative pixels. A three-frame
H.264 run through one producer verifies replacement and deferred native-surface
lifetime across GPU completion.
The playback fixture also verifies exact-zero and nonzero seeking,
paused/playing intent, end seeking, and position-preserving hardware-import
fallback. A separate pinned H.264 fixture has closed sparse GOPs and B-frames;
it proves that FFmpeg starts at the preceding keyframe and the session decodes
dependencies without publishing preroll. Pure timeline tests prove that an
inferred first-frame origin survives restart and use one-frame PTS lookahead
when a frame duration is not authoritative. A two-frame sparse-timeline FFV1
fixture places its second keyframe at 3000 seconds and proves the production
demux seek retains positions beyond the 32-bit-microsecond boundary.

A separate hashed Matroska fixture combines twelve lossless FFV1 frames with
lossless 32 kHz mono FLAC impulses on a nonzero timeline. The synchronized
integration scenario invokes the production shared media operation, exercises
both real FFmpeg decoders, converts and drains 48 kHz stereo float32 PCM,
verifies the common timeline and marker locations, and seeks without asserting
private FFmpeg open, packet, or decoder-call counts. It also proves that the
eight-second Matroska header value is kept provisional at open while the fully
observed selected A/V endpoints finalize the normalized playback duration at
three seconds.

The production-session scenario consumes one invocation of that operation
through a controlled audio device, proving audio-master frame selection,
pause, generation replacement, drain, and session completion. Five additional
manifest-hashed fixtures cover a decoded timestamp gap, short and long
post-audio video tails, and opposite audio and video start offsets. The
short-audio fixture also verifies a clean video-only seek interval after
selected audio has already ended. A no-presentation-consumer scenario proves
the playback monitor drains the bounded frame mailbox so the shared demuxer
and audio path keep making progress.
