# Video rendering and color-management plan

This plan takes the current real-video renderer to trustworthy SDR and HDR
playback without introducing a second tone mapper, source parser, or parallel
media operation. It complements the implementation summary in
[README.md](README.md).

SDR, PQ, HLG, HDR10+, and Dolby Vision are all V1 inputs. They use one
FFmpeg/libplacebo path whenever the libraries can decode and map them; SunPlayer
does not gate playback on a format-specific implementation. Color-correctness
claims remain scoped to representative fixtures and target models that have
actually been validated.

## Product invariants

* Normal playback expresses every format in the active-reference-white surface;
  an absolute/reference-monitoring intent would be a separate explicit mode.
* SunPlayer's linear working surface uses `1.0 = active reference white` and
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
* [x] Import D3D11VA NV12/P010 on the shared graphics device through one safe,
  exact-size GPU copy.
* [x] Render into an RGBA16F display-targeted surface and compose once through
  QRhi.
* [x] Exercise analytic SDR and static-PQ reference-white changes.
* [x] Play real local A/V through the single-pass media operation.

The analytic PQ test proves the numerical static-PQ bridge, not general HDR
format correctness or emitted luminance.

## Stage 1: FFmpeg/libplacebo format acceptance and target integration (complete)

This completed milestone keeps FFmpeg and libplacebo responsible for decoding,
source transfer interpretation, dynamic HDR
metadata, tone mapping, and gamut mapping. SunPlayer does not implement separate
SDR, PQ, HLG, HDR10+, or Dolby Vision pipelines. Its work is to preserve the
library inputs, describe the display target without contradictory units, and
prove that the production path actually uses the expected library feature or
documented fallback. Source-temporal reset behavior is required only if a
temporal renderer feature is enabled.

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
* [x] Expose source colorimetry, dynamic-metadata presence, libplacebo mapping
  path, and explicit fallback in diagnostics. In particular, report whether
  parsed Dolby Vision metadata produced a libplacebo reshape or the decoded
  base layer was shown; do not infer base-layer compatibility from appearance.
* [x] Preserve source ICC bytes through the retained frame and report presence
  and size; clear both ICC representations on the render-local libplacebo frame
  on every platform. Detailed profile validation and source-ICC rendering
  remain deferred independently of the linked libplacebo's LCMS feature.

### One display-target integration

The renderer selects a shared, metadata-first libplacebo policy. SDR and HLG
retain their established relative behavior. PQ uses supported HDR10+ OOTFs,
coherent dynamic/static metadata, or a diagnosed missing-metadata fallback
through existing libplacebo operators on SDR and HDR. Perceptual gamut mapping
remains common, while inverse tone mapping, peak detection, and dithering remain
disabled. The resulting decision is visible in HDR Lab and protected at the
production capture boundary without adding another mapper or quality-preset
surface.

* [x] Use pinned-representable HDR10+ OOTFs through ST 2094-40 on nominal SDR.
  On reference-white-adaptive HDR retain validated scene/static guidance with
  spline rather than applying the authored OOTF against an invented physical
  target.
  Use libplacebo's generalized BT.2446A EETF for other coherent PQ-to-SDR
  dynamic/static ranges, and retain spline for ordinary HDR. Use an explicit,
  diagnosed 1,000-nit maximum with spline only when ordinary base PQ has no
  usable maximum.
* [x] Keep perceptual gamut mapping, inverse tone mapping off, peak detection
  off, and dithering off for the RGBA16F target. Report the exact operator,
  metadata provenance, unsupported guidance, and fallback in diagnostics.
* [x] Keep Dolby and HDR10-compatible base metadata coherent. Retain the
  narrow typed Profile 8.1 compatibility fact, latch a supported HDR10+ base
  choice per playback generation for SDR/WCG, and include it in importer-cache
  reuse so a paused HDR/SDR target transition remaps the frame.
* [x] Do not add a no-op source-discontinuity hook while peak detection and
  other source-temporal renderer features are disabled. If one is enabled,
  open, seek, track change, and generation replacement must reset its state;
  display-target and window changes must not.
* [x] Keep dynamic peak detection disabled for the first acceptance pass.
  Consider enabling it only if representative missing or unreliable source
  metadata demonstrates a visible benefit and its cost and seek/scene behavior
  pass the same production-boundary tests.
* [x] Keep every normal HDR target plus relative SDR/HLG in the
  display-relative `203 * targetPeakHeadroom` coordinate. Give PQ/Dolby nominal
  100-nit SDR only at headroom one and apply the fixed `203 / 100` coordinate
  conversion. Do not install a live-reference-white producer scale at HDR
  headroom.
* [x] Preserve unknown SDR target-black semantics at the libplacebo boundary.
  Pass numeric zero only for unknown no-headroom SDR so the pinned library
  infers 1000:1 contrast; retain the effectively-zero sentinel for known zero
  and conservatively for unknown extended-linear HDR/EDR.
