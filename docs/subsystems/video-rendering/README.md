# Video rendering subsystem

## Status

SunPlayer has an explicit rendered-video surface contract and two diagnostic
producers behind the same lifecycle. HDR Lab uses the persistent libplacebo
7.360.1 producer by default and can select the procedural QRhi producer for
diagnostic comparison. This switch is not a player fallback: real playback
will use libplacebo and report an error if no supported libplacebo target path
can be created. On Windows, libplacebo shares the QRhi D3D11 device and
immediate context, wraps the QRhi-owned RGBA16F texture, and renders directly
without an output copy. On macOS, libplacebo Vulkan over MoltenVK imports the
QRhi-owned RGBA16F Metal texture directly and uses a shared GPU timeline for
producer/compositor handoff. VideoToolbox NV12 and P010 `CVPixelBuffer` planes
map through retained CoreVideo Metal views without a CPU transfer or GPU copy.
A real FFmpeg software `AVFrame` now maps through
libplacebo with one reusable input upload and retains its source across
target-only rerenders. Supported D3D11VA frames copy their active NV12, P010,
P012, or P016 rectangle once into a cached exact-size texture on the same GPU,
then map its planes into libplacebo with no CPU transfer. A deterministic Dolby
Vision Profile 8.1 fixture
proves that FFmpeg retains raw and parsed RPU metadata and libplacebo maps the
reshape on the production software-frame path. This is not a claim of support
for every Dolby Vision profile, enhancement layer, trim, or physical target.
Linux VAAPI frames map through libplacebo's direct DRM PRIME helper, with no
known input CPU transfer or GPU copy. FFmpeg owns export and decode-completion
waiting; one retired mapping bounds retained surfaces until GPU sampling ends.
Native Intel NV12/P010 pixel comparisons and continuous decode/seek pass; AMD
and asynchronous busy-surface stress remain open. Per-frame CPU input fallbacks are
not implemented; incompatible hardware uses the shared software re-decode.
The direct RGBA16F output target remains zero-copy.

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
* The rendered-video source owns source-specific state, display geometry,
  content revision, cadence, update requests, and device-recreatable producer
  creation.
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
  target-only rerenders. The D3D11 backend copies the active rectangle from a
  decoder-owned NV12/P010/P012/P016 texture-array slice into a cached exact-size
  texture, then maps that texture through `pl_d3d11_wrap`; the retained frame
  reserves the source slice while the copy is ordered. Its typed diagnostics
  distinguish direct hardware, software upload, same-device GPU-copy,
  CPU-round-trip, and unavailable outcomes. Shared
  policy rejects a hardware frame whose recorded graphics-device generation
  differs from the active domain. An unavailable hardware mapping is reported
  as a typed failure so playback can perform one software re-decode; the
  importer itself does not own fallback policy.
* The macOS importer maps VideoToolbox NV12/P010 planes into the same
  libplacebo boundary and retains the `CVPixelBuffer`, CoreVideo texture views,
  and libplacebo textures until nonblocking GPU-completion polling releases
  them. The Linux importer follows the same lifetime requirement using
  libplacebo-owned DRM PRIME mappings and one bounded retired frame.

These are purpose-specific boundaries, not another general graphics API.

## Surface contract

[ADR 0039](../../decisions/0039-unify-color-mapping-and-native-encoding.md)
replaces the separate SDR/adaptive rendering modes with one `VideoColorTarget`.
It carries the usable gamut, reference white, known/unknown minimum, headroom
and Absolute PQ availability. Presentation resolves that value once and the
rendered-surface request reuses it directly. Fixed texture properties remain
invariants: opaque signed linear BT.709/D65 in RGBA16F, with black at zero and
ordinary white at one. Native output encoding is a separate contract.

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

Native display adapters observe platform-specific target facts in their native
units. Shared presentation policy resolves them into one effective display
target, including reference white and target headroom. The current Windows
observer is an initial WinRT/QRhi implementation; it does not yet provide
stable physical display identity, complete DisplayConfig/DXGI facts,
provenance, confidence,
or stale-query protection.

The retained decoded `AVFrame` is the authoritative source-color boundary and
the exact object consumed by libplacebo. `VideoSignalDescription` is only a
small display snapshot of scalar names and component depth. FFmpeg already
propagates ordinary scalar and static metadata; SunPlayer only attaches global
HDR10+ metadata when a decoded frame lacks it. Frame-local metadata otherwise
wins by construction.

