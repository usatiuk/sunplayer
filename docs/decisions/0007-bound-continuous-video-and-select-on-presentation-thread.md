# 0007: Bound continuous video and select frames on the presentation thread

* Status: Accepted
* Date: 2026-07-29

## Context

The first media slice returned one retained `AVFrame`. Continuous playback must
allow demux, decode, scheduling, and presentation to advance independently
without allowing compressed packets, decoder surfaces, or Qt events to grow
without bound.

FFmpeg requires one serial owner for `AVFormatContext` and one for
`AVCodecContext`. Its send/receive API also requires an input packet that gets
`EAGAIN` to remain retained until output has been received and that exact packet
can be retried. D3D11VA frames pin decoder-pool surfaces for as long as their
referenced `AVFrame`s remain queued or selected.

The presentation engine already calls
`RenderedVideoSource::prepareForPresentation(now)` before entering GPU work.
That is the narrow point where a playback clock can choose a frame without
teaching FFmpeg, libplacebo, or the compositor about scheduling policy.

## Decision

One media operation owns:

* A demux worker with exclusive `AVFormatContext` access.
* A decoder worker/supervisor with exclusive `AVCodecContext` and hardware
  frame-pool access.
* A selected-video packet channel bounded by count and aggregate encoded bytes.
* A generation-scoped decoded-frame mailbox with a hard capacity of three.

End of input is ordered behind queued packets. The decoder sends one null
packet and receives until `AVERROR_EOF`; demux EOF and decoder drain remain
distinct states. Queue close, generation replacement, and stop requests wake
every blocked producer or consumer. A coalesced frames-available notification
requests presentation, but decoded frames remain only in the bounded mailbox,
not in Qt's event queue.

`MediaSession` owns user intent and the initial monotonic video-only clock.
`VideoFrameTimeline` normalizes decoded timestamps, while
`VideoFrameScheduler` consumes a clock-source-neutral `MediaClockSnapshot` and
owns due/drop/end policy. `DecodedVideoSource` receives a narrow selector and
continues to expose only the currently selected immutable frame to rendering.
After the first-frame bootstrap, decoded-frame wakeups request a presentation
pass but cannot advance the scheduler themselves.

Timing remains integer microseconds derived with FFmpeg rational rescaling.
Valid PTS uses the shared container/stream origin. Missing PTS advances by the
last positive frame duration, then by the nominal frame-rate estimate.
Backward values are clamped to the prior scheduled time. The selector retains
future frames and, when several frames are due, publishes the newest while
counting the preceding due frames as dropped.

Opening local media sets playing intent. Pause freezes the monotonic anchor and
stops continuous presentation requests; resume reanchors it. Ended playback
keeps the final frame visible, and replay starts a new playback generation from
the beginning.

## Consequences

* Backpressure propagates from presentation to decoded surfaces, decoding, and
  demux without an unbounded callback or event queue.
* Packet and decoder contexts have unambiguous thread ownership.
* The hardware-frame reserve covers the three queued frames, current selected
  frame, decoder output blocked on a full queue, and the producer's transient
  prior mapping during a frame switch.
* Decoder output is never dropped by the decoder; presentation policy owns
  dropping.
* The compositor, libplacebo producer, native importer, and active-source
  router remain unchanged.
* Video can visibly play before audio exists, but the monotonic clock is not an
  A/V synchronization claim. A later audio-backed clock publishes the same
  `MediaClockSnapshot` contract.
* There is no position-preserving decoder restart yet. Hardware-import fallback
  and graphics recovery restart from the beginning until a keyframe-anchored
  seek/decode-to-anchor primitive exists.
  [ADR 0009](0009-generation-scoped-seek-restart.md) later supplied that
  primitive.
* A single selected-video packet channel is sufficient for this slice. Audio
  and subtitles require aggregate cross-stream budgeting so a full video path
  cannot prevent demux from reaching interleaved audio packets.

## Alternatives considered

### Demux and decode in one worker

This is smaller but cannot honestly provide the documented packet boundary or
later allow another selected stream to keep progressing while video decode is
backpressured.

### Queue one Qt callback per decoded frame

Rejected because Qt's event queue would become the real unbounded frame queue
and retained hardware surfaces could outlive the declared budget.

### Make `DecodedVideoSource` thread-safe and push frames into it

Rejected because decoder workers would then mutate presentation state and own
drop policy. The source remains a presentation-thread selected-frame boundary.

### Restart hardware decoding at the current arbitrary packet

Rejected because decoder reference state and GOP dependencies make that
incorrect. Position preservation waits for a real seek/restart primitive.
