# Video rendering subsystem

## Status

Sunroom has an explicit rendered-video surface contract and two diagnostic
producers behind the same lifecycle. HDR Lab uses the persistent libplacebo
7.360.1 producer by default and can select the procedural QRhi producer for
diagnostic comparison. This switch is not a player fallback: real playback
will use libplacebo and report an error if no supported libplacebo target path
can be created. On Windows, libplacebo shares the QRhi D3D11 device and
immediate context, wraps the QRhi-owned RGBA16F texture, and renders directly
without an output copy. Decoded-frame import and real copy/CPU target paths are
not implemented.

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
  libplacebo GPU, device generation, backend/adapter diagnostics, and teardown
  order.
* The rendered-video source owns source-specific state, content revision,
  cadence, update requests, and device-recreatable producer creation.
* The rendered-video producer owns invalidation, submission reporting, and
  rendered-state commit/discard semantics without exposing source-specific
  controls to the presentation engine.
* The target lifecycle covers provisioning, result-bearing producer access,
  preparation for composition, submission acceptance/abort, the composition
  texture and its revision, and path diagnostics. The graphics domain selects
  the implementation and the producer owns the returned target.
* The analytic producer currently models the software-frame branch with one
  persistent 640×360 RGBA32F texture and buffer. It performs one explicit
  CPU-to-GPU upload when the input frame changes and reuses that texture for
  target-only rerenders; the work does not scale with the viewport. The future
  frame importer will map decoded software planes or platform hardware
  surfaces into libplacebo input planes while retaining their lifetime and
  synchronization state; that seam is not yet implemented.

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

The final QRhi compositor only places the resulting linear BT.709 surface,
blends other described layers in the same reference-white-relative convention,
and converts the final composition to the selected presentation convention.
The platform presentation backend owns swapchain choice and OS output encoding.
Neither one reinterprets source video metadata or tone-maps the video again.

## Platform backends

| Platform | Intended first path | Main unresolved risk |
| --- | --- | --- |
| Windows | Shared D3D11 device and RGBA16F texture | Immediate-context ordering and driver format support |
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
10. [ ] Add FFmpeg software and hardware frame importers through the established
   input seam.

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

Physical display correctness, macOS EDR viability, Vulkan synchronization, and
hardware-decoder zero-copy behavior remain platform-lab requirements rather
than claims made by the current Windows test.
