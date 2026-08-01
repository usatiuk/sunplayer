# Video rendering and color-management plan

This plan takes the current real-video renderer to trustworthy SDR and HDR
playback without introducing a second tone mapper, source parser, or parallel
media operation. It complements the implementation summary in
[README.md](README.md).

SDR, PQ, HLG, HDR10+, and Dolby Vision are all V1 inputs. They use one
FFmpeg/libplacebo path whenever the libraries can decode and map them; Sunroom
does not gate playback on a format-specific implementation. Color-correctness
claims remain scoped to representative fixtures and target models that have
actually been validated.

## Product invariants

* Normal playback adapts every HDR format to the active desktop reference white
  and usable display headroom.
* Sunroom's linear working surface uses `1.0 = active reference white` and
  permits negative and greater-than-one components.
* Libplacebo owns content transfer interpretation, tone mapping, and gamut
  mapping. The final compositor performs no video-only repair.
* The final presentation conversion occurs once after video, UI, and subtitles
  are composed.
* Media is opened, probed, read, and decoded by the existing single shared
  operation; color work never creates a second parsing path.
* System-managed presentation relies on the OS/compositor for the final
  display-profile calibration. Explicit display ICC is deferred.
* Claims are format- and platform-specific until proved by production-boundary
  captures and, for physical output, measurement.

## Stage 0: Rendering and playback foundation

* [x] Retain reference-counted FFmpeg frames across decode and presentation.
* [x] Keep one persistent libplacebo renderer and reusable software uploads.
* [x] Import D3D11VA NV12 directly on the shared graphics device.
* [x] Render into an RGBA16F display-targeted surface and compose once through
  QRhi.
* [x] Exercise analytic SDR and static-PQ reference-white changes.
* [x] Play real local A/V through the single-pass media operation.

The analytic PQ test proves the numerical static-PQ bridge, not general HDR
format correctness or emitted luminance.

## Stage 1: FFmpeg/libplacebo format acceptance and target integration

This is the immediate next implementation milestone. FFmpeg and libplacebo
remain responsible for decoding, source transfer interpretation, dynamic HDR
metadata, tone mapping, and gamut mapping. Sunroom does not implement separate
SDR, PQ, HLG, HDR10+, or Dolby Vision pipelines. Its work is to preserve the
library inputs, describe the display target without contradictory units, reset
library state at discontinuities, and prove that the production path actually
uses the expected library feature or documented fallback.

The milestone proves that the one production path accepts representative SDR,
PQ, HLG, HDR10+, and Dolby Vision inputs. Missing evidence narrows a
color-correctness claim; it does not create a second renderer or deliberately
reject a source the libraries can already show.

### Library boundary and diagnostics

* [x] Keep `VideoSignalDescription` as a small scalar diagnostic snapshot and
  inspect dynamic side data directly on the retained FFmpeg frame; do not
  introduce a parallel source-color policy object.
* [x] Remove redundant blanket stream-to-frame mutation while preserving the
  stream evidence that FFmpeg does not attach to every frame.
* [ ] Expose source colorimetry, dynamic-metadata presence, libplacebo mapping
  path, and explicit fallback in diagnostics. In particular, report whether
  parsed Dolby Vision metadata produced a libplacebo reshape or the decoded
  base layer was shown; do not infer base-layer compatibility from appearance.
* [x] Preserve source ICC bytes through the retained frame and report presence
  and size; do not claim an ICC transform while LCMS is disabled. Detailed
  profile validation remains deferred with application-managed ICC output.

### One display-target integration

The current renderer already disables inverse tone mapping, peak detection,
and dithering. It inherits libplacebo 7.360.1's spline tone mapper and
perceptual gamut mapper through default parameter structs. The immediate
policy change is intentionally small: assign those same choices explicitly,
give the policy a diagnostic identity, and protect it with production-boundary
captures. This freezes today's appearance without enabling another processing
stage or prematurely adding quality presets.

* [ ] Explicitly select libplacebo spline tone mapping and perceptual gamut
  mapping, keep inverse tone mapping disabled, keep peak detection disabled,
  and keep dithering disabled for the RGBA16F target. Report this initial
  policy in diagnostics so a future library upgrade cannot silently redefine
  playback.
* [ ] Add a source-discontinuity reset hook to the persistent renderer and
  drive it from open, seek, track change, and generation replacement. The hook
  clears libplacebo temporal source state with `pl_renderer_flush_cache()`
  when such state is in use; it does not recreate the renderer, graphics
  device, target, or retained source upload. Display-target and window changes
  are not source discontinuities and must not reset it.
* [ ] Keep dynamic peak detection disabled for the first acceptance pass.
  Consider enabling it only if representative missing or unreliable source
  metadata demonstrates a visible benefit and its cost and seek/scene behavior
  pass the same production-boundary tests.
* [ ] Express the physical display peak, active reference white, and output
  normalization through supported libplacebo semantics for every input. The
  existing relative/static-PQ adapter may be retained only where its virtual
  coordinate construction has been numerically validated; its destination
  values are deliberately not described as literal physical nits.
