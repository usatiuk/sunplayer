# Playback subsystem

## Status

`MediaSession` owns the initial continuous media lifecycle:

* `Empty`.
* `Opening`.
* `Ready`, with independently observable playing, paused, or ended intent.
* `Error`.

Opening, demuxing, and decoding happen off the GUI thread. A nonzero playback
generation belongs to each request; cancellation or a newer request invalidates
older packets, frames, PCM, sink observations, notifications, and completion.
One persistent supervisor retains only the latest pending request. Each active
operation owns one FFmpeg demux context, a shared-budget audio/video packet
router, decoder workers, a bounded audio sink, and the generation-scoped
decoded-frame mailbox. Cancel/replacement requests stop without joining the GUI
thread or reopening the source separately for audio.

The decoded-frame mailbox has a hard capacity of three because retained
D3D11VA frames reserve decoder-pool surfaces. Full frame capacity blocks decode;
the bounded packet channel then backpressures demux. A coalesced
frames-available notification requests selection without making Qt's event
queue an unbounded frame queue.

`DecodedVideoSource::prepareForPresentation()` asks a narrow playback-owned
selector for the frame due at the supplied presentation time. `MediaSession`
publishes a `MediaClockSnapshot`; `VideoFrameScheduler` applies due/drop/end
policy to that value and the bounded queue. The source still publishes only
the selected immutable frame and the renderer still knows nothing about decode
queues or clock sources. A coarse playback-owned monitor invokes the same
selection boundary while the renderer is inactive, hidden, or unexposed, so
presentation demand cannot stall the bounded media pipeline. Visible rendering
continues to drive the fine-grained cadence.
Opening becomes `Ready` once stream discovery establishes a usable timeline.
If an audio presentation clock is not available yet, a provisional monotonic
clock advances video and prevents bounded startup queues from waiting on one
another. The first valid audio presentation observation then becomes the
master clock; video waits or drops to follow it. The video layer may still be
empty when audio precedes the first video frame, and that future frame remains
queued until its presentation time. Local media autoplays. Play/pause/replay
change user intent without recreating the decoder unless replay follows end of
stream.

The initial monotonic clock and its snapshots stay in integer microseconds.
Valid FFmpeg PTS is mapped against one stable container/stream origin. If the
container and stream omit their origins, the first decoded best-effort
timestamp becomes the stable fallback and is retained across decoder
restarts. A missing PTS advances by the last positive duration, then the
nominal frame-rate estimate. Backward timestamps are clamped to the previous
scheduled time.

The public session exposes position and duration in explicitly named integer
milliseconds while retaining microseconds internally. Local sources report
seekability only after duration and a stable origin are known. A user seek
cancels the previous decode generation, opens fresh FFmpeg contexts, seeks to
a keyframe at or before the requested position, decodes dependencies, and
filters decoded preroll before the three-frame mailbox. `Ready` resumes only
after the target generation publishes the frame active at the requested
position or the first frame after it. Paused seeks stay paused; playing seeks
resume from the requested clock anchor. Rapid seeks use the existing
latest-request replacement and stale-generation rejection.

Embedded video/audio selection uses that same replacement boundary. The
session records the concrete indexes reported by the initial probe, captures
the current media clock when a different stream is chosen, and restarts the
whole generation at that position. The other video/audio selection, subtitle
selection, and play/pause intent remain unchanged. Selections survive seeks
and graphics/decode recovery for the current media, but a new open resets them
to FFmpeg's normal initial selection. If the current position is beyond a
selected video's declared endpoint, the restart is clamped to that track's
last representable instant.

The QML-facing session resolves selected video/audio/subtitle summaries from
those canonical track models instead of retaining a second selected-track
snapshot. It combines the selected descriptor with current decoded-frame
signal facts and nominal frame duration for the optional playback-details
panel. These diagnostics are eventually consistent during generation
replacement and never alter track selection, timing, or rendering policy.

