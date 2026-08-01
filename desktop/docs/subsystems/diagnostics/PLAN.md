# Diagnostics and observability plan

This plan is intentionally incremental. Add observations when a real
subsystem, failure mode, or validation question needs them; do not build a
general telemetry platform in advance.

## Milestone 1: Session logging and seek causality

* [x] Define subsystem-owned Qt logging categories.
* [x] Preserve console/debugger delivery and add a default session file.
* [x] Keep producer threads off file I/O with a bounded asynchronous sink.
* [x] Add explicit debug/file/disable command-line controls.
* [x] Bound session-file size and retained file count.
* [x] Test file formatting, file/queue bounds, concurrent flush and fatal
  behavior, custom-path isolation, Windows path policy, and Qt debug-category
  filtering.
* [x] Correlate open and seek records with playback generation.
* [x] Observe open, probe, seek, first packet, first decoded frame, normalized
  preroll, admission, cancellation, and completion.
* [x] Reproduce the reported large UHD network-file seek with an opt-in debug
  log and identify the first divergent boundary.
* [x] Add a deterministic regression for the discovered failure mode.
* [x] Fix the failure and record bounded seek behavior.

## Milestone 2: Typed playback and source progress

* [ ] Define a coalesced current-operation snapshot independent of log text.
* [ ] Track packet bytes/count, decoded preroll, queue occupancy, and source
  read progress without per-frame notifications.
* [ ] Distinguish opening, probing, seeking, buffering, source-stalled,
  decoder-stalled, and presentation-waiting states.
* [ ] Expose the latest structured media/playback failure with a stable code,
  stage, user-facing message, and diagnostic detail.
* [ ] Add cancellation and latest-request invariants for every progress state.

## Milestone 3: Rendering, presentation, and recovery

* [x] Migrate remaining raw graphics/video/presentation/platform messages to
  the shared categories.
* [ ] Publish effective source/target color state, field provenance,
  confidence, contradictions, and semantic revision.
* [ ] Publish source ICC presence/hash/application status and presentation
  calibration ownership without implying an unavailable transform.
* [ ] Publish window/display selection identity, capability provenance,
  presentation mode, surface encoding, and last target invalidation reason.
* [ ] Publish decoder/import/render/presentation adapter identity and device
  generation.
* [ ] Reconcile known CPU/GPU copies, synchronization mode, and fallback
  reason in one support snapshot.
* [ ] Record bounded device-loss and graphics-recovery transactions.

## Milestone 4: Audio and synchronization

The controlled sink already exposes typed generation, running state,
submitted/presented frame counts, media position, queue depth, and observed
capacity for deterministic tests. The cubeb sink and audio-master session are
now wired. A common low-rate sink snapshot exposes backend, PCM occupancy,
submitted/presented counts, clock reliability, and underruns; `MediaSession`
adds the current clock source. The Player renders the backend, clock source,
PCM occupancy, underruns, and current audio interruption; transition logging
exists, while high-rate timing and physical recovery traces remain.

* [x] Publish a typed low-rate audio sink and playback-clock snapshot without
  logging or signaling from the callback.
* [x] Log bounded audio-buffering state transitions outside the
  callback.
* [ ] Record the selected master clock and clock-anchor revisions.
* [ ] Track audio submitted/presented estimates, video selection/presentation,
  drift correction, underruns, and dropped frames.
* [ ] Add scenario traces that correlate seek completion across demux, audio,
  video, and presentation.
* [ ] Keep high-rate samples in bounded trace buffers rather than ordinary log
  files.

## Milestone 5: User-facing support tools

* [ ] Add a read-only diagnostics page for the active pipeline.
* [ ] Add a user-initiated support-bundle preview and export.
* [ ] Redact or omit sensitive paths and identifiers by default.
* [ ] Include coverage gaps and unavailable hardware validation explicitly.
* [ ] Never upload diagnostics automatically.

## Review questions

For each new observation, ask:

* Which concrete failure or decision does it distinguish?
* Is it a lifecycle event, current state, counter, or high-rate trace sample?
* What identity and unit make it unambiguous?
* Can it be produced without blocking a real-time or presentation path?
* Does normal logging avoid sensitive or excessively detailed data?
* Is the behavior tested through a typed seam rather than prose log parsing?
