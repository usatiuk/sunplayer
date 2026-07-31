# Audio subsystem plan

## Objective

Make presented audio the ordinary playback clock while preserving one source
read, bounded memory, deterministic synchronization tests, callback safety, and
explicit recovery semantics.

## Milestone 0: dependency and deterministic boundary

* [x] Enable FFmpeg libswresample without enabling unrelated FFmpeg features.
* [x] Add a Windows-only project-local cubeb overlay pinned to a reviewed
  upstream commit; do not claim unbuilt macOS/Linux backends.
* [x] Keep Windows cubeb packaging native; defer Rust backend packaging until
  macOS and Linux are exercised.
* [x] Verify cubeb's public C ABI can compile, link, and load and reports the
  compiled WASAPI backend without requiring COM initialization or an available
  audio endpoint.
* [x] Define interleaved float32 PCM blocks with generation, sample index, and
  media timestamp.
* [x] Add a bounded controlled sink with distinct submitted and presented
  cursors.

## Milestone 1: single-pass synchronized decode

* [x] Open, probe, seek, and read one `AVFormatContext` per playback attempt.
* [x] Route selected audio and video packets under one aggregate budget with
  an explicit one-oversized-packet exception.
* [x] Add focused router saturation/cancellation coverage before production
  adoption, including the one-oversized-packet exception.
* [x] Decode real audio and video concurrently without fixed sleeps.
* [x] Convert and fully drain 32 kHz mono audio into 48 kHz stereo float32 PCM.
* [x] Preserve one nonzero timeline origin across both streams.
* [x] Add a hashed lossless flash/impulse fixture and behavioral seek/drain
  regression scenario.
* [x] Route both video-only and synchronized operations through one
  hardware-capable packet decoder.
* [ ] Pass the active graphics capability through synchronized production
  playback and preserve whole-operation hardware fallback.
* [ ] Make `MediaSession` use the shared operation without retaining the old
  video-only demux path in parallel.

## Milestone 2: physical cubeb output

* [ ] Add a preallocated SPSC PCM and timestamp-metadata boundary.
* [ ] Implement `CubebAudioSink` with no allocation, blocking, application
  locks, synchronous logging, Qt calls, decode, or recovery in its callback.
* [ ] Negotiate a stable format for each audio-output epoch and rebuild
  libswresample when it changes.
* [ ] Implement gain, mute, preroll, drain, and short-underrun silence.
* [ ] Expose submitted position, cubeb playback position, observation time,
  reported latency, confidence, device identity where available, callback
  cadence, and underrun counters.
* [ ] Verify default-device playback and position monotonicity on Windows.
* [ ] In the device-backed suite, initialize COM explicitly, request WASAPI by
  name, and report endpoint unavailability separately from packaging failure.

## Milestone 3: audio-master playback

* [ ] Map presented backend frames back to normalized media time.
* [ ] Feed the existing `MediaClockSnapshot` and `VideoFrameScheduler` without
  giving the audio backend ownership of playback policy.
* [ ] Keep mute advancing media time; distinguish source silence from
  underrun/recovery hold silence.
* [ ] Invalidate packets, decoded frames, PCM, metadata, and observations on one
  seek generation boundary.
* [ ] Cover play, pause, seek, drain, no-audio fallback, short underrun, and
  large discontinuity through the controlled sink.
* [ ] Add ordinary audio/video position, queue, underrun, and drop diagnostics
  without logging from the callback.

## Milestone 4: device recovery and supported platforms

* [ ] Separate user play intent from buffering and device-recovery state.
* [ ] Validate default-device change, USB removal, Bluetooth disconnect and
  reconnect, sleep/wake, and service interruption on Windows.
* [ ] Decide from measurements whether cubeb automatic migration or explicit
  stream recreation gives the clearest epoch semantics.
* [ ] Package and validate cubeb's current macOS and Linux backend choices with
  locked Rust dependencies if those backends remain selected upstream.
* [ ] Validate PulseAudio and PipeWire-Pulse behavior on supported Linux
  configurations.

## Milestone 5: measurement-led refinement

* [ ] Measure callback jitter, queue horizons, latency changes, long-run drift,
  video lateness, and dropped-frame distributions on stable machines.
* [ ] Measure physical flash-to-impulse offset with a common acquisition
  timebase.
* [ ] Consider bounded resampler compensation only if measurements establish a
  persistent rate error against an independent master.
* [ ] Add multichannel policy and coverage after stereo playback is dependable.

## Deliberately deferred

* Exclusive mode and Windows RAW mode.
* Encoded HDMI passthrough.
* Arbitrary DSP graphs and audio-backend plugins.
* Continuous audio-rate correction without measured need.
* Gapless switching, crossfade, and per-device calibration databases.
