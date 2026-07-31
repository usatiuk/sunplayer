# Audio subsystem

## Status

Sunroom now routes decoded audio through the production playback session and
plays it through the default Windows output device. Presented cubeb frames are
the ordinary media clock for sources with audio; video-only sources retain the
monotonic clock.

The dependency graph includes FFmpeg libswresample and a pinned current cubeb
overlay. A shared FFmpeg media operation opens and probes one source once,
routes selected audio and video packets under one global count/byte budget,
decodes FLAC audio with the real FFmpeg decoder, and converts it to 48 kHz
stereo native-endian interleaved float32 PCM. The same operation decodes video
and preserves a shared normalized timestamp origin.

`MediaSession` uses that shared operation directly and passes the active
graphics capability into its video request, so D3D11VA negotiation and the
existing whole-operation software fallback remain available without a separate
audio open. One playback attempt therefore probes, seeks, reads, and cancels
one source generation for both streams. A hardware fallback deliberately
restarts that whole generation with fresh shared contexts; opening the media a
second time in parallel for audio is not part of the architecture.

`ControlledAudioSink` is a bounded deterministic test edge. It models
submission, presentation, pause, reset, and media-position mapping without an
operating-system device. It is deliberately lock-based and must not be reused
as cubeb's real-time callback buffer.

`CubebAudioSink` opens the default WASAPI output through the pinned cubeb
build. All cubeb lifecycle and position calls run on one dedicated MTA control
thread. Its callback consumes a preallocated SPSC float queue, writes bounded
underrun silence, applies one atomically published linear gain, and publishes
fixed-capacity output-to-media spans. It does not allocate, block, take
application locks, decode, log, invoke Qt, or perform device recovery.

Volume is a session-lifetime value from zero to one. Mute selects zero effective
gain without changing that remembered value. Both controls operate after PCM
queueing, so they affect already-buffered audio immediately while submitted and
presented cursors continue unchanged. They are not implemented by pausing the
device, dropping samples, or changing the media clock. A later measured slice
may add a short click-free ramp; this milestone deliberately keeps gain
application bounded and callback-safe.

## Accepted pipeline

```text
one AVFormatContext
        |
        +-- selected video packets --> video decoder --> decoded video frames
        |
        +-- selected audio packets --> audio decoder --> libswresample
                                                   --> float32 PCM blocks
                                                           |
                              +----------------------------+----------------+
                              |                                             |
                    ControlledAudioSink                              CubebAudioSink
                    deterministic tests                      default Windows device
```

The demux owner alone calls `av_read_frame()`. Packet queues are logically per
stream but charged against one aggregate budget. This bounds aggregate memory
except for one oversized packet admitted only when the router is empty.
It does not promise fairness through arbitrary pathological interleave if one
decoder stops consuming. Queue telemetry and source-failure scenarios will
determine whether production needs soft watermarks or reservations.
Decode results expose the configured aggregate limits, maximum observed packet
count and bytes, and largest packet so tests and diagnostics can verify the
one-packet exception without depending on private queue-call sequences.

Synchronized A/V requires one stable common timeline origin from the request,
container, or selected stream metadata. An originless selected A/V source is
currently rejected rather than independently zeroing audio and video and
silently destroying their encoded offset. Establishing a common origin from
buffered first timestamps is a later compatibility slice.

FFmpeg's format duration is an open-time estimate and is never treated as an
absolute endpoint or reduced by `start_time`. Once selected decoding reaches
EOF, the operation replaces it with the maximum observed normalized endpoint
and marks that duration final. A selected audio stream with no samples in the
requested interval contributes no endpoint, so a seek into a trailing
video-only region still finalizes from video. This derives the exact fixture
duration without a second parse or tail read.

## PCM contract

Each `PcmAudioBlock` contains:

* Playback generation.
* Absolute converted-stream frame index.
* Normalized media timestamp of its first frame.
* Sample rate and channel count.
* Native-endian interleaved float32 samples.

