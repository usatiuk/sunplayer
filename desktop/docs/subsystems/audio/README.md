# Audio subsystem

## Status

Sunroom now has the first non-device audio boundary, but the application does
not yet produce audible playback.

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
                    deterministic tests                         physical device later
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

The initial controlled format is 48 kHz stereo because it gives stable fixture
expectations. A real cubeb output epoch will use one negotiated fixed format and
recreate its resampler when that format changes.

## Device and clock direction

The intended production backend is cubeb in shared mode. Sunroom will follow
the default output unless the product later exposes an explicit device choice.
The real-time callback receives only prepared PCM and bounded metadata. It may
copy samples, apply gain or mute, write silence, and update fixed-capacity
counters. It may not decode, allocate, block, lock application mutexes, log
synchronously, invoke Qt, or recover a device.

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

These tests do not prove real callback safety, backend position semantics, or
speaker-to-display A/V offset. Those require real cubeb tests and later physical
flash/impulse measurement.

See [the plan](PLAN.md) and
[ADR 0011](../../decisions/0011-single-pass-media-routing-and-audio-output-boundary.md).
