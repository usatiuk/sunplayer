# Video rendering subsystem

## Status

Sunroom has an explicit rendered-video surface contract and two diagnostic
producers behind the same lifecycle. HDR Lab uses the persistent libplacebo
7.360.1 producer by default and can select the procedural QRhi producer for
diagnostic comparison. This switch is not a player fallback: real playback
will use libplacebo and report an error if no supported libplacebo target path
can be created. On Windows, libplacebo shares the QRhi D3D11 device and
immediate context, wraps the QRhi-owned RGBA16F texture, and renders directly
without an output copy. A real FFmpeg software `AVFrame` now maps through
libplacebo with one reusable input upload and retains its source across
target-only rerenders. Supported D3D11VA frames map their retained NV12, P010,
P012, or P016 texture-array slice directly into libplacebo plane views with no
input copy or CPU transfer. A deterministic Dolby Vision Profile 8.1 fixture
proves that FFmpeg retains raw and parsed RPU metadata and libplacebo maps the
reshape on the production software-frame path. This is not a claim of support
for every Dolby Vision profile, enhancement layer, trim, or physical target.
Same-device-copy and CPU target fallbacks, and non-Windows native importers,
are not implemented.

The broad investigation in
[../../ARCHITECTURE_NOTES.md](../../ARCHITECTURE_NOTES.md) is non-binding
research. The accepted device and target boundary is recorded in
[ADR 0004](../../decisions/0004-cross-platform-graphics-domain-and-video-interop.md).
The preferred macOS presentation realization is recorded in
[ADR 0014](../../decisions/0014-prefer-native-metal-presentation-on-macos.md).
The active color and rendering roadmap is [PLAN.md](PLAN.md).

## Accepted structure

```text
diagnostic source ──→ QRhi producer ─────────────────────┐
                  └─→ analytic libplacebo producer ──────┤
                                                        │
decoded frame ──→ frame importer ──→ libplacebo producer│
                                      + target interop ──┤
                                                        ↓
RenderedVideoSurface / QRhiTexture
        ↓
shared QRhi compositor
```

The known seams are established with their first implementations:

* The graphics-device domain owns QRhi, native backend state, the same-device
  libplacebo GPU and FFmpeg hardware device, device generation, execution
  synchronization, backend/adapter diagnostics, and teardown order.
* The rendered-video source owns source-specific state, content revision,
  cadence, update requests, and device-recreatable producer creation.
* The rendered-video producer owns invalidation, submission reporting, and
  rendered-state commit/discard semantics without exposing source-specific
  controls to the presentation engine.
* The target lifecycle covers provisioning, result-bearing producer access,
  preparation for composition, submission acceptance/abort, the composition
  texture and its revision, and path diagnostics. The graphics domain selects
  the implementation and the producer owns the returned target.
* The analytic producer models the software-frame branch with one
  persistent 640×360 RGBA32F texture and buffer. It performs one explicit
  CPU-to-GPU upload when the input frame changes and reuses that texture for
  target-only rerenders; the work does not scale with the viewport.
* The decoded-frame importer uses libplacebo's FFmpeg helper for software
  planes, retains the referenced `AVFrame`, and reuses its plane textures for
  target-only rerenders. The D3D11 backend maps decoder-owned
  NV12/P010/P012/P016 texture-array slices through `pl_d3d11_wrap`; the retained frame reserves
  that slice. Its typed diagnostics distinguish direct hardware, software
  upload, future GPU-copy/CPU-round-trip, and unavailable outcomes. Shared
  policy rejects a hardware frame whose recorded graphics-device generation
  differs from the active domain. An unavailable hardware mapping is reported
  as a typed failure so playback can perform one software re-decode; the
  importer itself does not own fallback policy.
* Future platform importers map Vulkan/DRM/VAAPI or VideoToolbox surfaces into
  the same libplacebo boundary while retaining their native lifetime and
  synchronization state.

These are purpose-specific boundaries, not another general graphics API.

## Surface contract

The compositor consumes the display-targeted RGBA16F surface defined by
[ADR 0003](../../decisions/0003-display-targeted-video-surface.md). It does not
know whether the underlying texture is QRhi-owned, libplacebo-owned, directly
shared, or populated by a fallback copy.

