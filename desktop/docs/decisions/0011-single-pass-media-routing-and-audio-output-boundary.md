# 0011: Route selected media once and separate decoded audio from device output

* Status: Accepted
* Date: 2026-07-31
* Media-input clarification: 2026-08-01
* Output-migration amendment:
  [0016: Reconcile output changes by semantic value](0016-reconcile-output-changes-semantically.md)

## Context

The video-only pipeline owns one FFmpeg demux context and one selected-video
packet channel. Adding audio by opening the same source a second time would
duplicate probing, seeking, network traffic, buffering, cancellation, and
failure state. It could also let the audio and video readers observe different
source state.

Audio decoding and physical output have different threading requirements.
FFmpeg decoding and sample conversion may allocate and block; a device callback
must consume already prepared PCM without decoding, waiting, logging, or
touching application state. Playback also needs a deterministic device edge for
behavioral tests and, eventually, an observation of audio that has reached the
backend presentation position rather than merely been submitted.

## Decision

One playback attempt owns exactly one `AVFormatContext`. It opens, probes,
positions, and reads the source once. The demux owner references selected audio
and video packets into per-stream queues charged against one global count and
byte budget. Decoder workers own their respective `AVCodecContext`s. The first
router has no per-stream reservation or soft watermark: it bounds aggregate
memory to the configured budget except that one oversized packet may enter an
otherwise empty router. It relies on both workers continuing to drain and does
not claim progress through arbitrarily pathological interleave when one worker
stalls.

The single-open rule applies equally to local files, FFmpeg-native remote URLs,
and a possible future custom `AVIOContext`. Remote support should begin by
passing a sanitized locator and narrow open options to FFmpeg. An
application-owned byte reader/cache is added only when concrete cancellation,
read-ahead, credential, or recovery requirements exceed the native protocol
layer. It replaces the input edge of the same `AVFormatContext`; it never adds
a parallel audio/video reader or a general source-plugin framework.

Decoded audio is converted off the real-time thread through FFmpeg
libswresample into native-endian, interleaved float32 PCM. A PCM block carries
its playback generation, absolute output-frame index, normalized media start,
sample rate, and channel count. The initial controlled scenario uses 48 kHz
stereo. The production sink requests the same stable format across ordinary
default-device migration; native endpoint conversion remains cubeb's concern.

`AudioSink` is the narrow decode/control-thread boundary. Its first
implementation, `ControlledAudioSink`, is bounded and deterministic and keeps
submitted and presented cursors separate. It intentionally uses locks and is
not a real-time callback implementation.

The physical implementation is a pinned cubeb backend. The real-time callback
reads from preallocated PCM and metadata storage, writes bounded hold silence,
and updates fixed-capacity observations only. It does not
decode, allocate, block, log synchronously, invoke Qt, or perform recovery.
Sunroom serializes stream lifecycle on its control thread. Cubeb and the sound
service own ordinary migration of a stream following the system default;
application-level stream recreation is a fallback after an error or
demonstrated persistent no-progress condition.

When cubeb supplies a playback position, Sunroom treats that position as the
primary backend presentation observation. It does not subtract cubeb's reported
latency again. Latency remains diagnostic and may support a lower-confidence
fallback when a backend position is unavailable. Position, latency, and device
change capabilities are capability-reported because they are not identical on
all cubeb backends.

Audio and video retain one normalized media timeline and one playback
generation. Seeking or replacement invalidates packets, frames, converted PCM,
and sink observations from the old generation together.

The production session uses the shared operation. Stream selection and initial
video diagnostics are published before decoder workers start. Readiness is a
timeline state, not a latch between the first audio and video payloads. A
provisional monotonic clock advances video until the first valid audio
presentation observation, after which audio becomes the master and video
waits or drops to follow it. If the provisional position is ahead at handoff,
the current video frame is held until presented audio catches up; audio is not
discarded or retimed to preserve a startup estimate. Audio may start while the
video layer is still empty, and a future first video frame is not displayed
before its presentation time. The Player viewport remains active in this
ready-without-frame interval so the presentation boundary can select a newly
available frame; the renderer provisions no video surface until the source
actually publishes display geometry.

The initial audio-output epoch in a playback generation begins at the requested
playback position. When selected audio begins later, the decoder publishes
timeline-advancing source silence up to the first decoded sample. This is media
silence, not underrun hold silence. It keeps the audio presentation clock
continuous and prevents bounded audio, video, and packet queues from waiting
circularly for a later stream. A selected audio stream that reaches clean
end-of-stream without producing output for the requested interval creates no
audio epoch; playback uses the no-audio clock for that interval. Decode failure
remains terminal and is not reclassified as an empty interval.

