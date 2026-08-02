# 0014: Prefer native Metal presentation for the macOS graphics domain

* Status: Accepted
* Date: 2026-08-01
* Supersedes: the macOS realization preference in
  [0004: Establish one graphics-device domain and explicit video interop seams](0004-cross-platform-graphics-domain-and-video-interop.md)

## Context

ADR 0004 originally required shared QRhi/libplacebo Vulkan over MoltenVK to be
the first macOS realization tested. Subsequent color-management review made
native presentation semantics more important than graphics-API symmetry:
macOS EDR and ColorSync presentation are naturally expressed through a Metal
layer with an accurately declared extended-linear color space.

Libplacebo 7.360.1 has no Metal GPU backend, so native Metal presentation does
not eliminate Vulkan/MoltenVK from the video producer. The existing graphics
domain and target-interop seams already allow the producer and compositor to
have a backend-specific sharing relationship without exposing native types to
playback.

## Decision

The intended first macOS presentation path uses QRhi Metal for Qt Quick,
composition, EDR, and ColorSync surface declaration. Libplacebo uses Vulkan
over MoltenVK as the video producer, with a narrow backend-owned Metal-texture
interop path into the QRhi composition domain.

Sunroom will not force QRhi Vulkan merely to share one graphics API if doing so
makes EDR declaration, ColorSync behavior, or energy characteristics less
direct. A fully shared MoltenVK domain remains an evidence-driven alternative
and may replace the interop path if experiments prove equivalent native EDR,
calibration, synchronization, and efficiency.

No new general abstraction is introduced by this decision. The existing
graphics-device domain owns both native environments and teardown order; the
target-interop contract owns sharing, copies, synchronization, and diagnostics.

## Consequences

* Native macOS presentation and display calibration have a clear owner.
* The implemented first slice requires QRhi and MoltenVK to expose the same
  `MTLDevice`, imports QRhi's RGBA16F Metal texture directly into libplacebo,
  and orders access with an exported Vulkan timeline semaphore/Metal shared
  event. VideoToolbox planes use retained CoreVideo Metal views on that same
  device.
* Direct no-copy output and NV12/P010 input are verified on the available
  Apple M2 host. Any later GPU copy or CPU fallback remains explicit and
  diagnosed.
* Physical EDR headroom, unlike-display transitions, device recovery, and the
  packaged deployment matrix remain separate validation gates.
* The architecture remains compatible with a later shared-MoltenVK result
  without changing playback or the rendered-video surface contract.

## Alternatives considered

### Require a shared MoltenVK domain first

Superseded as the preferred order. It may reduce interop, but native EDR and
ColorSync presentation are stronger platform requirements than using one API.

### Add a custom Metal video renderer

Rejected. Libplacebo remains the content renderer; Sunroom should solve the
narrow interop problem instead of duplicating its color pipeline.
