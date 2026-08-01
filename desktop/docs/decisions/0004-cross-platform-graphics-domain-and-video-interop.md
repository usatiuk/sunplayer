# 0004: Establish one graphics-device domain and explicit video interop seams

* Status: Accepted
* Date: 2026-07-28
* Superseded in part by:
  [0014: Prefer native Metal presentation for the macOS graphics domain](0014-prefer-native-metal-presentation-on-macos.md)

## Context

Sunroom will use libplacebo on Windows, Linux, and macOS, while final
composition remains in QRhi. The native device, texture-sharing, and
synchronization mechanisms necessarily differ:

* Windows can share one D3D11 device and texture.
* Linux can share Vulkan devices and images but requires explicit queue,
  layout, and semaphore ownership.
* macOS must first validate a shared Vulkan/MoltenVK path and may require a
  MoltenVK-to-Metal texture bridge for native EDR presentation.

These are known architectural requirements, not hypothetical extensibility.
Embedding D3D11 ownership directly in the presentation engine and extracting a
backend seam later would allow the first platform to dictate shared behavior.

A CPU framebuffer would be superficially portable, but normal playback would
download and re-upload every rendered frame. An unconditional GPU copy would
also hide a performance cost that direct shared targets can avoid.

## Decision

Create the known abstraction seams with the first implementation:

* A factory-selected graphics-device domain owns the native graphics
  environment, QRhi, libplacebo GPU, device generation, capabilities,
  synchronization facilities, diagnostics, and their teardown order.
* A shared rendered-video producer contract covers surface creation,
  invalidation, rendering, composition-texture access, and successful or
  failed frame completion. The diagnostic producer is its first implementation;
  libplacebo becomes another implementation.
* A libplacebo target-interop contract owns the backend-specific relationship
  between the `pl_tex` render target and the `QRhiTexture` sampled by the
  compositor.
* A frame-import contract owns the relationship between decoded software or
  hardware frames and libplacebo input planes.

Shared contracts may expose cross-platform QRhi and libplacebo types inside the
graphics/video-rendering subsystem. They must not expose D3D11, Vulkan, Metal,
IOSurface, dma-buf, queue, semaphore, or native decoder types.

The existing `RenderedVideoSurfaceDescription` and
`RenderedVideoSurfaceState` remain the semantic producer/consumer boundary.
Native allocation ownership is an implementation detail: the producer owns the
logical rendered surface and its QRhi composition view, while the backend
bridge may arrange the underlying native allocation.

Every backend reports its actual output path:

1. Shared render target with no output copy.
2. Same-device GPU copy.
3. CPU round trip as an explicit degraded or test path.

The report includes backend and adapter identity, target ownership,
synchronization mode, known GPU copies, known CPU transfers, and fallback
reason.

## Platform realization

The initial intended paths are:

* Windows: QRhi D3D11 and libplacebo D3D11 share one device; libplacebo wraps
  the QRhi-owned RGBA16F texture and native commands are bracketed through
  QRhi's external-command mechanism. The domain also gives that video-capable
  device to FFmpeg, enables immediate-context multithread protection, and
  serializes decoder callbacks with QRhi/libplacebo GPU resource, command, and
  teardown phases through one backend scope.
* Linux: QRhi and libplacebo share one Vulkan device; image layout,
  queue-family ownership, and semaphore state are explicit in the Vulkan
  backend.
* macOS: first validate QRhi Vulkan and libplacebo Vulkan over one MoltenVK
  domain. If native EDR requirements rule that out, add a backend that shares
  an IOSurface/Metal texture between MoltenVK rendering and QRhi Metal
  composition.

Unsupported backends fail through capability results and diagnostics rather
than leaking platform conditionals into shared rendering or playback code.

## Consequences

Benefits:

* Cross-platform ownership and fallback behavior shape the first
  implementation.
* The compositor remains independent of source format, libplacebo backend, and
  copy strategy.
* Direct sharing, GPU-copy fallback, and CPU fallback remain observable rather
  than implicit.
* Device loss has one aggregate teardown owner.
* New backends implement known contracts instead of forcing extraction from
  Windows code.

Costs:

* The foundation includes interfaces and factories that initially have only
  one executable backend and one direct QRhi target implementation.
* Vulkan and macOS synchronization still require platform validation.
* QRhi native interop uses private Qt APIs and must remain version-pinned and
  isolated.

## Alternatives considered

### Add D3D11 integration directly and extract later

Rejected because the other supported platform paths and their ownership
differences are already known.

### Always render to a CPU framebuffer

Rejected as the normal path because it requires a GPU download and upload per
frame. It remains a useful explicit fallback and deterministic test path.

### Always render to a separate libplacebo texture and copy

Rejected as the default because it adds work even when QRhi and libplacebo can
share a render target. A same-device GPU copy remains a capability fallback.

### Build another general-purpose GPU abstraction

Rejected. QRhi and libplacebo already provide their respective abstractions.
Sunroom needs only the narrow device, target, synchronization, and diagnostic
bridge between them.

## Not decided here

* Whether future compatible windows share a graphics domain; the current
  single window owns one domain.
* The final Vulkan queue and semaphore strategy.
* Whether shared MoltenVK presentation satisfies macOS EDR and energy goals.
* Hardware-decoder input import details for Linux and macOS.
* Which direct-import failures justify same-device GPU-copy or CPU fallback
  instead of the implemented one-shot software re-decode.
