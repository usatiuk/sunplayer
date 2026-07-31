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
* [ ] Add a real or controllable asymmetric-interleave regression before
  claiming robust arbitrary/network playback; use its failure to shape any
  per-stream soft watermark or reservation policy.
* [x] Decode real audio and video concurrently without fixed sleeps.
* [x] Start both decoder workers before demuxing and keep packet admission
  independent of first-frame or first-PCM readiness.
* [x] Convert and fully drain 32 kHz mono audio into 48 kHz stereo float32 PCM.
* [x] Preserve one nonzero timeline origin across both streams.
* [x] Preserve non-coincident stream starts with leading source silence and
  due-first-video scheduling.
* [x] Add a hashed lossless flash/impulse fixture and behavioral seek/drain
  regression scenario.
* [x] Route both video-only and synchronized operations through one
  hardware-capable packet decoder.
* [x] Pass the active graphics capability through synchronized production
  playback and preserve whole-operation hardware fallback.
* [x] Make `MediaSession` use the shared operation without retaining the old
  video-only demux path in parallel.

## Milestone 2: physical cubeb output

* [x] Add a preallocated SPSC PCM and timestamp-metadata boundary.
* [x] Implement `CubebAudioSink` with no allocation, blocking, application
  locks, synchronous logging, Qt calls, decode, or recovery in its callback.
* [ ] Negotiate a stable format for each audio-output epoch and rebuild
  libswresample when it changes.
* [x] Implement preroll, drain, and short-underrun hold silence.
* [x] Implement gain and mute without changing media-clock progression.
* [x] Expose submitted media frames, mapped presented media frames, raw cubeb
  playback position, reported latency, confidence, device-change capability,
  and underrun counters.
* [ ] Add monotonic observation time, device identity where available, and
  callback cadence/jitter diagnostics.
* [ ] Verify default-device playback and position monotonicity on Windows.
* [x] In the device-backed suite, initialize COM on the sink's dedicated MTA
  thread, request WASAPI by name, and distinguish stream-open failure from
  dependency loading.

## Milestone 3: audio-master playback

* [x] Map presented backend frames back to normalized media time.
* [x] Feed the existing `MediaClockSnapshot` and `VideoFrameScheduler` without
  giving the audio backend ownership of playback policy.
* [x] Keep mute advancing media time; distinguish source silence from
  underrun/recovery hold silence.
* [x] Invalidate packets, decoded frames, PCM, metadata, and observations on one
  seek generation boundary.
* [x] Cover play, pause, seek, staggered starts, clean zero-output audio
  intervals, drain without a live position, trailing video after audio drain,
  terminal sink failure, and sustained presentation-clock loss through the
  controlled sink.
* [x] Keep bounded video selection and timeline notifications progressing when
  no presentation consumer is active.
* [x] Cover sustained underrun and frozen media time through the controlled
  sink and real-FFmpeg session boundary.
* [ ] Cover latency change and large-discontinuity recovery through the
  controlled sink.
* [x] Add ordinary audio/video position, queue, underrun, and drop diagnostics
  without logging from the callback.

## Milestone 4: device recovery and supported platforms

* [x] Separate user play intent from `Buffering`; record pause intent before
  observing the fallible sink boundary.
* [x] Keep sustained hold-silence frozen and observable without falling back
  to a monotonic clock; keep established-clock loss terminal until physical
  output replacement exists.
* [ ] Detect a valid but non-advancing physical position with a grace-based
  progress watchdog; repeated equal `IAudioClock` samples are allowed within
  the grace window.
* [x] Disable Cubeb-managed default-device switching so opaque migration cannot
  redefine the current output epoch.
* [x] Patch the pinned WASAPI render loop so a disabled-switching session
  reconfigure fails before silently replacing the same endpoint's client.
* [x] Enumerate and open the Windows multimedia default explicitly as the
  selected device for Sunroom-owned replacement.
* [x] Identify every presentation/diagnostic observation by an audio-output
  epoch distinct from playback generation; reject an unanchored epoch change.
* [ ] After selected-device loss, recreate only device-dependent
  output/conversion state, re-anchor a new epoch at the last confident
  presented position, bound retry, and require a new presentation observation
  before resuming.
* [ ] Preserve the single demux/video pipeline across ordinary audio-device
  replacement; use a full media-generation restart only when buffered timeline
  state cannot be reconciled.
* [ ] Validate default-device change, USB removal, Bluetooth disconnect and
  reconnect, sleep/wake, and service interruption on Windows.
* [x] Select explicit stream recreation over Cubeb's opaque migration for
  trustworthy epoch semantics.
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
* Click-free gain ramps until real callback cadence measurements establish an
  appropriate bounded ramp policy.