libplacebo owns source interpretation, tone mapping, and gamut mapping. Its
linear unit is 203 nits. After mapping, the existing PRE_OUTPUT hook converts
library light L to working coordinates `q = (L - B) / (C - B)`, with all three
quantities in the same units. B is the inferred target black and C is the
ordinary-white coordinate. Validate B<C before rendering. This sends modeled
black to zero and white to one before video is blended with UI/subtitles.
Signed components survive this conversion. A nominal target peak P=C*H becomes
`(P-B)/(C-B)`, which can slightly exceed H; composition-volume hints use that
working peak rather than silently clipping it back to H.

Decoded PQ, HDR10+, and mapped Dolby use C=R, where the source-reference
preference R is 100–203 nits, default 100. Relative SDR, HLG and libplacebo HDR
Lab use C=203. Source pixels and metadata remain unchanged; R does not set the
display white or alter UI brightness. See
[ADR 0029](../../decisions/0029-configure-hdr-source-reference-white.md).

Optional Absolute PQ uses C=W, the known physical platform white, on supported
Windows HDR scRGB. The same affine normalization precedes the complete
composition's W/80 native-unit scale. This retains the physical target and
library mapping while establishing common black/white coordinates; it is no
longer a scale-only normalization: nominal presented light becomes
`W*(L-B)/(W-B)`, a small adjustment from L for positive B. Absolute PQ remains disabled by default and
unavailable on SDR, macOS and Wayland. SDR/HLG input does not use that preference.
See [ADR 0030](../../decisions/0030-optional-absolute-pq.md) as amended by ADR 0039.

The surface remains linear BT.709/sRGB coordinates even for a wide-gamut
target. Its separate optional raw target primaries describe the usable display
gamut to libplacebo. Windows Advanced Color supplies validated native RGB
primaries and white point for HDR and WCG. macOS supplies Display P3 only when
the active `NSScreen` reports that it can represent P3, otherwise BT.709 when
representable. Managed Wayland supplies explicit preferred target primaries or
falls back to its preferred primary color volume. An unavailable or invalid
target falls back conservatively to BT.709. This separation lets negative and
greater-than-one scRGB components represent colors outside BT.709 without
mislabeling the surface coordinates.

The shared display snapshot keeps the platform's color-mode label separate
from its display- versus scene-referred luminance behavior and per-field known
state. Windows can therefore retain raw WCG capability values without making
that Windows-specific interpretation a rule for future macOS or Wayland gamut
providers.

One generic operator uses BT.2446A at resolved H=1 and spline at H>1 for SDR,
PQ/Dolby, HLG and libplacebo HDR Lab. Transport encoding and potential HDR
capability do not select the operator. Supported one-window HDR10+ OOTFs on
the selected non-Dolby or HDR10-compatible base retain the ST 2094-40 override;
Lab's explicit mapping-disabled control retains Clip.

Metadata selection remains independent of that operator. Coherent HDR10+
scene values, static MaxCLL/mastering range and mapped Dolby source range reach
libplacebo unchanged. Ordinary base PQ without usable luminance metadata uses
the existing diagnosed 1,000-nit fallback. A mapped Dolby image is never combined
with HDR10+ or static values from its base representation. ADR 0039 amends the
operator choice in [ADR 0023](../../decisions/0023-use-metadata-first-hdr-to-sdr-policy.md).

The persistent “Prefer HDR10+ over Dolby Vision” setting defaults to disabled.
A proven HDR10-compatible Dolby base carrying recognized HDR10+ metadata
selects the base representation, with or without an authored curve. Disabling
the preference retains Dolby; single-format playback is unaffected. Selection
is independent of target mode and headroom and remains stable across missing
frame metadata until playback generation or preference changes. The existing
content-revision/update path rerenders paused video, and the importer remaps
when representation changes. Unknown/incompatible bases retain Dolby.
Pinned libplacebo's unsupported zero-anchor and local-window OOTFs are diagnosed
and fall back within the selected metadata family. Authored-curve availability
is independent of scene averages. Libplacebo owns source-luminance inference
and curve adaptation, including its static-range fallback when scene statistics
are unavailable. SunPlayer does not substitute MaxSCL for its inferred luminance.
Imported curve parameters are validated once, with version-specific
anchor bounds (15/9) and no custom ascending-control-value restriction. See
[ADR 0028](../../decisions/0028-prefer-hdr10plus-on-compatible-dual-format-video.md).