A surface is reusable only when its device, display target, content,
description, and successfully submitted render state remain compatible.

## Output paths and fallback

Backends attempt, in order:

1. Direct shared render target.
2. Same-device GPU copy.
3. CPU round trip as an explicit degraded or test path.

The selected path, copy counts, synchronization mode, and fallback reason are
diagnostic state. These are interop paths for the same libplacebo renderer, not
alternate video renderers. A fallback must not silently redefine the surface's
color or luminance meaning. The procedural QRhi producer is excluded from this
fallback chain.

## Color and luminance responsibilities

Native display adapters observe platform-specific facts in physical units.
Shared presentation policy resolves them into one effective display target,
including reference white and target peak. The current Windows observer is an
initial WinRT/QRhi implementation; it does not yet provide stable physical
display identity, complete DisplayConfig/DXGI facts, provenance, confidence,
or stale-query protection.

The retained decoded `AVFrame` is the authoritative source-color boundary and
the exact object consumed by libplacebo. `VideoSignalDescription` is only a
small display snapshot of scalar names and component depth. FFmpeg already
propagates ordinary scalar and static metadata; Sunroom only attaches global
HDR10+ metadata when a decoded frame lacks it. Frame-local metadata otherwise
wins by construction.

libplacebo owns source interpretation, tone mapping, and gamut mapping.
libplacebo's linear convention uses `1.0 = 203 nits`; Sunroom's rendered-video
surface uses `1.0 = active reference white`. For relative SDR and static PQ,
the producer currently expresses headroom as
`max_luma = 203 * targetPeakHeadroom`. Analytic capture proves that numerical
bridge for those inputs. Source pixels and HDR-transfer metadata remain
unchanged, and no custom pre-output multiplier runs after tone mapping.

The renderer explicitly selects libplacebo 7.360.1's spline tone mapper and
perceptual gamut mapper, disables inverse tone mapping, and passes null peak-
detection and dithering parameters. The same stable policy description is
published in diagnostics. Null peak detection means there is no smoothed
measured-peak state affecting playback today, so open and seek do not need a
no-op renderer reset. If a later evidence-backed quality profile enables
temporal peak detection or frame mixing, open, seek, track change, and
generation replacement must flush that source-temporal state without
destroying the persistent renderer. A target-only rerender is not a source
discontinuity.

Static PQ has an analytical target-response oracle. A real HLG fixture also
confirms that libplacebo 7.360.1 changes the captured OOTF response when
Sunroom's virtual destination changes, because the library uses that HDR
destination maximum while inferring HLG. V1 accepts this behavior for
display-relative playback, but does not claim absolute-reference HLG
monitoring. If physical evidence later rejects it, the next step is a focused
upstream API separating physical HLG peak from destination coordinates, not a
second Sunroom HLG stage. HDR10+'s source-authored targeted-display luminance
stays separate from the current display destination. The pinned Dolby Vision
helper supports the tested Profile 8.1 reshape but not all target trims or
enhancement-layer residual processing.

The importer reports the decoded transfer name, whether a usable HDR10+ scene-
luminance subset is present on the mapped frame, and whether libplacebo mapped
parsed Dolby Vision metadata. It inspects the retained and mapped frames
directly. These diagnostics are best-effort and may settle on a later frame;
playback does not build a parallel dynamic-metadata state machine
merely to make every transient diagnostic snapshot atomic. Deterministic real
HEVC fixtures now prove HLG rendering, two frame-local HDR10+ scenes, and a
mapped Dolby Vision Profile 8.1 reshape through this boundary. Dynamic-HDR and
HLG physical-output accuracy remains subject to the stated format-specific
limits and later measurement.

Those formats are required V1 scope. The current experimental label describes
unverified color behavior, not a plan to omit HLG, HDR10+, or Dolby Vision from
the player or to deliberately break files that already render.

Minimum target luminance and whether it is known are also part of the surface
description supplied to libplacebo. Sunroom preserves a measured physical zero
as distinct from unavailable metadata. Because libplacebo reserves numeric
zero for unknown minimum luminance and otherwise infers a linear-target
contrast ratio, the adapter uses `PL_COLOR_HDR_BLACK` at that API boundary for
an unknown or known-zero minimum; shared physical state remains unchanged.

