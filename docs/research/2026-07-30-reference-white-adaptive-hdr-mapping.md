# Reference-white-adaptive HDR display mapping

* Date: 2026-07-30
* Scope: SunPlayer's shared SDR/PQ display-mapping contract and its expression
  through libplacebo 7.360.1

## Question

How should SunPlayer map decoded video into its linear, SDR-white-relative
composition surface when the platform's live SDR/reference-white level changes?

The accepted product requirement is:

* Output reference white follows the platform SDR/reference-white level.
* HDR reference white follows that same output anchor during normal playback.
* Display peak is a capability boundary, not a request to expand every source
  to the available peak.
* Retained source values and metadata remain source truth. Render-local
  effective metadata may normalize relative SDR transfers, but never embeds
  display state into the source.
* libplacebo performs source interpretation, tone mapping, and gamut mapping.
* The final compositor applies only the platform representation conversion.

An explicit reference-monitoring mode could choose a different policy later.
It is not normal playback behavior and is not part of this slice.

## Standards and platform evidence

[ITU-R BT.2390-12](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2390-12-2025-PDF-E.pdf)
describes PQ content as optimized for a reference monitor and reference viewing
environment. Section 5.4 requires display mapping when the actual environment
or display cannot reproduce those conditions. Earlier system discussion
describes display adjustment for viewing environment, display limitations, and
viewer preference.

[Microsoft's Advanced Color guidance](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
requires an HDR application to adapt content to the display's luminance and
gamut capabilities. For FP16 scRGB presentation, numeric `1.0` represents
80 nits, while SDR content in a self-composited HDR surface must be scaled for
the system SDR-white level.

These sources establish the need for application-owned display mapping and the
Windows output coordinate conversion. SunPlayer's choice to anchor normal HDR
playback to the platform reference-white level is an accepted product policy.

## Pinned libplacebo behavior

The project pins libplacebo 7.360.1. The exact source used by vcpkg was
inspected, rather than assuming behavior from a newer branch.

Relevant behavior:

* [`PL_COLOR_SDR_WHITE` is 203 nits and `PL_HDR_NORM` defines `1.0` as that value](https://code.videolan.org/videolan/libplacebo/-/blob/v7.360.1/src/include/libplacebo/colorspace.h).
* PQ linearization produces normalized linear light in that fixed coordinate
  system; for example, 203 nits becomes `1.0`.
* [`pl_shader_color_map_ex`](https://code.videolan.org/videolan/libplacebo/-/blob/v7.360.1/src/shaders/colorspace.c)
  derives tone- and gamut-mapping ranges from the inferred source and
  destination color spaces, then writes linear RGB back in the 203-nit
  normalized coordinate system.
* The mapper does not expand a source peak to fill a larger target unless
  inverse tone mapping is explicitly enabled.
* The default color mapper uses spline tone mapping and perceptual gamut
  mapping. The high-quality preset additionally enables contrast recovery;
  SunPlayer does not enable that preset.
* The recommended renderer preset enables source peak detection, but SunPlayer
  currently disables peak detection explicitly. Enabling and validating
  dynamic measurement is a separate quality/performance decision.
* HLG source inference is target-dependent: libplacebo adjusts its effective
  HLG peak from the destination. HLG therefore needs dedicated capture
  validation against the reference-white-relative target rather than being
  declared correct from the PQ result.

## Failure in the previous mapping

The previous pipeline rendered to a physical-nit libplacebo target, then ran a
custom pre-output hook:

```text
libplacebo output
× 203 / activeSdrWhite
× activeSdrWhite / 80 at Windows presentation
```

For PQ content, the live reference-white factors canceled. A nominal 203-nit
source region remained near 203 physical nits even when the Windows SDR-white
level changed. The diagnostic did not catch this because it regenerated its PQ
source from the target reference white on every display change.

## Existing-API bridge

Let:

```text
W = active platform SDR/reference white
P = usable display peak
H = P / W
```

SunPlayer's composition surface means:

```text
linear 1.0 = W
surface maximum = H
```

Libplacebo writes linear `1.0` at its fixed 203-nit normalization. Therefore
SunPlayer describes the desired numerical target in libplacebo's coordinate
system:

```text
virtualTargetMax = 203 × H
virtualTargetMin = max(PL_COLOR_HDR_BLACK, 203 × physicalMin / W)
```

Unknown or measured-zero target minimum uses `PL_COLOR_HDR_BLACK` because
libplacebo reserves numeric zero for unknown metadata and otherwise infers a
linear-target contrast ratio.

The virtual maximum is an adapter coordinate, not a claim about the display's
physical luminance. For example:

```text
W = 100 nits
P = 600 nits
H = 6
virtualTargetMax = 1218
libplacebo output maximum = 1218 / 203 = 6
SunPlayer physical interpretation = 6 × 100 = 600 nits
```

This lets libplacebo own the complete tone/gamut mapping while removing the
custom normalization hook. Source pixels and HDR-transfer metadata are not
rescaled. For an SDR transfer, the render adapter uses a local color-space copy
with nominal white anchored at 203 nits, even when the retained frame reports
an explicit 100-nit mastering maximum. SDR is relative; preserving that
physical maximum would incorrectly render encoded white at `100 / 203`. The
adapter also clears HDR10+ and CIE-Y luminance fields from that local SDR copy
because libplacebo's default metadata policy could otherwise prefer those
stale absolute values. Mastering primaries remain available; the retained
frame and all of its metadata remain unchanged.

After linear composition, platform presentation owns only the coordinate
conversion. On Windows:

```text
scRGB = composed SDR-white-relative RGB × W / 80
```

Equivalent platform adapters must preserve the same semantic contract on
macOS and Linux even though their native output coordinates differ.

## Verification contract

The real D3D11/QRhi/libplacebo capture first asserts that an exact synthetic
203-nit PQ patch becomes surface `1.0`. It then holds one synthetic 1000-nit PQ
signal fixed while rendering it to one physical 600-nit target:

* At 80-nit reference white, target headroom is 7.5 and the source fits.
* At 100-nit reference white, target headroom is 6.0 and the source fits.
* At 203-nit reference white, target headroom is about 2.96 and highlights must
  be compressed.

The test asserts that the first two SDR-white-relative surfaces agree within
GPU tolerance, that the third remains inside declared headroom and is
compressed, that target-only changes do not regenerate or re-upload the fixed
PQ source, and that the final compositor applies `W / 80` exactly once.
Separate SDR captures deliberately carry an explicit 100-nit mastering maximum
plus stale HDR10+/CIE-Y luminance values, and still require relative white
`1.0` at 80, 100, and 203-nit output reference whites.

This proves numeric behavior through the real renderer and compositor up to an
offscreen FP16 target. It does not prove emitted monitor luminance.

## Known gaps

* The working RGB basis is extended-linear BT.709, but SunPlayer does not yet
  provide actual display primaries in `target.color.hdr.prim`. Libplacebo
  therefore infers a BT.709 target gamut. Wide-gamut target observation and
  propagation are required before claiming wide-gamut output.
* HLG's target-dependent OOTF needs a fixed-source, multi-target capture before
  the virtual target is considered validated for HLG.
* Dynamic peak detection, HDR10+, and Dolby Vision need separate metadata and
  performance validation.
* The virtual-target technique follows libplacebo's source behavior but is not
  a first-class destination-reference-white API. If it proves insufficient for
  HLG or dynamic metadata, pursue an upstream reference-white parameter rather
  than adding a second SunPlayer tone mapper.
* Physical luminance and cross-platform equivalence still require actual
  display measurement.
