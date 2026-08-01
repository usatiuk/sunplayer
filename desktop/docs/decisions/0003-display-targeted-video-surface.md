# 0003: Normalize rendered video to one display-targeted linear surface

* Status: Accepted
* Date: 2026-07-28
* Amended by:
  [0008: Anchor normal HDR playback to the platform reference white](0008-reference-white-adaptive-hdr-display-mapping.md)
* Related:
  [0013: Rely on system display calibration on managed presentation paths](0013-rely-on-system-display-calibration.md)

## Context

The player must eventually accept SDR and HDR sources with different
primaries, transfer functions, ranges, bit depths, static metadata, and dynamic
metadata. Letting those source distinctions reach the final compositor would
couple presentation, UI composition, and every future media format.

The first concrete producers render a diagnostic pattern through procedural
QRhi and libplacebo paths, but both need to cross the same boundary that
decoded video will use.

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
* Its pixel size, reference white, target minimum-luminance value and known
  state, target headroom, and fixed color semantics.

Reuse requires an exact match of that state. Swapchain identity is
intentionally absent, so an equivalent swapchain recreation does not discard a
valid surface. Device, display-target, content, or destination-size changes
invalidate it. A recorded render becomes completed and reusable only after the
owning QRhi frame ends successfully; failed frame submission discards the
pending state.

FFmpeg supplies the final decoded frame and libplacebo performs source
interpretation, chroma reconstruction, transfer and gamut conversion, tone
mapping, scaling, and dithering as applicable. SDR, HDR10/PQ, HLG, dynamic HDR,
and other supported source forms therefore converge on this one consumer
contract. Sunroom validates representative paths and reports observed
capabilities instead of reimplementing either library's format policy.

Platform display adapters own native observation and report physical display
facts. Shared presentation policy resolves those facts into the effective
target—including minimum luminance when known—supplied to the producer. The
producer must describe the destination so renderer output already satisfies
this surface contract. For relative SDR and static PQ, the current libplacebo
bridge expresses the target range in its fixed 203-nit normalized coordinate
system: target maximum is `203 * targetPeakHeadroom`, and target minimum is
converted by the same reference-white-relative relationship. HLG and dynamic
HDR require format-specific target semantics and must not inherit that formula
uncritically. No video-only normalization runs after the display map. The final
compositor remains unaware of libplacebo's internal coordinate system and of
the source format. Platform presentation then maps the complete final
composition to the selected OS swapchain convention without tone-mapping video
again.

The producer's target gamut and content mapping do not apply the monitor's ICC
calibration. On a system-managed path, the platform applies that final
calibration once to the complete tagged composition. Any future
application-managed display ICC mode must likewise operate after composition,
not inside the video producer.

Sunroom preserves the distinction between unknown minimum luminance and a
known physical zero. Renderer adapters translate that representation at their
API boundary. In particular, libplacebo reserves numeric zero for unknown
metadata and otherwise infers a linear-target contrast ratio, so its adapter
uses `PL_COLOR_HDR_BLACK` when the minimum is unknown or physically zero. This
does not change the value or known state stored in shared presentation state.

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
* The procedural diagnostic producer's tone mapper is temporary and is not a
  playback renderer or a claim of video color correctness.

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