Software and hardware decoded frames share semantic metadata and scheduling
contracts, but not storage behavior. Software planes require observable
uploads. Hardware frames require backend-native import, synchronization, and
lifetime retention and should be the normal playback path when supported.

Relative SDR white and the 203-nit HDR reference-white anchor both map to
surface `1.0`; PQ source values and mastering metadata remain source truth.
The physical luminance of surface `1.0` follows the platform reference white
at presentation.

Embedded source ICC bytes are retained with the `AVFrame`, but the pinned
libplacebo build has LCMS disabled and does not apply them. Source ICC rendering
is deferred until packaging, semantic profile validation, and SDR RGB behavior
are tested. ICC combined with PQ, HLG, HDR10+, or Dolby Vision remains
unsupported pending a separate model. Display calibration is a different
responsibility: on a system-managed path Sunroom declares the final
presentation encoding and lets the OS/compositor apply the active display
profile once to the entire composition. Ordinary Windows SDR with Advanced
Color inactive is currently an unmanaged sRGB-assumed fallback.
Application-managed display ICC is deferred and, if added, belongs after QRhi
composition rather than inside the video renderer.

The final QRhi compositor only places the resulting linear BT.709 surface,
blends other described layers in the same reference-white-relative convention,
and converts the final composition to the selected presentation convention.
The platform presentation backend owns swapchain choice and OS output encoding.
Neither one reinterprets source video metadata or tone-maps the video again.

## Current Windows display observation

Windows display adaptation is already live. The provider binds WinRT
`DisplayInformation` to the native window, listens for
`AdvancedColorInfoChanged`, and publishes HDR mode, SDR white, minimum
luminance, and maximum luminance. QRhi swapchain HDR information supplies a
fallback. The current implementation also reacts to `QScreen` changes and a
manual reprobe by refreshing the cached window-bound observer and marking the
output characteristics dirty.

[ADR 0016](../../decisions/0016-reconcile-output-changes-semantically.md)
is implemented. The HWND-bound `DisplayInformation` remains the Windows
Advanced Color authority, native events are latest-state hints, and rendered
surface reuse compares one semantic target. A changed target rerenders even a
paused frame; an equal target reuses the existing surface regardless of screen
identity or event count. At the render boundary, the engine checks whether the
current output still supports the active swapchain format and recreates only
when the desired format differs.

Stable DisplayConfig/DXGI identity, greatest-intersection selection, topology
generations, per-field confidence, and a general asynchronous query pipeline
are not required for correctness. Raw output identity, target primaries, and
peak/full-frame capability values may be added as diagnostics or policy inputs
when a concrete renderer decision needs them. Topology, HDR, movement, and
sleep/wake events only need to converge to the newest semantic target.

## Platform backends

| Platform | Intended first path | Main unresolved risk |
| --- | --- | --- |
| Windows | Shared video-capable D3D11 device; D3D11VA plane import and direct RGBA16F target | P010/P012/P016 capture, real device-loss injection, and GPU/CPU copy fallbacks |
| Wayland Linux | Shared Vulkan device and image | Layout, queue, semaphore ownership, and compositor color-management support |
| macOS | QRhi Metal/EDR domain with a narrow native import path | EDR behavior, VideoToolbox/IOSurface formats, and synchronization |

Native graphics and decoder types remain in backend implementations. Playback,
best-effort diagnostics, renderer policy, subtitles, and the compositor remain
shared.

## Implementation sequence

1. [x] Extract the graphics-device domain, rendered-video producer, and
   source lifecycle; route the diagnostic path through them.
2. [x] Expose output path, synchronization, copy/transfer, and fallback
   diagnostics through the current UI for the direct QRhi target.
3. [x] Add a pinned D3D11-only libplacebo dependency and verify its installed
   feature configuration and basic runtime lifecycle.
4. [x] Add the renderer-facing target access contract and Windows D3D11
   direct implementation.
5. [x] Render known SDR and BT.2020/PQ software-backed images and capture both
   the video surface and final composition.
6. [x] Express arbitrary active reference white and headroom through
   libplacebo's target, capture SDR at 80, 100, and 203 nits, and hold one
   fixed 1000-nit PQ signal across 80-, 100-, and 203-nit reference whites on a
   constant 600-nit target.
