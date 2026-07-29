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

## Milestone 2: libplacebo and first-frame coverage

* [x] Load the pinned libplacebo DLL through CTest and verify its version,
  installed D3D11, Shaderc, and built-in DOVI configuration, disabled Vulkan,
  OpenGL, and external libdovi features, runtime staging, and basic public-API
  lifecycle.
* [ ] Run the pinned libplacebo upstream suite as dependency validation where
  practical.
* [x] Render known sRGB and BT.2020/PQ software-backed frames through real
  libplacebo.
* [ ] Assert effective input metadata, target description, plane mapping,
  lifetime, synchronization, and copy path.
* [ ] Add one pinned SDR media container and manifest.
* [ ] Add one pinned HDR10 media container and manifest.
* [ ] Decode the first frame with real FFmpeg and render it with real
  libplacebo.
* [x] Capture the display-targeted video surface and final composition for the
  analytic libplacebo path.
* [x] Capture-validate reference-white normalization for SDR at 80, 100, and
  203 nits and a target-relative PQ diagnostic at 100 and 203 nits.
* [ ] Move one fixed, absolutely mastered PQ frame between different
  reference-white and peak targets without changing the source signal.
* [x] Keep the procedural and libplacebo diagnostic inputs aligned to the same
  pattern layout so their output differences exercise rendering policy.
* [x] Exercise a persistent 640×360 animated diagnostic input into a 1100×600
  target for 60 frames and report throughput without a shared-CI timing gate.
* [ ] Decide whether the growing corpus justifies OpenEXR/OpenImageIO.
* [ ] Treat FATE as optional pinned-FFmpeg dependency validation, not Sunroom
  integration coverage.

## Milestone 3: playback scenarios

* [ ] Add a controlled monotonic clock and audio sink when scheduling requires
  deterministic advancement.
* [ ] Cover short playback, pause, end of stream, buffering, and frame
  selection through real queues.
* [ ] Cover seek-generation invalidation without fixed sleeps.
* [ ] Add subtitle and audio fixtures with the features being implemented.
* [ ] Add structured completion events and diagnostics with generation IDs.
* [ ] Introduce an out-of-process local control channel only when several
  actual-application scenarios benefit from shared orchestration.
* [ ] Exercise startup, local-file open, playback commands, errors, and clean
  termination through the real binary.

## Milestone 4: dedicated systems coverage

* [ ] Real supported GPU and hardware-decode/import configurations.
* [ ] D3D, Vulkan, and Metal validation modes where applicable.
* [ ] Device loss, allocation failure, and long resource-lifecycle stress.
* [ ] Real operating-system display changes and multi-monitor movement.
* [ ] Controlled unreliable source and cancellation scenarios.
* [ ] Real SMB/NFS failure scenarios when mounted sources are supported.
* [ ] Packaged-application UI and clean-machine smoke tests.
* [ ] Stable-machine performance and power baselines.
* [ ] Guided and later instrumented physical HDR and A/V sync verification.

These jobs should report unavailable capabilities and fixtures explicitly.
They need not all block every change.