Perceptual gamut mapping remains selected for every path. Inverse mapping, peak detection, and
dithering remain disabled, and the exact decision and fallback provenance are
published through existing diagnostics. Null peak
detection means there is no smoothed measured-peak state affecting playback,
so open and seek do not need a no-op renderer reset. If a later
evidence-backed quality profile enables temporal peak detection or frame
mixing, open, seek, track change, and generation replacement must flush that
source-temporal state without destroying the persistent renderer. A
target-only rerender is not a source discontinuity.

Static PQ has an analytical target-response oracle. HLG explicitly
sets its render-local source peak to `203 * H`, extending libplacebo's existing
virtual-peak inference continuously to headroom one. Decoded neutral/color
captures cover that endpoint. This is a documented relative playback model,
not an independent physical-reference HLG appearance oracle.
HDR10+'s source-provided targeted-display luminance
stays unchanged. Libplacebo adapts its supported OOTF to the selected R*H
virtual target on SDR and HDR. This preserves the white-relative convention; it
does not establish physical reference-display equivalence.
The pinned Dolby Vision helper supports the tested Profile 8.1 reshape but not
all target trims or enhancement-layer residual processing.

The importer reports the decoded transfer name, whether a usable HDR10+ scene-
luminance subset is present on the mapped frame, and whether libplacebo mapped
parsed Dolby Vision metadata. The producer additionally reports the selected
operator, metadata provenance, unsupported source guidance, and explicit
fallback. It inspects the retained and mapped frames directly. These
diagnostics are best-effort and may settle on a later frame;
playback does not build a parallel dynamic-metadata state machine
merely to make every transient diagnostic snapshot atomic. Deterministic real
HEVC fixtures now prove HLG rendering, two frame-local HDR10+ scenes, and a
mapped Dolby Vision Profile 8.1 reshape through this boundary. Dynamic-HDR and
HLG physical-output accuracy remains subject to the stated format-specific
limits and later measurement.

Those formats are required V1 scope. The current experimental label describes
unverified color behavior, not a plan to omit HLG, HDR10+, or Dolby Vision from
the player or to deliberately break files that already render.

Target headroom and whether its minimum is known are separate parts of the
surface description supplied to libplacebo. Normal playback does not carry a
second physical-peak-authority flag. SunPlayer preserves a measured physical
zero as distinct from unavailable metadata.
Because libplacebo reserves numeric zero for unknown minimum luminance, every
unknown target minimum reaches that API as zero. `pl_color_space_infer` resolves
the constructed LINEAR target consistently to the library's nominal 1000:1
contrast, independently of operator, source or HDR transport. This is a fallback,
not a display measurement. Known physical zero uses `PL_COLOR_HDR_BLACK`;
positive minima retain the target-range unit conversion. Managed SDR preserves
the observed minimum instead of rewriting it to known zero. The affine
conversion removes the modeled floor before composition.

Software and hardware decoded frames share semantic metadata and scheduling
contracts, but not storage behavior. Software planes require observable
uploads. Hardware frames require backend-native import, synchronization, and
lifetime retention and should be the normal playback path when supported.

All source paths produce the same surface where `1.0` means platform reference
white, using the coordinate conversions described above. PQ source values and
mastering metadata remain source truth. The physical luminance of surface `1.0`
follows the platform reference white at presentation.

Embedded source ICC bytes are retained with the `AVFrame` and reported in
diagnostics. The render-local libplacebo frame explicitly clears both ICC
representations on every platform, so an LCMS-enabled system libplacebo cannot
silently change behavior relative to the Windows build. Source ICC rendering
is deferred until packaging, semantic profile validation, and SDR RGB behavior
are tested. ICC combined with PQ, HLG, HDR10+, or Dolby Vision remains
unsupported pending a separate model. Display calibration is a different
responsibility: on a system-managed path SunPlayer declares the final
presentation encoding and lets the OS/compositor apply the active display
profile once to the entire composition. Ordinary Windows SDR with Advanced
Color inactive and unmanaged Wayland SDR are sRGB-assumed fallbacks.
Application-managed display ICC is deferred and, if added, belongs after QRhi
composition rather than inside the video renderer.

The final QRhi compositor blends video, subtitles and UI in the same signed
linear coordinates. It applies native white-unit scaling once, uses
libplacebo's relative-colorimetric primary matrix, then encodes the selected
transfer. A small build-time generator obtains sRGB, gamma-2.2 and PQ shader
functions from `pl_shader_delinearize`; Qt bakes them into the existing QRhi
pass. Its target describes already normalized composition, so physical black
is not normalized again. Linear output retains signed values; nonlinear output
clamps only after destination-primary conversion. Input UI sRGB decoding stays
in the compositor. There is no additional tone mapper or GPU pass.

