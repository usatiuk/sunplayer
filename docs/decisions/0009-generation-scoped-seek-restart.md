# 0009: Use generation-scoped decode restarts for seeking and recovery

* Status: Accepted
* Date: 2026-07-30
* Amended: 2026-08-23

## Context

SunPlayer needs user seeking now and will also need decoder restarts for hardware
fallback and graphics-device recovery. The current pipeline deliberately gives
one worker exclusive ownership of each FFmpeg context, bounds retained packets
and frames, and rejects stale work by playback generation.

An in-place seek command would require a new cross-thread barrier covering
demux I/O, queued packets, decoder reorder state, hardware surfaces, scheduler
state, and cancellation. A restart already has the required ownership and
latest-request semantics.

## Decision

Use one generation-scoped decode-start request for initial open, user seek,
hardware-import fallback, graphics recovery, and replay. The request carries:

* The media path and first frame identity.
* The selected hardware-decode capability and retained-frame budget.
* An optional normalized target position. Initial open omits it; a real seek
  to zero carries zero.
* The stable media timeline origin.
* Whether the demuxer must seek or can read naturally from the start. The
  target remains present when a nonseekable source restarts at zero so decoded
  negative-timestamp preroll is still filtered.

Each same-media targeted restart cancels the previous operation, increments
the playback generation once, clears the stale queue, opens fresh demux and
codec contexts, positions or begins natural reading before demux handoff, and
decodes forward normally. Every restart clears the currently presented frame
and invalidates its decoded producer. This prevents a mapped source frame,
libplacebo mapping, or native import cache from crossing decoder generations.
An explicit seek to zero remains a targeted restart.

Decoded keyframe preroll is removed before the bounded frame mailbox. The gate
uses a decoded duration when FFmpeg supplied one. When duration is missing or
only estimated, it waits for the next decoded PTS before deciding which frame
covers the target. It admits the frame active at the target and the next frame
needed by normal scheduling, or the first frame after a gap. At end of stream,
the final preceding frame is the bounded fallback. Stream discovery or audio
may make the session ready before a future replacement video frame is due, but
the seek remains observable and the viewport remains active on its empty
background until the new generation's frame is selected. Its clock anchors no
earlier than both the requested position and that frame's PTS. Paused seeks
remain paused; playing seeks resume after bootstrap. Play/pause
intent remains mutable while the replacement opens. Rapid relative UI commands
are coalesced briefly before dispatch, while any request dispatched during an
active seek immediately replaces that generation; old-generation callbacks
and frames remain invalid.

If a current-generation seek drains cleanly without admitting any video frame,
the session completes the seek. It does not present stale video indefinitely or
treat the valid no-video target as a decoder failure.

Graphics recovery uses the same restart for software and hardware-backed
pipelines. Software frame storage can remain valid across device generations,
but retaining the old active pipeline would also retain obsolete decode
capability for subsequent seek or fallback. If recovery interrupts a user
seek, the pending seek state and latest requested position survive until the
replacement graphics capability arrives.

The exact internal timeline stays in microseconds. QML observes explicitly
named millisecond properties and issues a millisecond seek command. Duration,
seekability, and stable origin are discovered once and retained across
same-media restarts.

The normalized target is converted to the selected stream's absolute timestamp
with one 64-bit `av_rescale_q` operation plus checked addition to the stable
origin. Incremental clock helpers that narrow through `AVRational` are not used
for an arbitrary absolute playback position.

## Consequences

Benefits:

* Seek, software fallback, and graphics recovery share one lifecycle.
* FFmpeg ownership remains simple and no partial flush protocol is invented.
* Long-GOP preroll cannot deadlock the three-frame mailbox.
* Hardware decoder state and EOF state are naturally fresh after every seek.
* The request seam can later be implemented with persistent contexts without
  changing the session or QML contract.

Costs:

* Each seek currently reopens and reprobes the local file.
* The viewport shows its normal black background until the replacement frame
  reaches its target.

## Alternatives considered

### Seek and flush the live contexts

Deferred. It may reduce restart latency, but it requires a correct ownership
barrier and stale-packet protocol that the video-only slice does not yet need.

### Decode from the beginning

Rejected. It is unbounded for long media and makes recovery latency depend on
elapsed playback time.

### Drop encoded packets before the requested timestamp

Rejected. Inter-frame video requires packets before the target to reconstruct
the requested frame.

### Put all preroll into the frame mailbox

Rejected. A GOP can exceed the mailbox's hardware-surface budget and block the
decoder before it reaches the target.