7. [x] Make libplacebo the HDR Lab default while retaining procedural QRhi as
   an explicit diagnostic comparison only.
8. [ ] Add the Vulkan implementation and exercise it on native Wayland Linux.
9. [ ] Validate QRhi Metal/EDR presentation and the narrow
   MoltenVK/Vulkan-to-IOSurface/Metal video bridge on macOS; retain shared
   MoltenVK presentation only as an evidence-driven alternative.
10. [x] Add the retained FFmpeg `AVFrame` contract, software-plane importer,
    persistent upload reuse, and real decoded-frame capture.
11. [x] Make the Windows graphics domain own a video-capable,
    multithread-protected D3D11 device and add direct D3D11VA plane import.
12. [ ] Add Vulkan/DRM/VAAPI and VideoToolbox platform importers as their
    backends are implemented.

## Verification

Target diagnostic schema has focused state tests. The real D3D11 integration
test creates the factory-selected graphics domain and exercises both diagnostic
producers through shared interfaces. The libplacebo case creates the real
D3D11 GPU and persistent renderer, uploads aligned sRGB and BT.2020/PQ analytic
inputs, captures the directly shared RGBA16F surface and final composition,
asserts pattern layout, reference-white normalization, minimum-target
contract validity, orientation, alpha, extended values, one explicit input
upload per changed frame, and zero output copies, then destroys and rewraps
the native target after resize and validates its pixels. SDR captures cover
80, 100, and 203 nits. The PQ capture first maps an exact 203-nit patch to
surface `1.0`, then keeps a 1000-nit source fixed against a 600-nit physical
target. It verifies uncompressed agreement when the source fits at 80- and
100-nit white, highlight compression when 203-nit white reduces available
headroom, and one final `referenceWhite / 80` composition scale. The
test also destroys the bound producer, creates the other implementation,
rebinds the compositor, and captures the result. A sustained probe submits 60
animated 640×360 frames into a 1100×600 target and reports local throughput
without a universal timing threshold. The QRhi case retains its broader
compositor, submission, and UI-layer coverage. Each future native libplacebo
backend requires equivalent real-GPU coverage. Cross-backend output
comparisons use declared tolerances.

The separate dependency integration test links the MSVC-built test process to the
clang-cl-built libplacebo DLL, checks that the installed generated
configuration enables D3D11, Shaderc, and built-in DOVI handling while
disabling Vulkan, OpenGL, and external libdovi, checks the pinned version, and
exercises a real log create/destroy lifecycle.

The FFmpeg video test opens pinned, hashed RGB, Matroska/FFV1, Matroska/H.264,
and raw HEVC fixtures through real `avformat`/`avcodec`. Four-frame BT.2020
Main10 fixtures cover static HDR10/PQ, HLG, two-scene HDR10+, and Dolby Vision
Profile 8.1. Each is decoded once, retained, imported, rendered into RGBA16F,
and captured; exact hashes and source signal facts are checked before format-
specific analytical or behavioral assertions. The focused RGB adapter
destroys decoder contexts after returning the retained frame, while the three-
frame FFV1 and H.264 cases also exercise continuous drain through the
production bounded packet/decode path. The RGB case maps through the production
software importer and captures both the display-targeted surface and final
composition. It asserts known pixels, one input upload, zero input download/GPU
copy, zero output copies, and source-upload reuse while rerendering the same SDR frame for
203- and 100-nit reference whites.

The FFV1 case proves real compressed-video demux, timestamp and metadata
retention, exact limited-range BT.709 YUV420P samples, and tolerant
libplacebo-converted linear RGB. Its SAR 32:27 produces a 16:9 Player content
rectangle. A pinned H.264 case uses the production shared-device D3D11VA
decoder and direct NV12 plane importer, asserts no input download/upload or GPU
copy and no output copy/transfer, captures its display-targeted output, and
compares representative pixels against the software decode. Physical display
correctness, physical HLG/dynamic-HDR target accuracy, actual display-gamut
propagation, P010/P012/P016 capture, general display-matrix rotation, macOS EDR
viability, and Vulkan synchronization remain unproven.
