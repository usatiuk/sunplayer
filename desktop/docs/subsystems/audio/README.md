# Audio subsystem

## Status

Sunroom now has both the deterministic audio boundary and the first physical
Windows output implementation, but the application does not yet route decoded
audio into that sink and therefore does not yet produce audible playback.

The dependency graph includes FFmpeg libswresample and a pinned current cubeb
overlay. A shared FFmpeg media operation opens and probes one source once,
routes selected audio and video packets under one global count/byte budget,
decodes FLAC audio with the real FFmpeg decoder, and converts it to 48 kHz
stereo native-endian interleaved float32 PCM. The same operation decodes video
and preserves a shared normalized timestamp origin.

The synchronized operation is not wired into `MediaSession` yet. Both it and
the production video-only path feed one packet-level decoder that owns codec
setup, D3D11VA negotiation, metadata merge, send/receive/drain, and frame
publication. The synchronized regression deliberately exercises software
frames; production migration still has to pass the active graphics capability
and preserve whole-operation hardware fallback. Opening the media a second
time for audio is not an acceptable intermediate architecture.

`ControlledAudioSink` is a bounded deterministic test edge. It models
submission, presentation, pause, reset, and media-position mapping without an
operating-system device. It is deliberately lock-based and must not be reused
as cubeb's real-time callback buffer.

`CubebAudioSink` opens the default WASAPI output through the pinned cubeb
build. All cubeb lifecycle and position calls run on one dedicated MTA control
thread. Its callback consumes a preallocated SPSC float queue, writes bounded
underrun silence, and publishes fixed-capacity output-to-media spans. It does
not allocate, block, take application locks, decode, log, invoke Qt, or perform
device recovery.

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
absolute endpoint or reduced by `start_time`. Once both selected streams reach
EOF, the operation replaces it with the maximum observed normalized audio/video
endpoint and marks that duration final. This derives the exact fixture duration
without a second parse or tail read.

## PCM contract

Each `PcmAudioBlock` contains:

* Playback generation.
* Absolute converted-stream frame index.
* Normalized media timestamp of its first frame.
* Sample rate and channel count.
* Native-endian interleaved float32 samples.

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

The integration scenario crosses one real FFmpeg open/probe/read operation,
both decoders, libswresample conversion and drain, shared timestamp
normalization, and bounded PCM submission. It asserts behavior at media and
sample boundaries rather than FFmpeg packet or decoder-call counts. Focused
sink tests verify that submitted audio does not become presented until the
controlled device advances, pause freezes presentation, capacity is bounded,
and generation reset wakes blocked work.

The callback-boundary tests additionally verify ring wrap, whole-block atomic
publication, sticky stop/reset cancellation, zero fill, hold-silence mapping,
and bounded ledger overwrite. A Windows integration scenario explicitly
selects cubeb's WASAPI backend, opens the default endpoint on the sink's MTA
thread, and exercises silent preroll, start, position observation, pause,
generation reset, drain, and destruction. A focused regression proves that a
PCM block cannot create a preroll/capacity dependency cycle.

These tests do not yet prove audible decoded playback, device migration,
callback timing under pressure, injected cubeb failures, or
speaker-to-display A/V offset. Those require production session integration,
real-device fault scenarios, and later physical flash/impulse measurement.

See [the plan](PLAN.md) and
[ADR 0011](../../decisions/0011-single-pass-media-routing-and-audio-output-boundary.md).
