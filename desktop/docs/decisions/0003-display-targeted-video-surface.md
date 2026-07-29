# 0003: Normalize rendered video to one display-targeted linear surface

* Status: Accepted
* Date: 2026-07-28

## Context

The player must eventually accept SDR and HDR sources with different
primaries, transfer functions, ranges, bit depths, static metadata, and dynamic
metadata. Letting those source distinctions reach the final compositor would
couple presentation, UI composition, and every future media format.

The first concrete producer is only a procedural diagnostic pattern, but it
needs to cross the same boundary that libplacebo-rendered video will use.

## Decision

Rendered video crosses into final composition as a producer-owned,
active-viewport-sized texture with this contract:

* RGBA16F.
* Linear RGB using sRGB/BT.709 D65 primaries.
* Extended floating-point components; values are not restricted to `[0, 1]`.
* Opaque alpha.
* Canonical top-left sampling coordinates.
* SDR-white-relative luminance, where RGB `1.0` is the recorded
  SDR/reference-white luminance.
* Display-targeted color processing and tone mapping are complete before the
  final compositor samples the texture.

The final compositor may place the video layer, blend described UI and subtitle
layers, scale the composed SDR-white-relative result into the active
presentation convention, and encode the swapchain. It must not interpret
source video metadata, select a source transfer function, or tone-map video.

The producer owns the logical surface and its composition-visible QRhi texture
view. Backend interop may arrange ownership of the underlying native
allocation. The compositor borrows the QRhi view for the current presentation
device. A completed surface records:

* The graphics-device generation.
* The effective display-target revision.
* The producer-content revision.
* Its pixel size, reference white, target headroom, and fixed color semantics.

Reuse requires an exact match of that state. Swapchain identity is
intentionally absent, so an equivalent swapchain recreation does not discard a
valid surface. Device, display-target, content, or destination-size changes
invalidate it. A recorded render becomes completed and reusable only after the
owning QRhi frame ends successfully; failed frame submission discards the
pending state.

FFmpeg will normalize effective source metadata and libplacebo will perform
source decoding, chroma reconstruction, transfer and gamut conversion, tone
mapping, scaling, and dithering as applicable. SDR, HDR10/PQ, HLG, dynamic HDR,
and other supported source forms therefore converge on this one consumer
contract. This decision defines the convergence point; it does not claim those
source formats are implemented yet.

## Consequences

Benefits:

* The final compositor is independent of source format and HDR standard.
* SDR and HDR content can share geometry, UI, subtitle, and presentation code.
* Paused content can be rerendered when the effective display target changes.
* Swapchain-only recovery can retain producer resources.
* Old-device textures cannot be considered reusable after device recovery.
* The pure state contract can be tested without QRhi or a window.

Costs and limitations:

* Display-targeted surfaces may need rerendering when SDR white, headroom, or
  output selection changes.
* A fixed linear-sRGB working surface can represent extended and negative
  values but does not eliminate precision or gamut-mapping choices upstream.
* Active viewport dimensions are aligned to integer physical pixels, so
  fractional device-pixel ratios can move an edge by less than one pixel
  relative to its logical QML geometry.
* The current diagnostic producer's tone mapper is temporary and is not a
  claim of video color correctness.

## Alternatives considered

### Pass source-format textures to the final compositor

Rejected. It would duplicate FFmpeg/libplacebo responsibilities and make each
new source color space or HDR format a presentation concern.

### Store active scRGB component values in the video texture

Not selected. Keeping the surface SDR-white-relative lets video and SDR UI be
combined under one luminance convention before the final presentation scale is
applied once.

### Tie surface validity to the swapchain generation

Rejected. The producer and its texture use the QRhi device, not the visible
swapchain render-pass descriptor. Equivalent swapchain recreation should only
rebuild and rebind the final compositor.
