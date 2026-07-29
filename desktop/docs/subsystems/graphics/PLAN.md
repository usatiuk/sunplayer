# Graphics and display subsystem plan

## Objective

Turn the current Windows HDR presentation playground into the shared
presentation boundary for decoded video, while preserving explicit color
contracts, one-device composition, demand-driven rendering, and observable
fallback behavior.

The device, producer, target-interop, and generic active-viewport seams now
wrap the retained HDR Lab. The first libplacebo producer and direct D3D11
target are implemented and still precede playback.

## Completed foundation

The current prototype establishes:

* [x] Application-owned D3D11 QRhi and visible swapchain.
* [x] Qt Quick redirected into an application-owned RGBA16F texture.
* [x] Final fullscreen QRhi composition pass.
* [x] Extended-linear sRGB/scRGB presentation with SDR fallback.
* [x] Combined QRhi, Qt screen, and Windows Advanced Color state.
* [x] Window movement and display-mode invalidation.
* [x] Demand-driven render scheduling with explicit animation.
* [x] Resize, surface destruction, swapchain invalidation, and bounded
  device-loss recovery.
* [x] Basic presentation diagnostics and manual reprobe.
* [x] Procedural HDR pattern for presentation testing.
* [ ] Recorded manual runtime validation across the supported Windows display
  scenarios.
* [x] Automated presentation-policy and video-surface-state tests.
* [x] Analytic D3D11 producer/compositor readback smoke test.
* [ ] Renderer image corpus and cross-backend GPU tests.

## Milestone 1: explicit video surface and narrow compositor

Move the diagnostic pattern through the future video boundary without
introducing FFmpeg or libplacebo yet.

### Work

* [x] Define the minimal display-targeted surface description alongside its
  first producer and consumer.
* [x] Create an offscreen RGBA16F video render target on the engine QRhi.
* [x] Render the existing diagnostic pattern into that target.
* [x] Bind the video and Qt Quick textures in the final compositor.
* [x] Reduce the final compositor to layer geometry, alpha composition, and
  swapchain encoding.
* [x] Remove source tone mapping and pattern generation from the final shader.
* [x] Track graphics-device and display revisions needed to invalidate a
  rendered surface.
* [x] Preserve valid texture ownership through resize, swapchain recreation,
  and device recreation.
* [x] Report the active video-surface format and producer in diagnostics.
* [x] Add focused tests for surface-description and invalidation logic where
  it can be kept independent of QRhi.

### Acceptance

* The playground looks and behaves the same in SDR and extended-linear output.
* The final compositor no longer interprets source HDR metadata or tone maps
  source content.
* The diagnostic surface has one explicit, documented color and luminance
  contract.
* Swapchain-only recreation does not unnecessarily recreate display-independent
  resources.
* Display-dependent surfaces are rerendered after a target display change.
* Device loss cannot leave a surface from the old QRhi generation bound.

The implementation and pure-state coverage are complete. Build and CTest
verification pass. Visual equivalence in SDR and extended-linear modes remains
part of the recorded manual Windows presentation matrix; the application was
also launched successfully in an automated four-second startup liveness smoke,
but that does not assert visual or color correctness.

## Milestone 2: shared graphics and video-rendering seams

Establish the boundaries already required by the supported rendering backends
before the first libplacebo implementation fixes D3D11 assumptions into shared
code.

### Work

* [x] Extract a factory-selected graphics-device domain from direct D3D11
  startup and ownership.
* [x] Define the rendered-video producer lifecycle, invalidation, successful
  completion, and composition-texture contract.
* [x] Route the diagnostic producer through that contract without changing its
  output.
* [x] Define result-bearing target provisioning, producer access, composition
  preparation, submission acceptance/abort, and texture-revision contracts.
* [x] Implement the direct QRhi target and output-path diagnostic schema.
* [x] Implement native D3D11 libplacebo target selection with direct sharing.
* [ ] Add same-device GPU-copy and explicit CPU target fallbacks when a real
  unsupported-direct-path case requires them.
* [x] Keep D3D11, Vulkan, Metal, IOSurface, queue, semaphore, and decoder-native
  types inside backend implementations.
* [x] Preserve deterministic teardown and device-generation invalidation.

### Acceptance

* The current diagnostic output and demand-driven behavior remain unchanged.
* The presentation engine depends on shared contracts rather than constructing
  the D3D11 implementation directly.
* Backend selection, synchronization mode, known copies/transfers, and fallback
  reasons are queryable without native types for each implemented target.
* Tests cover accepted submissions with committed and discarded rendered
  states, invalidation, target resizing, and backend target state at the
  strongest practical boundary.

The shared source/producer lifecycle, direct QRhi target, and optional
composition layer are implemented. The Windows build, focused CTest targets,
real D3D11 capture, and hidden startup smoke pass. Tests drive the diagnostic
source and producer through shared interfaces, verify direct-target diagnostics
with no copies, exercise committed and discarded render states after accepted
submissions, resize and rebind the native texture, verify UI-only composition
without sampling video, and load the pinned libplacebo dependency. Native
libplacebo direct interop is now covered in milestone 3; actual fallback
selection remains open.

## Milestone 3: libplacebo renderer

Replace the temporary producer with a persistent libplacebo renderer.

### Work

* [x] Add the versioned D3D11-only libplacebo dependency and verify its
  installed feature configuration and public-API lifecycle.
* [x] Implement and validate the Windows D3D11 target bridge against the
  application-owned graphics-device domain.
* [x] Create persistent libplacebo log, GPU, and renderer objects.
* [x] Render known sRGB and BT.2020/PQ software-backed images into the video
  surface.
