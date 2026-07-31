# Testing subsystem plan

## Objective

Establish useful regression coverage from the start, then grow it alongside
real player boundaries. Avoid both extremes: postponing tests until the player
is large, and building a general scenario laboratory before there are scenarios
to run.

## Milestone 0: bootstrap current presentation tests

* [x] Add `include(CTest)` at the project root.
* [x] Discover and link `Qt6::Test` only when `BUILD_TESTING` is enabled.
* [x] Keep the first test self-contained; one test does not justify a shared
  test-support target.
* [x] Register an initial Qt Test executable with CTest.
* [x] Stage Windows runtime DLLs beside the test executable so loader failures
  cannot open an interactive missing-library dialog.
* [x] Suppress Windows critical-error and crash-reporting dialogs in the test
  process so failures return to the runner.
* [x] Extract a pure presentation-target policy from
  `PresentationOutputState` where required for strong tests.
* [x] Add table-driven coverage for:
  * [x] SDR fallback.
  * [x] Scene-referred scRGB SDR-white scaling.
  * [x] Windows HDR luminance precedence over QRhi data.
  * [x] Unknown luminance and SDR-white fallback.
  * [x] Current, potential, and effective headroom.
* [x] Label focused deterministic tests `unit` and the first real graphics test
  `gpu;windows`; add further labels only with new execution classes.
* [ ] Record the first guided Windows presentation matrix with OS, GPU, driver,
  display, mode, diagnostics, and result.

### Acceptance

* The default configured build can build and run its deterministic tests with
  one CTest command.
* Disabling `BUILD_TESTING` does not require the Qt Test component or build test
  targets.
* Presentation policy tests do not mock QWindow, WinRT, or QRhi merely to reach
  arithmetic and precedence logic.
* The runtime matrix distinguishes untested from passed scenarios.

## Milestone 1: video-surface contract coverage

Implement alongside graphics milestone 1:

* [x] Test every required field and invariant of the explicit rendered-surface
  description.
* [x] Test that a graphics-device generation change always invalidates a
  surface.
* [x] Test that a target display revision invalidates a display-targeted
  surface.
* [x] Test that producer-content changes invalidate the completed surface.
* [x] Test that swapchain-only recreation preserves a surface when its explicit
  contract permits reuse.
* [x] Add an analytically known pattern with independently calculated expected
  values.
* [x] Add a real D3D11 QRhi integration test:

  ```text
  pattern producer
  → RGBA16F video surface
  → final compositor
  → transfer-enabled texture
  → raw readback
  ```

* [x] Compare selected values or regions with declared tolerances.
* [x] Report backend and floating-point readback capability.
* [x] Cover active video-viewport geometry and visibility as pure state.
* [x] Exercise QML shell-to-viewport publication through the real components.
* [x] Capture UI-only composition with the video layer hidden.
* [x] Label the GPU test separately so dedicated runs can select it while it
  remains part of the current supported Windows default suite.

## Milestone 2: libplacebo and decoded-frame coverage

* [x] Load the pinned libplacebo DLL through CTest and verify its version,
  installed D3D11, Shaderc, and built-in DOVI configuration, disabled Vulkan,
  OpenGL, and external libdovi features, runtime staging, and basic public-API
  lifecycle.
* [ ] Run the pinned libplacebo upstream suite as dependency validation where
  practical.
* [x] Render known sRGB and BT.2020/PQ software-backed frames through real
  libplacebo.
* [x] Assert effective SDR input metadata, target description, plane mapping,
  lifetime, synchronization, and copy path.
* [x] Add one pinned SDR media container and manifest.
* [ ] Add one pinned HDR10 media container and manifest.
* [x] Seed the fixture layout with a pinned, hashed lossless RGB image and
  decode its first frame with real FFmpeg through the production libplacebo
  and QRhi capture path.
* [x] Enforce the seed fixture's manifest hash in the scenario.
* [x] Cover an analytically generated Matroska/FFV1 YUV420P stream with
  BT.709 limited-range metadata, timestamps, non-square pixels, exact decoded
  plane samples, and tolerant linear-RGB capture.
* [x] Cover a pinned H.264 stream through real D3D11VA decode and direct NV12
  plane import, assert zero input and output copies/transfers, and compare its
  capture against software decode.
* [x] Drain all three FFV1 and H.264 frames through the production continuous
  send/receive/flush path, including bounded simultaneous D3D11VA retention.
* [x] Make the registered hardware-decode CTest fail when D3D11VA is missing;
  retain explicit skip reporting only for direct manual execution.
* [x] Inject a failed post-selection result at the fallback-policy boundary and
  prove it retries in software while retaining the fallback reason.
* [x] Reject a real retained hardware frame in a replacement graphics
  generation and assert the producer emits the typed software-retry failure.
* [x] Capture the display-targeted video surface and final composition for the
  analytic libplacebo path.