* [x] Render a controlled HLG fixture through the current adapter at two
  reference-white/headroom targets. The captured OOTF response changes with
  the virtual target as pinned libplacebo source predicts. V1 accepts that
  display-relative behavior without claiming absolute-reference monitoring;
  physical evidence may later motivate a focused upstream API separating HLG
  physical peak from destination coordinates.
* [x] Keep HDR10+ source metadata unchanged, including its source-authored
  targeted-system-display luminance. Use its representable OOTF on nominal SDR
  and scene guidance on reference-white-adaptive HDR.
* [x] Report whether libplacebo applied Dolby Vision reshaping or displayed the
  decoder's proven HDR10-compatible base-layer result. Parse only the typed
  decoder configuration needed for that narrow choice; do not implement
  missing trims/residual processing in SunPlayer.

### Production-boundary acceptance matrix

* [x] Retain the existing BT.709 SDR corpus and add deterministic,
  redistributable PQ/HDR10, HLG, two-scene HDR10+, and Dolby Vision Profile 8.1
  HEVC fixtures.
  BT.2020 SDR and the broader range/bit-depth matrix remain Stage 4 expansion,
  not a separate V1 renderer path.
* [x] Decode every new fixture once through the production FFmpeg operation,
  retain its four real frames, and render those frames through the production
  importer, libplacebo renderer, and RGBA16F target. The content-independent
  QRhi compositor and final Windows conversion remain independently covered
  rather than repeated for every source format.
* [x] Assert hashes, decoded signal facts, static mastering/content-light
  metadata, the explicit color policy, finite/monotonic/bounded captures,
  static-PQ analytical values, independent BT.2446A and ST 2094-40 vectors,
  metadata-less PQ render-boundary output on SDR and HDR, two HLG target
  responses, numeric and production-path PQ near-black separation under an
  unknown SDR minimum, frame-local HDR10+ scene progression and OOTF dispatch on nominal
  SDR, fixed-headroom producer invariance across reference-white coordinates,
  exact production-surface composition under the distinct Windows/macOS scale
  laws, coherent Dolby/base selection, mapped Dolby
  Vision reshaping, and same-frame remapping
  across SDR/HDR targets. Existing tests retain target-only rerender, final
  Windows scaling, and real inter-frame seek coverage; temporal-state reset is
  intentionally absent while temporal renderer features are off.

Immediate milestone outcome:

* All representative formats enter the same retained-frame/import/render path.
* SDR retains its validated numerical model; PQ uses the accepted
  metadata-first source policy, nominal-100 SDR, reference-white-adaptive HDR,
  and diagnosed missing-metadata fallback.
* HLG's observed target response is accepted for display-relative V1 playback;
  absolute-reference monitoring remains outside the claim rather than being
  approximated by a second SunPlayer color pipeline.
* Supported HDR10+ OOTFs and scene metadata reach the selected libplacebo
  operator without stale carry-over.
* Dolby Vision diagnostics distinguish mapped reshape from a proven coherent
  base representation and never mix their metadata families.
* Unverified target behavior remains visible without blocking otherwise valid
  playback.

Existing files that already decode and render must remain playable while this
milestone replaces experimental target behavior with verified support. A new
failure is acceptable only when FFmpeg/libplacebo genuinely lacks a valid path
for that source and the fallback or error is explicit; it is not a reason to
build a competing SunPlayer implementation.

## Stage 2: Simplify live Windows target reconciliation

This is not a greenfield display observer. The current implementation already:

* [x] Caches an HWND-bound WinRT `DisplayInformation` and observes
  `AdvancedColorInfoChanged`.
* [x] Publishes active Standard/WCG/HDR mode, managed-mode native primaries,
  SDR white, minimum luminance, and maximum luminance, with QRhi swapchain HDR
  information as a fallback.
* [x] Reacts to `QWindow::screenChanged` and refreshes the cached window-bound
  display observer.
* [x] Rerenders a paused target-dependent frame when the detected reference
  white, headroom, or target gamut changes.

The accepted design treats native events as hints and the latest semantic
`PresentationTarget` as truth. Windows' HWND-bound `DisplayInformation` already
tracks the window and is the Advanced Color authority. SunPlayer will not add a
cross-platform hierarchy of selection, capability, provider, topology, or
query revisions, nor a competing greatest-intersection display selector.

* [x] Remove `displayTargetRevision` from the rendered-video reuse key. Record
  every video-affecting target value directly in the surface request and reuse
  it when those values are equal.
* [x] Stop recreating the swapchain merely because `QScreen` identity changed.
  Recreate or reconfigure only when the presentation format, declared color
  encoding, HDR/SDR mode, adapter/device, or native resource lifetime changes.
* [x] Route `screenChanged`, Advanced Color changes, and manual reprobe through
  one idempotent render-boundary format check. The cached HWND observer tracks
  movement; no competing movement timer or asynchronous query pipeline exists.