Early ordinary-playback frames remain queued, due frames are selected only at
the presentation boundary, and if several frames are due only the newest is
published while earlier due frames are counted as dropped. Decoder wakeups
after first-frame bootstrap only request a presentation pass. Decoder drain is
distinct from demux EOF; playback ends only after the queue and final frame
duration are consumed, and the final frame remains visible.

Presentation failures return to the session as user-visible errors. A typed
hardware-frame import failure instead causes one software-only restart from
the current logical position; failure of that software path or a repeated typed failure
becomes the visible error. Supported Windows streams report `D3D11VA`;
unsupported or failed hardware decode reports `Software` plus its fallback
reason. This is diagnostic state, not a user-selectable renderer or decoder
preference.

Graphics-device recreation captures the current or pending logical position,
advances the playback generation for every in-flight or ready pipeline, clears
queued/current state, and holds the session in `Opening` until it can restart
at that position against the replacement capability. Software frame storage is
generation-independent, but the active pipeline still restarts so subsequent
seek and fallback requests use the replacement graphics capability.
If device invalidation interrupts a user seek, the pending seeking state and
latest requested position remain replaceable while the session waits for the
new graphics capability.

The first shared audio-interruption state is now explicit. Sources with audio
use the current generation's cubeb presented-frame observation as the
production clock; sources without audio use the monotonic producer of the same
`MediaClockSnapshot` contract. Startup is timeline-driven rather than gated on
both streams producing a first payload. An audio epoch begins at the requested
position, using timeline-advancing source silence when selected audio starts
later; audio may advance while the video layer remains empty when video starts
later. Clean audio EOF with no output for the requested interval creates no
audio epoch. When audio ends before video, a monotonic tail clock continues
from the final presented audio position. A sustained callback hold enters
`Buffering` and freezes at the last confident position without destroying the
session. Sustained loss of an established live position and explicit
current-generation output failure remain terminal until physical stream
replacement is implemented.

The playback monitor emits throttled timeline notifications during normal
audio-only intervals and monotonic video tails. It begins during opening for
startup/drain observation. A device clock that has not yet started remains the
ordinary provisional startup state; only loss after a trustworthy position has
been established can become a clock failure.

## Clock ownership

The playback core owns the media timeline. Audio and platform backends report
timestamped observations; they do not mutate session position directly.

During ordinary playback with an audio track, the position already presented
by the audio backend is the master observation. Cubeb's reported position is
mapped through a bounded output-to-media ledger; reported latency is diagnostic
and is not subtracted a second time.

Before that first valid audio observation, playback uses its provisional
monotonic clock. This is startup progress, not a competing long-lived master:
as soon as audio becomes observable, subsequent video selection follows audio.
If the provisional video position is ahead at handoff, the current frame is
held until audio catches up; audio is not skipped or retimed to preserve the
temporary video estimate. Loss of an established clock never falls back to
the provisional clock; after a one-second grace period it becomes a visible
session error.

Conceptually:

```text
audio presentation observation
        ↓
media-clock snapshot
        ↓
video scheduler compares normalized frame PTS
        ↓
wait / present / drop / discard stale generation
```

Muted playback keeps the audio stream advancing at zero output gain, preserving
the same clock and PCM-to-media mapping. Volume and mute never pause the sink
or rewrite decoded timestamps. Media without an audio track uses a
monotonic-clock master.

Seeking re-anchors the clock explicitly through a replacement generation.
Suspend/resume, device replacement, large discontinuities, and any measured
need for gradual drift correction remain recovery work rather than implicit
clock smoothing.

## Audio-device recovery

User intent and temporary ability to advance are distinct facts. The session
exposes `playRequested` independently of its interruption:

* User pause freezes the clock until the user resumes.
* A sustained output hold enters `Buffering`, not `Paused`.
* `playing` becomes false while Buffering, while `playRequested` preserves the
  user's intent and keeps the low-rate playback monitor and application-owned
  playback power inhibition active.