* [ ] Render a controlled HLG fixture through the current adapter and compare
  its target response at multiple physical peaks and reference whites. Source
  inspection identifies a plausible OOTF/virtual-target mismatch, but the
  experiment decides whether it is material in the production path. If it is,
  first use an existing libplacebo-supported configuration; otherwise take a
  focused issue or API proposal upstream. Do not add a Sunroom HLG mapper or
  preselect a dependency patch before the result exists.
* [ ] Keep HDR10+ source metadata unchanged, including its source-authored
  targeted-system-display luminance, and supply the current physical display
  peak separately through the libplacebo destination.
* [ ] Report whether libplacebo applied Dolby Vision reshaping or displayed the
  decoder's base-layer result. Do not parse Dolby Vision profiles or implement
  missing trims/residual processing in Sunroom.

### Production-boundary acceptance matrix

* [ ] Add small redistributable FFmpeg-decoded fixtures representing BT.709
  SDR, BT.2020 SDR, PQ/HDR10, HLG, HDR10+ scene transitions, and Dolby Vision
  reshape/base-layer behavior. These are integration evidence for FFmpeg and
  libplacebo, not an exhaustive profile matrix or duplicate implementation.
* [ ] Render every fixture through the production importer, persistent
  libplacebo renderer, RGBA16F target, and QRhi compositor at multiple
  reference-white/headroom targets.
* [ ] Assert source evidence, active libplacebo path, reference-white
  adaptation, physical-target semantics, no unintended expansion, monotonic
  bounded compression, target-only rerender without re-import, dynamic-state
  reset, and exactly one final Windows coordinate conversion appropriate to
  the active HDR scene-referred or SDR display-referred mode.

Immediate milestone outcome:

* All representative formats enter the same retained-frame/import/render path.
* SDR and static PQ retain the validated numerical model.
* HLG's observed target response is recorded; an incorrect result is fixed at
  the libplacebo integration/API boundary rather than hidden by a second
  Sunroom color pipeline.
* HDR10+ metadata reaches libplacebo without stale carry-over.
* Dolby Vision diagnostics distinguish mapped reshape from decoded base layer.
* Unverified target behavior remains visible without blocking otherwise valid
  playback.

Existing files that already decode and render must remain playable while this
milestone replaces experimental target behavior with verified support. A new
failure is acceptable only when FFmpeg/libplacebo genuinely lacks a valid path
for that source and the fallback or error is explicit; it is not a reason to
build a competing Sunroom implementation.

## Stage 2: Simplify live Windows target reconciliation

This is not a greenfield display observer. The current implementation already:

* [x] Caches an HWND-bound WinRT `DisplayInformation` and observes
  `AdvancedColorInfoChanged`.
* [x] Publishes active HDR mode, SDR white, minimum luminance, and maximum
  luminance, with QRhi swapchain HDR information as a fallback.
* [x] Reacts to `QWindow::screenChanged` and debounced movement.
* [x] Rerenders a paused target-dependent frame when the detected reference
  white or headroom changes.

The accepted design treats native events as hints and the latest semantic
`PresentationTarget` as truth. Windows' HWND-bound `DisplayInformation` already
tracks the window and is the Advanced Color authority. Sunroom will not add a
cross-platform hierarchy of selection, capability, provider, topology, or
query revisions, nor a competing greatest-intersection display selector.

* [ ] Remove `displayTargetRevision` from the rendered-video reuse key. Record
  every video-affecting target value directly in the surface request and reuse
  it when those values are equal.
* [ ] Stop recreating the swapchain merely because `QScreen` identity changed.
  Recreate or reconfigure only when the presentation format, declared color
  encoding, HDR/SDR mode, adapter/device, or native resource lifetime changes.
* [ ] Route movement, `screenChanged`, Advanced Color changes, hotplug, and
  resume through one idempotent latest-state reprobe. Coalesce bursts lightly;
  do not build a mandatory asynchronous probe pipeline for synchronous Windows
  state.
* [ ] Keep native display identity, reported full-frame peak, raw capability
  values, update reason, and timestamps as optional diagnostics. Rendering
  consumes one effective target peak and does not use confidence scores as a
  correctness mechanism.
* [ ] Add deterministic semantic-policy coverage proving equal targets reuse a
  paused video surface, material target changes rerender it, and presentation
  mode changes request only the required swapchain work.
* [ ] Record `SystemManaged` for a correctly tagged Advanced Color surface,
  distinguish HDR scene-referred scRGB scaling from SDR Advanced Color/WCG
  display-referred FP16, and record `UnmanagedSrgb` only for ordinary SDR with
  Advanced Color inactive.

Wayland's compositor description identity remains a local asynchronous
protocol lifetime. It does not justify extra shared display revisions on
Windows or macOS. See
[the reconciliation research](../../research/2026-08-01-display-audio-migration-reconciliation.md)
and [ADR 0016](../../decisions/0016-reconcile-output-changes-semantically.md).

