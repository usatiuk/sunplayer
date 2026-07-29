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
input copy or CPU transfer. Dolby Vision side data is explicitly mapped into
mapping-owned libplacebo metadata. Same-device-copy and CPU target fallbacks, and
non-Windows native importers, are not implemented.

The broad investigation in
[../../ARCHITECTURE_NOTES.md](../../ARCHITECTURE_NOTES.md) is non-binding
research. The accepted device and target boundary is recorded in
[ADR 0004](../../decisions/0004-cross-platform-graphics-domain-and-video-interop.md).

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
including reference white and target peak. Source metadata normalization and
the libplacebo producer then own source transfer, primaries, range, tone
mapping, and the conversion into Sunroom's platform-neutral surface contract.

libplacebo's linear convention uses `1.0 = 203 nits`. Sunroom's rendered-video
surface uses `1.0 = active reference white`. The producer applies
`203 / referenceWhiteNits` at libplacebo's pre-output stage so this internal
library convention does not leak into composition. SDR input remains relative;
the target-relative diagnostic pattern encodes PQ samples from its chosen
absolute luminance. A real decoded PQ signal remains source-absolute and does
not change when the output target changes.

Minimum target luminance and whether it is known are also part of the surface
description supplied to libplacebo. Sunroom preserves a measured physical zero
as distinct from unavailable metadata. Because libplacebo reserves numeric
zero for unknown minimum luminance, the adapter passes `PL_COLOR_HDR_BLACK`
only at that API boundary for a known zero; shared physical state remains zero.

Software and hardware decoded frames share semantic metadata and scheduling
contracts, but not storage behavior. Software planes require observable
uploads. Hardware frames require backend-native import, synchronization, and
lifetime retention and should be the normal playback path when supported.

Before rendering, shared policy resolves unspecified source color fields
through libplacebo. A source that remains relative SDR is anchored to the
active target SDR white; PQ, HLG, or another effectively HDR source retains its
absolute signal and mastering metadata. This prevents the same SDR frame from
changing composition-relative brightness when the platform SDR-white value
changes.

The final QRhi compositor only places the resulting linear BT.709 surface,
blends other described layers in the same reference-white-relative convention,
and converts the final composition to the selected presentation convention.
The platform presentation backend owns swapchain choice and OS output encoding.
Neither one reinterprets source video metadata or tone-maps the video again.

## Platform backends

| Platform | Intended first path | Main unresolved risk |
| --- | --- | --- |
| Windows | Shared video-capable D3D11 device; D3D11VA plane import and direct RGBA16F target | P010/P012/P016 capture, real device-loss injection, and GPU/CPU copy fallbacks |
| Linux | Shared Vulkan device and image | Layout, queue, and semaphore ownership |
| macOS | Shared Vulkan/MoltenVK domain, with Metal interop if required | EDR behavior, IOSurface formats, and cross-API synchronization |

Native graphics and decoder types remain in backend implementations. Playback,
metadata policy, renderer policy, subtitles, and the compositor remain shared.

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
6. [x] Normalize libplacebo output to arbitrary active reference white and
   capture SDR at 80, 100, and 203 nits plus a target-relative PQ diagnostic at
   100 and 203 nits.
7. [x] Make libplacebo the HDR Lab default while retaining procedural QRhi as
   an explicit diagnostic comparison only.
8. [ ] Add the Vulkan implementation and exercise it on Linux.
9. [ ] Validate MoltenVK presentation on macOS before choosing shared Vulkan or a
   Metal interop backend.
10. [x] Add the retained FFmpeg `AVFrame` contract, software-plane importer,
    persistent upload reuse, and real first-frame capture.
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
80, 100, and 203 nits; the
target-relative PQ diagnostic covers 100 and 203 nits. The test also destroys
the bound producer, creates the other implementation, rebinds the compositor,
and captures the result. A sustained probe submits 60 animated 640×360 frames
into a 1100×600 target and reports local throughput without a universal timing
threshold. The QRhi case retains its broader compositor, submission, and
UI-layer coverage. Each future native libplacebo backend requires equivalent
real-GPU coverage. Cross-backend output comparisons use declared tolerances.

The separate dependency integration test links the MSVC-built test process to the
clang-cl-built libplacebo DLL, checks that the installed generated
configuration enables D3D11, Shaderc, and built-in DOVI handling while
disabling Vulkan, OpenGL, and external libdovi, checks the pinned version, and
exercises a real log create/destroy lifecycle.

The FFmpeg first-frame test opens pinned, hashed RGB and Matroska/FFV1 fixtures
through real `avformat`/`avcodec` and destroys decoder contexts after returning
the retained frame. The RGB case maps through the production software importer
and captures both the display-targeted surface and final composition. It
asserts known pixels, one input upload, zero input download/GPU copy, zero
output copies, and source-upload reuse while rerendering the same SDR frame for
203- and 100-nit reference whites.

The FFV1 case proves real compressed-video demux, timestamp and metadata
retention, exact limited-range BT.709 YUV420P samples, and tolerant
libplacebo-converted linear RGB. Its SAR 32:27 produces a 16:9 Player content
rectangle. A pinned H.264 case uses the production shared-device D3D11VA
decoder and direct NV12 plane importer, asserts no input download/upload or GPU
copy and no output copy/transfer, captures its display-targeted output, and
compares representative pixels against the software decode. Physical display
correctness, fixed mastered HDR input, P010/P012/P016 capture, general
display-matrix rotation, macOS EDR viability,
and Vulkan synchronization remain unproven.