An audio-output epoch is distinct from a playback generation. It identifies one
cubeb stream lifetime and its presentation ledger, not every native endpoint
or client that cubeb may use internally. Presentation snapshots and diagnostics
carry both identities, and playback rejects a replaced stream's epoch until it
has been explicitly re-anchored. Application-level stream replacement recreates
device-dependent output state and anchors the new raw clock to the last
confident media position. It does not normally reopen the shared source,
demuxer, or video decoder. Full generation replacement remains a fallback when
buffered timeline state cannot be reconciled.

Both decoder workers start before demuxing, and packet admission is independent
of first-frame or first-PCM readiness. Every selected packet passes through the
same cancellable aggregate budget. Each decoded-output boundary remains
bounded; the playback-owned monitor advances the video scheduler even when no
window or page is requesting frames, preventing that downstream boundary from
starving audio demuxing.

Presented audio drives `MediaClockSnapshot` during the audio interval. One
sink observation is reused for each presentation decision. A drained epoch
publishes its final media endpoint independently of whether the backend still
answers live position queries; if audio drains before video, playback
continues from that endpoint with a monotonic tail clock. A live position
observation may be briefly unavailable. Sustained loss of an established
observation is terminal until output-epoch replacement exists; it never
switches to the provisional or no-audio clock. A callback underrun maps device
frames to hold silence without advancing media, and a sustained hold enters
`Buffering`. User play intent remains separate from the interruption. Explicit
current-generation sink failure is likewise terminal. Active
sessions monitor this state even when no video frame is being requested. The
same playback-owned monitor selects or drops due video frames while
presentation is hidden or inactive, preventing the bounded frame mailbox from
starving the shared audio demux path.

Synchronized A/V currently requires a stable common origin from the request,
container, or selected stream metadata before workers start. It rejects a
source whose selected audio and video streams provide none; independently
zeroing first decoded timestamps would destroy a real inter-stream offset.

## Consequences

Benefits:

* Local and network sources are not parsed or read twice for A/V playback.
* Packet memory is bounded once across the selected streams instead of by
  unrelated per-stream hard caps.
* Deterministic scenarios can exercise real demux, decode, resampling, queueing,
  and clock mapping without opening an audio device.
* A bounded Windows application scenario additionally proves the production
  Cubeb clock, QML viewport, QRhi/libplacebo, compositor, and swapchain wiring
  for audio-first startup.
* The cubeb callback has a small, explicit real-time contract.
* The video scheduler remains clock-source-neutral.
* Output gain and mute share the same sink contract in deterministic and
  physical implementations and cannot redefine clock progression.
* Low-rate audio diagnostics share one typed contract without making the
  callback log, allocate, or signal Qt.

Costs and current limits:

* The shared production operation feeds the hardware-capable video packet
  decoder and passes the active graphics capability. Deterministic A/V clock
  scenarios still use software frames so they do not depend on GPU capability.
* One aggregate packet cap cannot guarantee fairness for an arbitrary
  container interleave. Production telemetry and unreliable-source scenarios
  must establish whether soft watermarks or reservations are needed.
* `ControlledAudioSink` proves deterministic queue and cursor behavior; the
  physical sink's default-endpoint tests do not prove real playback latency.
* Hold-silence mapping, the initial `Buffering` interruption, and the
  audio-backed production clock exist. Replacing a physical device/output
  epoch while retaining the media generation remains a later slice; sustained
  clock loss and terminal device failure currently become visible errors.
* cubeb is maintained as a project-local overlay because the pinned registry
  package is substantially older than the reviewed upstream revision. The
  temporary fail-closed WASAPI patch and explicit device pinning are superseded
  by ADR 0016 and will be removed by the next simplification refactor.

## Alternatives considered

### Open independent FFmpeg contexts for audio and video

Rejected. It duplicates I/O and source state and is especially unsuitable for
large or unreliable network files.

### Decode audio in the device callback

Rejected. FFmpeg and libswresample work does not satisfy a real-time callback
contract.

### Use submitted PCM as the master clock

Rejected. Submission precedes operating-system and device presentation by an
amount that can change with the backend and device.

### Start with a general audio-backend plugin system

Rejected. The known need is one controlled sink and one production sink behind
a narrow contract. A wider backend framework has no current policy to express.

### Implement three native device backends immediately

Rejected. cubeb already provides the relevant cross-platform device and timing
boundary. Native extensions remain an escape route if measured backend behavior
cannot meet a supported platform requirement.