## Stage 3: Target gamut and composition verification

* [ ] Supply trusted display/target primaries separately from the extended
  BT.709/scRGB coordinate basis used by the RGBA16F surface.
* [ ] Prove that negative and greater-than-one components survive libplacebo,
  QRhi composition, and the selected swapchain.
* [ ] Verify Qt Quick and a synthetic subtitle-style layer enter the final
  blend with the intended linear and premultiplied-alpha semantics. The future
  subtitle subsystem must reuse and revalidate this seam; it is not a
  prerequisite for the video-color gate.
* [ ] Capture system-managed and unmanaged-sRGB policy selection without
  applying display ICC in the video renderer.
* [ ] Perform the initial Windows HDR physical check for reference white,
  representative highlights, and target gamut with a documented display and
  measurement method.
* [ ] Rerender a paused video surface only when source, target, policy, device,
  interop, or relevant extent semantics change; ordinary UI updates recompose
  only.

Gate: Windows presentation color correctness for the Stage 1 input formats is
not claimed until representative core scenarios, Stage 2 live-target facts,
this stage's target-gamut and extended-composition checks, and the initial
physical check pass.

## Stage 4: Player color reliability and expanded corpus

* [ ] Expand real fixtures for BT.601/709 limited and full SDR, BT.2020 SDR,
  8/10/12-bit storage, chroma locations, missing and contradictory metadata,
  geometry changes, and track changes.
* [ ] Publish one structured source/display/rendering diagnostic snapshot with
  policy identities, revisions, provenance, copy counts, and fallback reason.
* [ ] Remove HDR Lab target overrides from production Player state so
  diagnostic experimentation cannot silently change ordinary playback.
* [ ] Add Player-level open, pause, seek, display-change, device-recovery, and
  shutdown scenarios around the color pipeline.

## Stage 5: Hardware-frame coverage

* [ ] Capture P010, P012, and P016 D3D11VA direct import.
* [ ] Compare at least one metadata-bearing P010 HDR source through software
  decode and D3D11VA import, asserting equivalent effective color evidence,
  libplacebo mapping selection, and captured output within backend tolerance.
  Capability-gate any hardware format that has not passed this differential.
* [ ] Implement and diagnose same-device GPU-copy and explicit CPU fallback
  paths where needed.
* [ ] Retain every decoder/native surface until GPU completion and reject
  adapter/device mismatches before recording commands.

Windows V1 color release gate:

* The single production FFmpeg/libplacebo path plays and diagnoses
  representative SDR, PQ/HDR10, HLG, HDR10+, and Dolby Vision inputs.
* Live Windows target facts, target gamut, composition, calibration ownership,
  and initial physical checks pass.
* Player-level diagnostics identify source interpretation, dynamic-HDR/base
  path, target policy, presentation mode, copies, and fallbacks.
* Supported software and Windows hardware paths have no silent format-specific
  regression or untracked transfer.

## Stage 6: macOS and Wayland presentation

* [ ] Implement Metal/EDR presentation with an accurately declared
  extended-linear surface, current headroom observation, and no second system
  media tone mapper.
* [ ] Implement and validate ADR 0014's libplacebo MoltenVK/Vulkan producer to
  IOSurface/Metal target bridge, including synchronization, lifetime, copy
  accounting, device recovery, and comparison with a shared-MoltenVK
  alternative.
* [ ] Add the VideoToolbox/IOSurface importer with the macOS graphics domain,
  and add Vulkan/DRM PRIME/VAAPI importers with the Wayland Linux graphics
  domain.
* [ ] Implement Wayland color-management-v1 surface descriptions and preferred
  description revisions; use an honest SDR fallback when unavailable.
* [ ] Treat the managed macOS and Wayland paths as `SystemManaged`; use
  `UnmanagedSrgb` only as the explicit SDR fallback inside a supported Wayland
  session when stronger compositor support is unavailable.

X11 and XWayland are unsupported and do not receive a presentation backend,
fallback path, packaging claim, or validation matrix.

## Stage 7: Reliability, performance, and physical validation

* [ ] Stress seeks, display movement, hotplug, sleep/wake, device loss, and
  long playback while checking bounded resource retention and copy counts.
* [ ] Establish stable-machine decode/render/presentation performance and
  energy baselines.
* [ ] Capture linear half-float outputs for software correctness and use
  colorimeter or spectroradiometer measurements for emitted reference white,
  highlights, gamut, and cross-platform equivalence.
* [ ] Keep hardware, compositor, and physical-display limitations visible in
  support diagnostics.

## Deliberately deferred

* Application-managed display ICC and legacy calibration workflows.
* Enabling source-ICC transforms before LCMS packaging and validated SDR RGB
  profile tests; ICC is an embedded-profile path rather than a substitute for
  the required scalar-metadata SDR/HDR formats above.
* Handmade PQ, HLG, gamut, or highlight-compression shaders.
* General renderer/backend plugin systems.
* Exact cross-GPU pixel equality and large self-generated golden suites.