An output epoch begins at the requested media position. If stream metadata
declares that selected audio begins later, the decoder publishes the complete
declared interval as timeline-advancing silence, split into bounded PCM blocks,
before packet routing can form a packet/frame/PCM queue cycle. Decoded
timestamps remain authoritative: they fill any additional leading or
midstream gap and reject overlaps. This is source silence and advances media
time. It is distinct from callback underrun hold silence, which does not
advance media time.

FFmpeg owns compressed decoding. libswresample owns sample-format conversion,
planar/interleaved conversion, channel rematrixing, sample-rate conversion, and
draining converter delay. Playback owns timeline normalization, generation
invalidation, queue bounds, and discontinuity policy. The sink owns device
negotiation and presentation observations; it does not reinterpret media PTS.

The initial controlled and physical formats are 48 kHz stereo because that
gives stable fixture expectations and one proven device epoch. Format
renegotiation and rebuilding libswresample for a changed device epoch remain
recovery work.

Each physical epoch fixes both its preroll and maximum accepted PCM block size.
The maximum is no greater than `queue capacity - preroll`, so an accepted block
can never wait for a callback that cannot start until that same block is
published. FFmpeg output must be split before this boundary if a future format
can produce a larger block.

## Device and clock direction

The production backend is cubeb in shared mode. Sunroom follows the default
output unless the product later exposes an explicit device choice. Context
collection-change notification is capability-reported; stream-level device
change callbacks are not assumed because the pinned WASAPI backend does not
provide them.

The playback position reported by cubeb is the primary presentation
observation. Sunroom must not subtract reported latency from that position a
second time. Reported latency remains useful diagnostics and a possible
lower-confidence fallback input. Backends expose different device identity and
change-notification capabilities, so those observations remain optional and
explicit rather than assumed portable.

Startup is timeline-driven rather than a two-payload latch. Stream selection
and initial video diagnostics establish the session boundary. Until the first
valid audio presentation observation exists, video uses a provisional
monotonic clock; audio becomes the master as soon as its epoch is observable.
If that handoff moves the authoritative position behind the currently shown
video frame, the frame is held until presented audio catches up. Audio is not
dropped or accelerated to preserve a provisional estimate.
An audio epoch may start while the video layer is empty, and a first video
frame with a future PTS stays queued until its time. If audio begins later,
leading source silence keeps its presentation clock continuous from the
requested position. Clean audio EOF with zero output for the requested
interval creates no audio epoch and keeps the monotonic clock. A seek or
replacement cancels the decoder and matching sink epoch together, waking
blocked PCM submission and rejecting stale completion.

The audio and video decoder workers both start before demuxing. Packet routing
does not depend on either decoder producing its first output: every selected
packet uses the same cancellable aggregate byte/count budget. Decoder outputs
apply their own bounded backpressure. During active playback, the session also
selects or drops due video independently of GUI rendering, so an inactive
presentation consumer cannot fill the decoded-video queue and indirectly
starve audio demuxing.

When presented audio drains before video, the session anchors a monotonic tail
clock at the final presented audio position, even if cubeb no longer answers a
live position query after reporting drain. This lets later video frames and
the authoritative media endpoint complete without pretending that a stopped
audio device is still advancing. A generation-scoped sink failure instead
cancels the pipeline and becomes a visible session error; it is not silently
treated as a no-audio fallback.

The session reuses one cubeb observation per presentation decision. A
lightweight playback monitor also selects or drops due video frames and emits
throttled timeline updates when the window, swapchain, or active page is not
requesting renders. That keeps the bounded video queue from backpressuring the
shared demuxer and starving audio. It also observes audio drain and failure.
Explicit sink failure is immediate; after `Ready`, loss of an established live
position is allowed a one-second grace period and then becomes a visible
clock-unavailable error rather than freezing indefinitely or silently
switching clock sources.

The same bounded monitor publishes a typed, low-rate audio diagnostic snapshot
containing backend and format, PCM capacity and occupancy, submitted and
presented media frames, device positions and latency when available, underrun
frames, device revision, notification capability, and clock reliability. The
session exposes the active clock source and the ordinary Player page shows the
backend, clock, queued PCM duration, and underrun count. No callback emits Qt
signals or log records.

