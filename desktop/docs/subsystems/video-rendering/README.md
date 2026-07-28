# Video rendering subsystem

## Status

Sunroom currently has an explicit rendered-video surface contract and a
procedural QRhi diagnostic producer. libplacebo, decoded-frame import, and
platform video-target interop are not implemented.

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

* The graphics-device domain owns QRhi, libplacebo GPU state, native backend
  state, device generation, synchronization, capabilities, diagnostics, and
  teardown order.
* The rendered-video producer owns invalidation and completion semantics.
* The libplacebo target bridge makes one render target available to libplacebo
  and QRhi without exposing native types.
* The frame importer maps software and platform hardware frames into
  libplacebo input planes while retaining their lifetime.

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

1. Extract the graphics-device domain and rendered-video producer contract;
   route the current diagnostic producer through them without behavior change.
2. Define the libplacebo target-interop contract, output-path diagnostics, and
   lifecycle rules.
3. Add a pinned libplacebo dependency and the Windows D3D11 shared-target
   implementation.
4. Render known SDR and HDR/extended-value software images and capture both the
   video surface and final composition.
5. Add the Vulkan implementation and exercise it on Linux.
6. Validate MoltenVK presentation on macOS before choosing shared Vulkan or a
   Metal interop backend.
7. Add FFmpeg software and hardware frame importers through the established
   input seam.

## Verification

Shared policy receives focused state tests. Each native backend requires a real
GPU integration test using the production libplacebo renderer and QRhi
compositor. Cross-backend output comparisons use declared tolerances.

Physical display correctness, macOS EDR viability, Vulkan synchronization, and
hardware-decoder zero-copy behavior remain platform-lab requirements rather
than claims made by the current Windows test.