* [x] Normalize libplacebo's 203-nit linear convention to the active
  reference-white surface convention and capture it at 80, 100, and 203 nits.
* [x] Reuse the renderer across size, source, and display changes.
* [x] Expose backend, render target, zero-copy, synchronization, and failure
  diagnostics.
* [x] Keep the animated diagnostic input at a persistent source-frame size
  independent of the viewport, report its CPU upload separately from output
  copies, and exercise sustained submission without a universal timing gate.
* [ ] Add render timing diagnostics.
* [ ] Record image-test fixtures for SDR, HDR-to-HDR, and HDR-to-SDR behavior.

### Acceptance

* libplacebo, not the final compositor, performs video color processing.
* A known input produces repeatable output in SDR and extended-linear modes.
* The normal test path does not perform an accidental GPU-to-CPU round trip.
  Its one intentional software-frame CPU-to-GPU upload is reported separately
  from output-target copies.
* Unsupported output-target interop produces an explicit same-device GPU-copy
  or CPU-round-trip fallback; software-frame upload remains a separate input
  path.

The direct D3D11 approach and reference-white normalization are verified
against the pinned library and Qt versions. Completion of the broader renderer
milestone still requires a maintained image corpus, render timing, and
realization of non-direct target paths if supported hardware requires them.

## Milestone 4: first decoded video frame

Feed one FFmpeg-decoded software frame through libplacebo and keep it displayed
without a playback clock. This proves the required software path; normal
playback should prefer the later platform hardware importer when available.

### Work

* [x] Accept a local file selected by the application shell.
* [x] Prove local open/probe through FFmpeg against a pinned headless fixture.
* [x] Select the default video stream and decode its first displayable frame in
  the headless integration boundary.
* [x] Retain explicit time base, effective sample aspect ratio, frame identity,
  geometry, storage, signal diagnostics, and exact side data.
* [x] Map or upload a software `AVFrame` through the production libplacebo
  boundary.
* [x] Preserve the decoded frame so target-only changes can rerender it without
  another input upload.
* [ ] Normalize full effective color metadata with provenance.
* [x] Apply decoded sample aspect ratio to an aspect-preserving content
  rectangle.
* [ ] Capture-validate general display-matrix orientation.
* [ ] Surface loading and decode errors without terminating the UI.
* [ ] Display source pixel format, dimensions, color metadata, decode path,
  render path, and copies in diagnostics.

### Acceptance

* A supported local SDR or HDR file displays its first frame.
* The UI remains responsive while the file is opened and decoded.
* Moving the paused frame to another display rerenders it for the new target.
* Unsupported media produces a clear error and leaves the presentation shell
  usable.

Playback queues, continuous scheduling, seeking, and audio remain later
playback/media milestones. The first Windows hardware path is milestone 5.

## Milestone 5: Windows hardware decode and direct input import

Prefer the native decoder path without changing the decoded-frame, source,
libplacebo producer, or compositor contracts.

### Work

* [x] Create the Windows D3D11 device with video support and import that exact
  device/context into QRhi and libplacebo.
* [x] Give FFmpeg a referenced D3D11VA hardware-device context with
  shader-resource decoder surfaces.
* [x] Enable D3D11 multithread protection and serialize FFmpeg device callbacks
  with QRhi/libplacebo GPU resource and command phases.
* [x] Negotiate decoder hardware formats through FFmpeg's public codec
  configuration API.
* [x] Retain the hardware `AVFrame`, device generation, texture array, and
  slice at the decoded-frame boundary.
* [x] Directly wrap NV12, P010, P012, and P016 D3D11 plane views for
  libplacebo with explicit effective depth and storage shift.
* [x] Retry the entire first-frame operation in software after a configured
  hardware decode failure and expose the fallback reason.
* [x] Retry once in software when a decoded hardware surface cannot be
  imported, without making that policy part of the native importer.
* [x] Supersede old-generation decode/frame state and re-decode current media
  after graphics-device recreation while retaining a ready
  generation-independent software frame.
* [x] Prove real H.264 D3D11VA decode/direct import against a pinned fixture.
* [ ] Capture P010/P012/P016 and real device-loss behavior.
* [ ] Measure decoder/render contention during continuous playback.

### Acceptance

The real H.264 scenario returns D3D11 storage belonging to the active graphics
generation, renders through the production libplacebo target, reports zero
input CPU transfers and GPU copies, and agrees with software decode within
declared floating-point tolerances. The unsupported FFV1 hardware path falls
back to software with an explicit reason.

## Backend realization

The first slice defines the known shared contracts; later platform work
exercises and refines them from evidence without changing their semantic
responsibilities.

* [ ] Implement macOS Metal/EDR presentation and display observation.
* [ ] Implement Linux Vulkan presentation and available compositor/display
  observation.
* [ ] Validate backend-neutral orientation, texture, synchronization, and
  device-generation contracts.
* [ ] Measure native import feasibility and costs before promising zero-copy
  behavior on each platform.

## Validation matrix

For each implemented milestone, validate at least:

| Scenario | Expected behavior |
| --- | --- |
| SDR output | Correct sRGB encoding and no HDR headroom claims |
| HDR output | Extended-linear presentation and correct SDR-white placement |
| HDR disabled on capable display | State and swapchain settle without mismatched encoding |
| Move between unlike displays | Swapchain is recreated and display-targeted content rerenders |
| DPI or window resize | UI and video geometry remain aligned |
| Animation disabled | No continuous presentation loop |
| Swapchain out of date | Resize/retry without stale render-pass resources |
| Device loss | Old-generation resources are destroyed before bounded recovery |

Performance, power, and copy-path measurement become required once decoded
frames and libplacebo exist.
