# Media subsystem

## Status

Sunroom integrates the official vcpkg FFmpeg 8.1.2 package with `avformat`,
`avcodec`, and core `avutil`. The Windows package includes D3D11VA, D3D12VA,
DXVA2, and Media Foundation support. Sunroom now exercises D3D11VA for
supported streams and keeps software decoding as an explicit fallback.

The production `decodeVideoFrames()` operation accepts one restartable request,
opens a local file, discovers the best video stream, and continuously emits immutable
`DecodedVideoFrame`s. `decodeFirstVideoFrame()` is now only a focused-test
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

This remains the first selected-video pipeline. It has no audio/subtitle
packet dispatch, complete track-discovery model, or
source-stall recovery.

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
propagate them. Effective sample aspect ratio prefers the decoded frame and
then the snapshotted stream/codec default while those contexts remain alive.
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

1. Normalize complete stream, chapter, attachment, and session duration state.
2. Dispatch audio and subtitle packets without letting a full video channel
   prevent progress for interleaved streams.
3. Add packet byte/duration and decode-time diagnostics.
4. Add equivalent native hardware-device negotiation on Linux and macOS when
   their graphics domains are implemented.

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
The playback fixture also verifies exact-zero and nonzero seeking,
paused/playing intent, end seeking, and position-preserving hardware-import
fallback. A separate pinned H.264 fixture has closed sparse GOPs and B-frames;
it proves that FFmpeg starts at the preceding keyframe and the session decodes
dependencies without publishing preroll. Pure timeline tests prove that an
inferred first-frame origin survives restart and use one-frame PTS lookahead
when a frame duration is not authoritative. A two-frame sparse-timeline FFV1
fixture places its second keyframe at 3000 seconds and proves the production
demux seek retains positions beyond the 32-bit-microsecond boundary.