* [x] Capture-validate reference-white normalization for SDR at 80, 100, and
  203 nits.
* [x] Move one fixed 1000-nit analytic PQ signal across 80-, 100-, and 203-nit
  reference whites on a constant 600-nit target without changing the source;
  verify an exact 203-nit patch at surface `1.0`, no expansion while the source
  fits, highlight compression when it does not, and the final Windows scRGB
  scale.
* [ ] Repeat the fixed mastered-PQ scenario with a pinned FFmpeg-decoded HDR
  fixture and effective source metadata.
* [ ] Validate a fixed HLG source across the same physical targets because its
  libplacebo OOTF is target-dependent.
* [x] Keep the procedural and libplacebo diagnostic inputs aligned to the same
  pattern layout so their output differences exercise rendering policy.
* [x] Exercise a persistent 640×360 animated diagnostic input into a 1100×600
  target for 60 frames and report throughput without a shared-CI timing gate.
* [ ] Decide whether the growing corpus justifies OpenEXR/OpenImageIO.
* [ ] Treat FATE as optional pinned-FFmpeg dependency validation, not Sunroom
  integration coverage.

## Milestone 3: playback scenarios

* [x] Cover initial Empty/Opening/Ready/Error session state, nonblocking
  cancellation and replacement, superseding generations, worker destruction,
  and presentation failure without fixed sleeps.
* [x] Cover typed hardware-import software retry and current-media re-decode
  after graphics-device invalidation/replacement, including the one-retry
  limit, logical-position preservation, and replacement-capability use by
  software-backed pipelines.
* [x] Drive the production clock-source-neutral snapshot and provisional
  monotonic clock with controlled presentation times.
* [x] Cover twelve-frame playback, pause/resume/replay, end of stream, hard
  frame-queue capacity, backpressure, refill, and due/drop selection through
  real queues.
* [x] Add a bounded controlled audio sink with distinct submitted and
  presented cursors, pause, generation reset, and backpressure coverage.
* [x] Add a hashed lossless A/V fixture with a nonzero shared timeline, visual
  flashes, audio impulses, sample-rate conversion, channel rematrixing, and
  resampler drain.
* [x] Exercise one invocation of the production shared FFmpeg media operation
  across both selected streams and assert media/sample behavior without
  coupling the regression to private open, packet, or decoder-call counts.
* [x] Assert aggregate packet count/byte observations stay within the declared
  budget or its explicit one-oversized-packet exception.
* [x] Add a focused shared-router saturation scenario that proves demux
  backpressure and stop wakeup independently of fixture packet counts.
* [x] Cover missing shared-origin rejection at the timing-policy boundary.
* [x] Drive the production session and video scheduler from the controlled
  presented-audio clock.
* [x] Cover shared-operation session seek/drain, opposite A/V start offsets,
  clean zero-output audio intervals, video outlasting audio, drained terminal
  position, hidden sink failure, sustained clock loss, and complete bounded
  playback without a presentation consumer with real pinned A/V fixtures where
  applicable.
* [x] Cover seek-generation invalidation without fixed sleeps, including
  rapid replacement, decoded preroll, and sparse-GOP/B-frame media.
* [ ] Add subtitle fixtures and extend audio fixtures only with implemented
  formats, layouts, discontinuities, or recovery behavior.
* [ ] Add structured completion events and diagnostics with generation IDs.
* [ ] Introduce an out-of-process local control channel only when several
  actual-application scenarios benefit from shared orchestration.
* [x] Exercise audio-first startup and local-file playback through the real
  binary, production audio/GPU dependencies, two distinct swapchain video
  revisions, and automatic clean termination.
* [ ] Extend real-binary scenarios to playback commands, errors, replacement,
  and controlled shutdown when the shared control boundary is justified.

## Milestone 4: dedicated systems coverage

* [x] First real supported GPU and D3D11VA decode/import configuration.
* [ ] Broader GPU, codec, bit-depth, and hardware-decode/import configurations.
* [ ] D3D, Vulkan, and Metal validation modes where applicable.
* [ ] Device loss, allocation failure, and long resource-lifecycle stress.
* [ ] Inject a failure after the real D3D11VA `get_format` selection inside
  the production decode attempt; the current deterministic test starts at the
  fallback-policy boundary.
* [ ] Real operating-system display changes and multi-monitor movement.
* [x] Controlled continuous-pipeline cancellation, queue stop wakeup, and
  stale-completion scenarios.
* [ ] Controlled unreliable continuous-source scenarios.
* [ ] Real SMB/NFS failure scenarios when mounted sources are supported.
* [ ] Packaged-application UI and clean-machine smoke tests.
* [ ] Stable-machine performance and power baselines.
* [ ] Guided and later instrumented physical HDR and A/V sync verification.

These jobs should report unavailable capabilities and fixtures explicitly.
They need not all block every change.
