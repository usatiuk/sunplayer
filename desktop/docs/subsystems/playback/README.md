# Playback subsystem

## Status

`MediaSession` owns the initial continuous video lifecycle:

* `Empty`.
* `Opening`.
* `Ready`, with independently observable playing, paused, or ended intent.
* `Error`.

Opening, demuxing, and decoding happen off the GUI thread. A nonzero playback
generation belongs to each request; cancellation or a newer request invalidates
older frames, notifications, and completion. One persistent supervisor retains
only the latest pending request. Each active operation owns a demux worker,
byte-bounded packet channel, decoder, and generation-scoped decoded-frame
mailbox. Cancel/replacement requests stop without joining the GUI thread.

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
queues or clock sources.
Opening becomes `Ready` when the first frame is selected, and local video
autoplays. Play/pause/replay change user intent without recreating the decoder
unless replay follows end of stream.

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

There is still no audio clock or unified buffering state. The monotonic clock
is deliberately one producer of the shared
`MediaClockSnapshot` value, not a claim of A/V synchronization.

## Clock ownership

The playback core owns the media timeline. Audio and platform backends report
timestamped observations; they do not mutate session position directly.

During ordinary playback with an audio track, the estimated position already
rendered by the audio device is the master observation. It combines submitted
sample counts, device/backend presentation position, output latency, monotonic
sample time, playback rate, and the current seek/pause anchor.

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

Muted playback keeps the audio stream advancing at zero gain when practical,
preserving the same clock. Media without an audio track uses a monotonic-clock
master.

Small drift may be corrected gradually. Seeking, suspend/resume, device
replacement, or another large discontinuity re-anchors the clock explicitly
rather than hiding the jump through prolonged drift correction.

## Audio-device recovery

User intent and temporary ability to advance are distinct state:

* User pause freezes the clock until the user resumes.
* Audio-device loss enters `RecoveringAudio`, not `Paused`.
* Video presentation and media-clock advancement stop while the required audio
  stream is recreated.
* The new stream receives bounded preroll, reports a trustworthy position and
  latency, and re-anchors the media clock.
* Playback resumes automatically only if user intent still says playing.

This policy covers default-device changes and Bluetooth disconnect/reconnect
without allowing video to run silently ahead or presenting old queued audio
after recovery.

A short audio underrun writes silence and lowers clock confidence. A sustained
underrun participates in the unified buffering state and freezes progression.
The real-time audio callback only reads prepared PCM, applies trivial gain,
writes silence, and updates atomic counters; it never decodes, allocates,
seeks, logs synchronously, or calls into Qt.

## Frame scheduling

Every decoded frame has a playback generation independent of its PTS. PTS may
be missing or repeated and is not frame identity.

The initial video-only scheduler uses predicted presentation time:

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
Timeline and preroll tests verify stable-origin reuse, authoritative-duration
intervals, one-frame PTS lookahead for missing durations, and that frames
ending before the target do not enter the bounded mailbox. A fallback scenario
verifies that a
hardware-import failure restarts at the current position. Scheduler tests
exercise paused snapshots, multi-due dropping, and one authoritative declared
session endpoint independently of the clock producer. Queue tests cover hard
backpressure plus stop/generation wakeups;
timeline tests cover valid, missing, repeated, and non-monotonic timestamps.

The later audio-clock suite still needs underrun, latency change, device
replacement, seek generation, and large-discontinuity cases.

Later physical verification uses synchronized audio impulses and visual flashes
to measure actual speaker-to-display output timing. Software timestamps alone
cannot prove Bluetooth, operating-system, display, and acoustic latency.