## Current display observation

Windows display adaptation is already live. The provider binds WinRT
`DisplayInformation` to the native window, listens for
`AdvancedColorInfoChanged`, and publishes the actual Standard/WCG/HDR mode,
validated native primaries and white point for managed modes, SDR white,
minimum luminance, and maximum luminance. QRhi swapchain HDR information
supplies a fallback. HDR remains scene-referred and uses the existing `W/80`
final scRGB scale. WCG observations are display-referred and do not imply HDR
headroom or authorize a new output path. Qt's current HDR eligibility gate
continues to limit Windows WCG-only output to the existing SDR path. Reported
WCG luminance remains raw capability data. The implementation also reacts to `QScreen` changes and a manual
reprobe by refreshing the cached window-bound observer and marking the output
characteristics dirty.

On macOS the active `NSScreen` publishes current/potential relative EDR
headroom and a conservative target gamut: Display P3 only when AppKit says the
screen can represent P3, otherwise BT.709 when representable. Screen-parameter,
screen, and screen-color-space changes refresh the same provider. On managed
Wayland, the ready preferred description publishes reference white, target
luminance/headroom, and explicit target primaries with a primary-volume
fallback. These are mapping targets, not application-side display calibration;
ColorSync or the Wayland compositor still owns the final profile transform.

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
are not required for correctness. Raw output identity and full-frame peak may
be added as diagnostics or policy inputs when a concrete renderer decision
needs them. Topology, Advanced Color mode, movement, and sleep/wake events only
need to converge to the newest semantic target.

## Platform backends

| Platform | Intended first path | Main unresolved risk |
| --- | --- | --- |
| Windows | Shared video-capable D3D11 device; exact-size D3D11VA GPU copy and direct RGBA16F target | P012/P016 capture, affected-file validation, and real device-loss injection |
| Wayland Linux | Shared Vulkan device and image | Layout, queue, semaphore ownership, and compositor color-management support |
| macOS | QRhi Metal/EDR presentation plus same-device MoltenVK target and VideoToolbox NV12/P010 Metal-plane import | Physical EDR and unlike-display transitions, broader formats, and device recovery |

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
8. [x] Add the QRhi-owned Vulkan implementation and exercise its software-
   decoded path on native Wayland under WSLg.
9. [x] Implement QRhi Metal/EDR presentation and the narrow same-device
   MoltenVK/Vulkan-to-Metal texture bridge on macOS. Direct target import and
   GPU-only synchronization pass on Apple M2; physical EDR above SDR white and
   unlike-display transitions remain native-hardware gates.
10. [x] Add the retained FFmpeg `AVFrame` contract, software-plane importer,
    persistent upload reuse, and real decoded-frame capture.
11. [x] Make the Windows graphics domain own a video-capable,
    multithread-protected D3D11 device and import D3D11VA planes through one
    cached exact-size GPU copy.
12. [x] Add the Vulkan/DRM/VAAPI platform importer; native Intel/AMD validation
    has passed on Intel and remains pending on AMD. The macOS VideoToolbox
    NV12/P010 importer is complete for its initial scope.

## Verification

The captures and platform checkpoints below describe the established test
boundaries. The October 22 refactor changes their target/operator expectations;
its current run evidence and remaining acceptance are recorded in the
[delivery plan](../../plans/video-rendering/2026-10-21-color-pipeline-simplification.md).
Previous Windows/macOS results are not a fresh execution of the changed code.

Target diagnostic schema has focused state tests. The real D3D11 integration
test creates the factory-selected graphics domain and exercises both diagnostic
producers through shared interfaces. The libplacebo case creates the real
D3D11 GPU and persistent renderer, uploads aligned sRGB and BT.2020/PQ analytic
inputs, captures the directly shared RGBA16F surface and final composition,
asserts pattern layout, reference-white normalization, minimum-target
contract validity, orientation, alpha, extended values, one explicit input
upload per changed frame, and zero output copies, then destroys and rewraps
the native target after resize and validates its pixels. SDR captures cover
80, 100, and 203 nits. The analytic PQ capture first maps an exact 203-nit patch
to surface `1.0`, then keeps a 1000-nit source fixed while relative headroom is
derived from a 600-nit capability. It verifies uncompressed agreement when the
source fits, highlight compression when available headroom falls, and one final
`referenceWhite / 80` composition scale. The
test also destroys the bound producer, creates the other implementation,
rebinds the compositor, and captures the result. A sustained probe submits 60
animated 640×360 frames into a 1100×600 target and reports local throughput
without a universal timing threshold. The QRhi case retains its broader
compositor, submission, and UI-layer coverage. Each future native libplacebo
backend requires equivalent real-GPU coverage. Cross-backend output
comparisons use declared tolerances.