User intent remains separate from temporary ability to play. Pause freezes the
timeline. Device loss or sustained underrun will freeze it in a recovery or
buffering state, create a new audio-output epoch, preroll current-generation
PCM, and re-anchor before resuming if the user still wants playback.

A short underrun is represented as hold silence in the output ledger. Cubeb's
device-frame position continues to advance while its media-frame mapping stays
fixed, then resumes from the next real PCM frame. End-of-stream is
generation-scoped so a stale decoder cannot finish a newer sink epoch.

## Verification

The checked-in `sdr-bt709-ffv1-flac-sync.mkv` fixture contains lossless FFV1
video flashes and matching FLAC impulses on a nonzero container timeline. Its
manifest pins the hash, generation command, source format, resampled format,
expected frame count, and marker positions.

The integration scenarios invoke the real FFmpeg media operation across
both decoders, libswresample conversion and drain, shared timestamp
normalization, and bounded PCM submission. It asserts behavior at media and
sample boundaries rather than FFmpeg packet or decoder-call counts. Focused
sink tests verify that submitted audio does not become presented until the
controlled device advances, pause freezes presentation, capacity is bounded,
and generation reset wakes blocked work.

The production-session scenario uses the same real A/V operation with a
controlled sink. It proves one production media-operation invocation per open
or seek, audio-master position and frame selection, pause, seek-generation
replacement, drain, and end of stream. Pinned shorter-audio fixtures verify
the audio-to-monotonic tail transition over both short and high-frame-rate
video tails. Injecting a sink failure after decoded-audio EOS verifies
current-generation failure propagation and cancellation.

A real-FFmpeg session regression runs muted playback through that same
controlled-device boundary and proves presented audio remains the advancing
master clock. Focused sink coverage verifies that gain changes samples but not
the submitted/presented mapping, and QML component coverage exercises both
controls and the typed diagnostic bindings.

Two additional lossless fixtures offset audio and video starts in opposite
directions. They prove that leading audio gaps advance through silence, audio
may play before the first video frame, and future video is not presented early.
The shorter-audio scenario also seeks beyond the audio endpoint, disables the
live position after drain, and completes on the video timeline. Separate
session scenarios prove hidden post-decode sink failure and sustained clock
loss without requiring presentation calls. A further real-FFmpeg session
scenario supplies no presentation consumer at all and proves playback still
drains bounded video/audio queues to end of stream; removing the playback
monitor's frame selection makes that scenario fail.

The callback-boundary tests additionally verify ring wrap, whole-block atomic
publication, sticky stop/reset cancellation, zero fill, hold-silence mapping,
and bounded ledger overwrite. A Windows integration scenario explicitly
selects cubeb's WASAPI backend, opens the default endpoint on the sink's MTA
thread, and exercises silent preroll, start, position observation, pause,
generation reset, drain, and destruction. A focused regression proves that a
PCM block cannot create a preroll/capacity dependency cycle.

A registered application scenario launches the built `sunroom` executable
with the production FFmpeg, Cubeb, QML, QRhi, libplacebo, compositor, and
swapchain paths. Its audio-first fixture reaches `Ready` before a video frame
is available; the scenario requires two distinct video content revisions to
reach the swapchain plus live, advancing Cubeb-derived media-clock progress
before exiting automatically.
The QML component scenario separately asserts that `Ready` without a decoded
frame keeps the Player viewport and controls active. Playback liveness no
longer depends on that UI invariant because the playback monitor drains frames
without a presentation consumer.

Automated tests do not prove acoustic output, device migration, callback timing
under pressure, real cubeb fault recovery, or speaker-to-display A/V offset.
Those require real-device fault scenarios and later physical flash/impulse
measurement. The production cubeb backend is currently Windows-only; macOS and
Linux packaging and behavior remain explicit future work.

See [the plan](PLAN.md) and
[ADR 0011](../../decisions/0011-single-pass-media-routing-and-audio-output-boundary.md).
