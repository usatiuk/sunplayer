# FFmpeg keyframe seek and restart

* Date: 2026-07-30
* Scope: pinned FFmpeg 8.1.2, local seekable inputs, selected-video pipeline

## Question

How can SunPlayer add accurate user seeking without weakening the existing
single-owner FFmpeg lifecycle, bounded queues, cancellation, hardware-frame
lifetime, or generation invalidation?

## Evidence

The pinned `libavformat/avformat.h` documents `avformat_seek_file()` as a
range-based seek to the closest point from which active streams can be
presented. `AVSEEK_FLAG_BACKWARD` is not part of that contract. With a selected
video stream, no `AVSEEK_FLAG_ANY`, an unbounded minimum, and the requested
timestamp as both target and maximum, the chosen anchor cannot be after the
request and remains keyframe constrained:

```text
avformat_seek_file(
    format,
    videoStream,
    INT64_MIN,
    targetTimestamp,
    targetTimestamp,
    0)
```

The pinned `libavformat/seek.c` uses the demuxer's range seek when available
and falls back internally to the older keyframe seek implementation. Adding a
second application-level fallback would create different behavior rather than
improve reliability.

SunPlayer's current operation probes and selects the stream before
`AVFormatContext` moves to its demux worker. Its `AVCodecContext` remains on
the decoder worker. Therefore a fresh operation can perform the seek before
either context begins packet processing without adding a cross-thread command
or flush protocol.

FFmpeg stream timestamps use the stream time base, while container start time
uses `AV_TIME_BASE`. A stable origin must retain both timestamp and time base.
Converting the origin to rounded microseconds and back can move a boundary by
a stream tick. The implementation therefore rescales the retained origin and
the absolute normalized offset directly into the selected stream's time base,
then combines them with checked 64-bit addition.

The later large-file investigation found that `av_add_stable()` is unsuitable
for this absolute conversion: its incremental-rational path narrows a
microsecond increment through a 32-bit `AVRational` numerator. See
[the long-timeline investigation](2026-07-30-large-network-matroska-seek-observability.md).

## Findings

* A seek should cancel and replace the whole decode operation in this first
  implementation. Fresh demux and codec contexts need no explicit
  `avformat_flush()` or `avcodec_flush_buffers()`.
* Demux positioning and presentation-target filtering are distinct. A
  nonseekable restart at zero reads naturally but retains an explicit zero
  target so frames before the normalized origin cannot be published.
* Only selected streams should remain active for the seek. The current
  video-only operation marks every unselected stream `AVDISCARD_ALL` before
  calling `avformat_seek_file()`.
* Seeking positions the demuxer at a dependency-safe keyframe. Dependent
  frames must still be decoded; packets or frames must not be discarded
  before decode merely because their timestamp precedes the request.
* Decoded preroll must be filtered before SunPlayer's three-frame mailbox.
  Otherwise a long GOP can fill the mailbox before the requested frame can be
  reached.
* A decoded frame duration is authoritative only when FFmpeg supplied it.
  When it is missing, the next decoded PTS is a stronger interval boundary
  than a nominal frame-rate guess. The preroll gate therefore retains at most
  one candidate for lookahead before opening the bounded mailbox.
* The public timeline is nonnegative integer microseconds relative to one
  stable origin. When neither container nor stream publishes a start time,
  the initial first decoded best-effort timestamp becomes that origin and is
  retained for every restart.
* Seek completion means the current generation's target frame has been
  accepted for presentation, not merely that FFmpeg's seek call returned.
* A future in-place implementation will require a demux/decoder barrier,
  stale-packet discard, and `avcodec_flush_buffers()`. Nothing in this slice
  claims that more complex protocol is implemented.

## Result

The accepted lifecycle is recorded in
[ADR 0009](../decisions/0009-generation-scoped-seek-restart.md). Focused
tests cover a pinned closed-GOP H.264/B-frame seek, stable-origin reuse,
duration-aware and PTS-lookahead preroll filtering, exact-zero and end seeking,
nonseekable replay, rapid replacement and cancellation, session intent, seek
failure, position-preserving fallback, and a pending seek across graphics
recovery. A second real-container regression crosses the 32-bit-microsecond
boundary and verifies the production seek target remains 64-bit.