* [ ] Validate hotplug and resume convergence on real Windows systems and add
  another native hint only if those events do not reach the existing observer.
* [x] Add semantic surface coverage proving equal targets reuse a video surface
  while reference-white/headroom changes invalidate it. Actual window movement
  and swapchain-format changes remain real-platform validation.

Native display identity, reported full-frame peak, raw capability values,
update reason, and timestamps remain optional diagnostics. Rendering consumes
one effective target peak and does not use confidence scores as a correctness
mechanism.

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

* [x] Supply usable platform target primaries separately from the extended
  BT.709/scRGB coordinate basis used by the RGBA16F surface: validated Windows
  Advanced Color native xy; an AppKit-confirmed P3 or BT.709 conservative lower
  bound for macOS EDR; and explicit/fallback preferred target primaries on
  managed Wayland. Exact macOS ICC xy and physical output validation remain
  separate work.
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
* [x] Remove HDR Lab target overrides from production Player state so
  diagnostic experimentation cannot silently change ordinary playback.
* [ ] Add Player-level open, pause, seek, display-change, device-recovery, and
  shutdown scenarios around the color pipeline.

## Stage 5: Hardware-frame coverage

Immediate Windows edge-corruption repair, grounded in
[the D3D11VA diagnosis](../../research/2026-08-23-windows-d3d11va-edge-corruption.md):

* [x] Replace unconditional sampling of padded D3D11 decoder surfaces with one
  cached, exact-size same-device GPU copy. Keep hardware decode and zero CPU
  transfers; report the copy accurately.
* [x] Capture the complete 2× NV12 and P010 output boundary against software
  decode, including the final row and right edge, and exercise a nonzero
  crop-relative NV12 copy plus odd-extent software retry. Do not use
  interior-only samples as the safety oracle.
* [x] Validate representative affected P010 files on Windows. Keep zero-copy as
  possible future advanced/experimental behavior, not the playback default.

Broader hardware-frame work:

* [ ] Capture P012 and P016 D3D11VA import through the safe copy path; NV12 and
  P010 are covered.
* [ ] Compare at least one metadata-bearing P010 HDR source through software
  decode and D3D11VA import, asserting equivalent effective color evidence,
  libplacebo mapping selection, and captured output within backend tolerance.
  Capability-gate any hardware format that has not passed this differential.
* [ ] Implement and diagnose an explicit CPU fallback only where a demonstrated
  unsupported hardware boundary requires it.
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

* [x] Implement Metal/EDR presentation with an accurately declared
  extended-linear surface, current relative headroom observation, and no
  second system media tone mapper. Physical current headroom above `1.0` and
  unlike-display transitions remain native-hardware validation.
* [x] Implement ADR 0014's same-device libplacebo MoltenVK/Vulkan producer to
  Metal target bridge with direct texture import, exported shared-event
  synchronization, explicit lifetime/copy diagnostics, and no CPU wait or
  output copy. Device-loss injection remains broader graphics validation.
* [x] Add the VideoToolbox NV12/P010 Metal-plane importer with the macOS
  graphics domain, retained through libplacebo GPU completion.
* [ ] Add Vulkan/DRM PRIME/VAAPI importers with the Wayland Linux graphics
  domain.
* [x] Inventory Linux color-management-v1 capabilities. Own one latest-version
  surface declaration with ready managed gamma-2.2 and BT.2020/PQ descriptions,
  observe version-2 preferred targets independently, and otherwise select
  unmanaged assumed-sRGB SDR without Wayland color ownership. Continue to
  reject X11 and XWayland.
* [x] Make final-compositor output encoding explicit at the surface boundary:
  piecewise sRGB for Windows and Wayland unmanaged fallbacks, gamma 2.2 for
  managed Wayland SDR, extended linear for Windows/macOS HDR, and BT.2020/PQ
  for managed Wayland HDR. Keep one compositor. The
  shared shader branches and selection tests exist; backend-neutral pixel
  readback for the Linux gamma-2.2 branch remains pending.
* [x] Treat managed gamma-2.2 Wayland SDR as `SystemManaged` and its startup
  fallback as `UnmanagedSrgb` with one-times SDR headroom and HDR unavailable.
  SunPlayer owns the managed declaration while Qt remains the toplevel owner and
  Vulkan pass-through prevents a competing WSI declaration.
* [x] Observe preferred-output target state without using it as a content-
  encoding command. Keep a capable window BT.2020/PQ across HDR/SDR outputs;
  after genuine HDR presentation failure, atomically return to the complete
  managed gamma-2.2 SDR tuple for that graphics generation.

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
* Enabling source-ICC transforms before the dependency policy, semantic
  precedence, and validated SDR RGB profile tests are shared across platforms;
  ICC is an embedded-profile path rather than a substitute for the required
  scalar-metadata SDR/HDR formats above.
* Handmade PQ, HLG, gamut, or highlight-compression shaders.
* General renderer/backend plugin systems.
* Exact cross-GPU pixel equality and large self-generated golden suites.
