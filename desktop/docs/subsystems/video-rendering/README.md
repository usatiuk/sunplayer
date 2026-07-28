# Video rendering subsystem

## Status

Sunroom currently has an explicit rendered-video surface contract and a
procedural QRhi diagnostic source and producer behind shared lifecycle
contracts. The graphics domain factory-selects the current direct QRhi target,
which reports provisioning state, synchronization, texture revision, and its
zero-copy path. libplacebo, decoded-frame import, native libplacebo target
interop, and real fallback paths are not implemented.

The broad investigation in
[../../ARCHITECTURE_NOTES.md](../../ARCHITECTURE_NOTES.md) is non-binding
research. The accepted device and target boundary is recorded in
[ADR 0004](../../decisions/0004-cross-platform-graphics-domain-and-video-interop.md).

## Accepted structure

```text
diagnostic source ──→ diagnostic producer ───────────────┐
                                                        │
decoded frame ──→ frame importer ──→ libplacebo producer│
                                      + target interop ──┤
                                                        ↓
RenderedVideoSurface / QRhiTexture
        ↓
shared QRhi compositor
```

The known seams are established with their first implementations:

* The graphics-device domain owns QRhi, native backend state, device
  generation, backend/adapter diagnostics, and teardown order. It will also
  own libplacebo GPU state and report relevant capabilities once that
  dependency is integrated.
* The rendered-video source owns source-specific state, content revision,
  cadence, update requests, and device-recreatable producer creation.
* The rendered-video producer owns invalidation, submission reporting, and
  rendered-state commit/discard semantics without exposing source-specific
  controls to the presentation engine.
* The target lifecycle covers provisioning, result-bearing producer access,
  preparation for composition, submission acceptance/abort, the composition
  texture and its revision, and path diagnostics. The graphics domain selects
  the implementation and the producer owns the returned target.
* The future frame importer will map software and platform hardware frames into
  libplacebo input planes while retaining their lifetime; that seam is not yet
  implemented.

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
diagnostic state. A fallback must not silently redefine the surface's color or
luminance meaning.

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
3. [ ] Add a pinned libplacebo dependency, its renderer-facing target access
   contract, and the Windows D3D11 direct/copy/fallback implementations.
4. [ ] Render known SDR and HDR/extended-value software images and capture both the
   video surface and final composition.
5. [ ] Add the Vulkan implementation and exercise it on Linux.
6. [ ] Validate MoltenVK presentation on macOS before choosing shared Vulkan or a
   Metal interop backend.
7. [ ] Add FFmpeg software and hardware frame importers through the established
   input seam.

## Verification

Target diagnostic schema has focused state tests. The real D3D11 integration
test creates the factory-selected graphics domain, drives the diagnostic source
and producer through shared interfaces, captures the direct QRhi target and
composition, verifies zero-copy diagnostics, exercises successful and
discarded render-state promotion after accepted submissions, and
reprovisions/resizes the texture through an explicit revision and compositor
rebind. Each future native libplacebo backend requires equivalent real-GPU
coverage. Cross-backend output comparisons use declared tolerances.

Physical display correctness, macOS EDR viability, Vulkan synchronization, and
hardware-decoder zero-copy behavior remain platform-lab requirements rather
than claims made by the current Windows test.
