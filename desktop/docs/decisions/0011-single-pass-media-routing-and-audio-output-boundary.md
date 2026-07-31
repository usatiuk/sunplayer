# 0011: Route selected media once and separate decoded audio from device output

* Status: Accepted
* Date: 2026-07-31

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

Decoded audio is converted off the real-time thread through FFmpeg
libswresample into native-endian, interleaved float32 PCM. A PCM block carries
its playback generation, absolute output-frame index, normalized media start,
sample rate, and channel count. The initial controlled scenario uses 48 kHz
stereo; real device negotiation may select another fixed format for a later
audio-output epoch.

`AudioSink` is the narrow decode/control-thread boundary. Its first
implementation, `ControlledAudioSink`, is bounded and deterministic and keeps
submitted and presented cursors separate. It intentionally uses locks and is
not a real-time callback implementation.

The intended physical implementation is a pinned cubeb backend. The real-time
callback will read from preallocated PCM and metadata storage, apply bounded
gain or silence, and update fixed-capacity observations only. It will not
decode, allocate, block, log synchronously, invoke Qt, or perform recovery.
Device lifecycle and recovery remain control-thread responsibilities.

When cubeb supplies a playback position, Sunroom treats that position as the
primary backend presentation observation. It does not subtract cubeb's reported
latency again. Latency remains diagnostic and may support a lower-confidence
fallback when a backend position is unavailable. Position, latency, and device
change capabilities are capability-reported because they are not identical on
all cubeb backends.

Audio and video retain one normalized media timeline and one playback
generation. Seeking or replacement invalidates packets, frames, converted PCM,
and sink observations from the old generation together.

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
* The later cubeb callback has a small, explicit real-time contract.
* The video scheduler remains clock-source-neutral.

Costs and current limits:

* The video-only and shared A/V operations now feed one hardware-capable packet
  decoder. The synchronized regression uses software frames; production
  migration must pass the active graphics capability and preserve the existing
  whole-operation hardware fallback without reopening the source.
* One aggregate packet cap cannot guarantee fairness for an arbitrary
  container interleave. Production telemetry and unreliable-source scenarios
  must establish whether soft watermarks or reservations are needed.
* `ControlledAudioSink` proves queue and cursor behavior, not real device
  latency or callback safety.
* Device epochs, hold-silence mapping, cubeb recovery, and an audio-backed
  production clock remain later slices.
* cubeb is maintained as a project-local overlay because the pinned registry
  package is substantially older than the reviewed upstream revision.

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