The Windows dependency integration test links the MSVC-built test process to
the clang-cl-built libplacebo DLL, checks that the installed generated
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
203- and 100-nit reference whites. A production decoded static-PQ case holds
headroom fixed at 160- and 240-nit reference whites: producer pixels remain
stable, while the exact surfaces pass through the compositor and Windows output
changes by 1.5; the macOS expectation remains scale one.

The FFV1 case proves real compressed-video demux, timestamp and metadata
retention, exact limited-range BT.709 YUV420P samples, and tolerant
libplacebo-converted linear RGB. Its SAR 32:27 produces a 16:9 Player content
rectangle. A pinned H.264 case uses the production shared-device D3D11VA
decoder and safe NV12 plane importer; a Main10 HEVC case exercises P010. Both
assert zero input CPU transfers, one exact-size input GPU copy, and no output
copy/transfer, then compare the complete four-pixel border at 2× output size
against software decode; NV12 also exercises a nonzero crop. The Linux suite
verifies system libplacebo's required Vulkan/shader capabilities and builds the
production QRhi-owned Vulkan target; a WSLg llvmpipe smoke exercises software
decode, direct rendering, composition, swapchain presentation, and teardown.
Physical display correctness, physical Windows gamut verification,
physical HLG/dynamic-HDR target accuracy, exact macOS ICC target
chromaticities, native Wayland display-transition behavior, P012/P016 capture, general display-matrix
rotation, physical macOS EDR output above SDR white and unlike-display
transitions, broader native-GPU Linux coverage beyond the recorded Intel runs, and Vulkan
resize/surface-recreation synchronization matrix remain unproven.

Subtitle composition brightness comes from the existing appearance snapshot.
After sRGB decoding, the compositor scales subtitle RGB by brightness (default
0.8) before blending. Alpha and the video/UI layers are unaffected. This applies
to text and bitmap subtitles and precedes the shared platform-white scale and
output encoding. Like overall opacity, brightness does not advance the subtitle
raster revision or invalidate decoded video. Zero brightness yields black
subtitle pixels at their existing coverage, not transparent subtitles.

## Native encoding and Wayland SDR/HDR selection

`PresentationSurfaceContract` describes selected pixel encoding and the variable
Gamma22 output primaries. The optional controller owns native declarations;
Wayland modes, capability bits, pending objects and rejection state stay private.
Windows/macOS retain Qt's existing output selection, with no new WCG SDR support.

On Wayland, current SDR/WCG display observations select matching gamma-2.2 SDR
primaries where supported; HDR observations select PQ where supported. Missing
feedback or enhanced capabilities selects canonical managed sRGB, or unmanaged
piecewise sRGB when the managed base contract is unavailable. Every SDR output
constrains H to one and the mapper's usable gamut to the selected output.

Ready descriptions are assigned immediately before their matching buffer is
presented. Pending readiness defers presentation; later frames reconcile the
latest semantic target. Primaries-only changes retain the swapchain; SDR/PQ
changes reuse queued swapchain recreation while retaining the native window,
surface and device. PQ and display-primary SDR rejection are independently
bounded to the graphics-device generation. No display epoch or transition
framework is added. This replaces stable PQ on SDR from
[ADR 0021](../../decisions/0021-use-hdr10-pq-for-managed-wayland-hdr.md) and the
unconditional PQ preference in [ADR 0032](../../decisions/0032-support-display-primary-wayland-sdr.md).

Managed Wayland HDR does not depend on optional mastering metadata. Its PQ
primary range is explicitly zero-to-10000 nits with reference white 203;
mastering-capable compositors additionally receive the tighter composed volume.
Otherwise the full primary volume is declared. This changes transport
eligibility, not decoded-video mapping or output scaling (ADR 0033).

The refactor's validation evidence is tracked in the
[delivery plan](../../plans/video-rendering/2026-10-21-color-pipeline-simplification.md).
Linux Debug passes 41/41 CTests, including the final-encoding GPU oracles; the
Release player build and QML lint pass. A separate required hidden-window host
test uses synthetic display/generation inputs and injected rejection to cover
v2 description readiness, assignment and pending replacement. It submits no
swapchain buffers and performs no device recreation. Engine declaration/buffer pairing, physical
SDR/PQ movement and output remain acceptance work. Windows/macOS execution of
this change has not been performed here; earlier captures do not establish it.