* Video presentation and media-clock advancement stop during Buffering, and
  the source stops advertising continuous-frame demand.
* Pause intent is recorded before the fallible sink observation and remains
  separate from the Buffering fact.

Ordinary default-route switching is delegated to the existing null-device
cubeb stream on Windows, macOS, and Linux; it does not replace the audio-output
epoch. Application-level physical stream replacement after a cubeb error is
not implemented yet. Its accepted direction is to replace only the audio-
output epoch from the frozen position, preroll it, and leave recovery only
after a trustworthy new presented-audio observation. The playback generation,
shared demux, and video decoder stay alive unless their buffered timeline
cannot be reconciled. This avoids rereading a network source or allowing video
to run silently ahead.

A short audio underrun writes silence and lowers clock confidence. A sustained
underrun participates in the unified buffering state and freezes progression.
The real-time audio callback only reads prepared PCM, applies trivial gain,
writes silence, and updates atomic counters; it never decodes, allocates,
seeks, logs synchronously, or calls into Qt.

## Frame scheduling

Every decoded frame has a playback generation independent of its PTS. PTS may
be missing or repeated and is not frame identity.

The clock-source-neutral scheduler uses the supplied media position:

* Early frame: retain.
* One due frame: select for presentation.
* Several due frames: publish the newest and count the preceding due frames as
  dropped.
* Old generation: discard regardless of timestamp.

Late thresholds and presentation-latency prediction remain policy to validate
against refresh cadence, renderer latency, and hardware. They are not fixed
from intuition in this first scheduler.

## Verification direction

Current focused tests drive a real twelve-frame FFmpeg fixture through the
production session, pause with the three-frame mailbox full, advance controlled
presentation times, resume, select every frame, replay, seek while paused and
playing, seek to end, and verify drain/end behavior and bounded occupancy. A
real sparse-GOP H.264 seek test verifies decode from a preceding keyframe,
B-frame presentation order, and publication at the requested position.
An additional two-frame, 3000-second sparse-timeline Matroska fixture verifies
the real FFmpeg seek call does not narrow an absolute microsecond target.
Timeline and preroll tests verify stable-origin reuse, authoritative-duration
intervals, one-frame PTS lookahead for missing durations, and that frames
ending before the target do not enter the bounded mailbox. A fallback scenario
verifies that a
hardware-import failure restarts at the current position. Scheduler tests
exercise paused snapshots, multi-due dropping, and one authoritative declared
session endpoint independently of the clock producer. Queue tests cover hard
backpressure plus stop/generation wakeups;
timeline tests cover valid, missing, repeated, and non-monotonic timestamps.

The multitrack controlled-sink scenario proves that video-only and audio-only
changes replace the generation at the current position, publish the requested
pixels and PCM, preserve paused/playing intent, and retain
both selections through a later seek.

The controlled-sink session scenario crosses the production shared FFmpeg A/V
operation, resampling, bounded PCM and frame queues, presented-audio position,
video selection, play/pause, seek, generation invalidation, and drain. Pinned
unequal-duration and opposite-offset fixtures verify the post-audio video tail,
clean zero-output audio after a seek, timeline-advancing leading source
silence, audio-before-video playback, and that a future first video frame is
not displayed early. Drained audio retains a terminal endpoint even if live
position observation disappears. Hidden post-decode sink failure becomes a
visible error without requiring a presentation request. Sustained clock loss
also becomes a visible terminal error after its grace period. A real-FFmpeg
no-presentation-consumer scenario proves playback-owned selection
continues draining the three-frame mailbox and reaches end of stream.
A real-FFmpeg/controlled-sink scenario also proves sustained underrun freezes
media time, enters `Buffering`, preserves play intent, honors pause, and clears
only after media presentation resumes. Latency change, device replacement, and
large-discontinuity recovery remain missing.

Later physical verification uses synchronized audio impulses and visual flashes
to measure actual speaker-to-display output timing. Software timestamps alone
cannot prove Bluetooth, operating-system, display, and acoustic latency.
