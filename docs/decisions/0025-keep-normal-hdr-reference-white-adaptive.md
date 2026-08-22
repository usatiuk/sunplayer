# 0025: Keep normal HDR reference-white adaptive

* Status: Accepted
* Date: 2026-08-22
* Supersedes:
  [0024: Map PQ against absolute target luminance](0024-map-pq-against-absolute-target-luminance.md)
  for normal HDR playback
* Amends:
  [0003: Normalize rendered video to one display-targeted linear surface](0003-display-targeted-video-surface.md),
  [0008: Anchor normal HDR playback to the platform reference white](0008-reference-white-adaptive-hdr-display-mapping.md),
  and
  [0023: Use a metadata-first HDR-to-SDR policy](0023-use-metadata-first-hdr-to-sdr-policy.md)

## Context

The rendered-video and composition contract uses one coordinate:

```text
working RGB 1.0 = active platform SDR/reference white
working highlight ceiling = usable peak / reference white
```

ADR 0024 introduced a physical-target branch for decoded PQ and mapped Dolby
on eligible Windows HDR displays. It gave libplacebo the physical peak, scaled
its output by `203 / referenceWhiteNits`, and then retained the final Windows
`referenceWhiteNits / 80` scRGB scale. Those two live-white factors cancel:

```text
(mappedNits / 203) * (203 / W) * (W / 80) = mappedNits / 80
```

That algebra is valid for explicit absolute/reference monitoring, but it is
not SunPlayer's normal-playback intent. It made decoded HDR independent of the
Windows SDR-white control while SDR video, UI, and subtitles continued to
follow it.

The regression passed because one production test asserted the cancellation
itself, changed both reference white and headroom, and stopped at the producer
surface. The compositor test used the analytic producer, which did not enter
the decoded-only branch. The shared capture helper also synthesized physical
peak authority on macOS even though production AppKit state exposes relative
EDR headroom only.

ADR 0024 did contain one valid correction: PQ and mapped Dolby rendered to a
no-headroom SDR target need a nominal 100-nit destination and a fixed
`203 / 100` output-coordinate conversion. That fixed conversion does not cancel
a live platform reference white.

## Decision

Normal playback has no automatic physical-HDR target mode. After source color
inference, the shared libplacebo render context derives its destination solely
from source transfer/representation and target headroom:

```text
absoluteLuminanceSource = PQ transfer or mapped Dolby representation
nominalSdrTarget = absoluteLuminanceSource and targetPeakHeadroom <= 1

coordinateWhite = nominalSdrTarget ? 100 : 203
targetMaximum = coordinateWhite * targetPeakHeadroom
outputNormalization = nominalSdrTarget ? 203 / 100 : 1
```

Every target with headroom above one therefore uses `203 * headroom` and no
producer scale involving the live reference white. Relative SDR and HLG use
the same 203-nit coordinate. Source pixels and retained metadata are unchanged.

A positive physical target minimum is converted into the selected virtual
target by the existing ratio:

```text
targetMinimum = targetMaximum
    * physicalTargetMinimum
    / (referenceWhiteNits * targetPeakHeadroom)
```

Unknown and known-zero minimum retain their distinct shared state and both use
`PL_COLOR_HDR_BLACK` at libplacebo's boundary.

The metadata-first policy remains source-focused. HDR10+ scene/static maxima,
mapped-Dolby range selection, and the diagnosed 1,000-nit missing-PQ fallback
continue on HDR targets with spline. A source-provided HDR10+ OOTF is selected
only for the nominal-SDR target where its authored physical target and the
application destination are coherent. On reference-white-adaptive HDR, valid
scene guidance remains available but the source OOTF is not applied against an
invented physical display target.

Platform serialization remains deliberately different:

* Windows HDR multiplies the complete composition once by `W / 80` and emits
  extended-linear scRGB.
* macOS EDR uses final scale `1`; native component `1.0` is current SDR white
  and AppKit supplies only relative EDR headroom.
* Managed Wayland HDR uses final scale `1`, emits BT.2020/PQ with a 203-nit
  source reference, and lets the compositor anchor that source reference to
  the preferred output reference white.

Target gamut remains separate from the linear-BT.709 storage basis and from
final calibration. Windows publishes validated Advanced Color primaries.
macOS publishes Display P3 only when the active `NSScreen` says it can
represent P3, otherwise BT.709 when representable; this is a conservative
usable gamut, not exact native chromaticities. Managed Wayland publishes
explicit preferred target primaries, falling back to the preferred primary
color volume. Windows, ColorSync, or the Wayland compositor still performs the
one final display-profile/calibration transform.

## Consequences

Benefits:

* Decoded HDR again follows the same live reference-white anchor as the rest
  of normal composition.
* Windows has no producer `203 / W` factor to cancel its required final `W / 80`
  conversion.
* macOS and Wayland retain their native display-referred/reference-white
  semantics instead of inheriting Windows physical-nit assumptions.
* Nominal-100 HDR-to-SDR and the valid metadata-first source policy survive the
  rollback.
* Physical-target authority and absolute-mode state are removed from the
  rendered-surface and color-policy contracts.
* Available platform gamut reaches libplacebo without adding an ICC parser or
  a second calibration transform.

Costs and limitations:

* Normal playback is not an absolute/reference-monitoring mode. Such a mode
  would need an explicit product intent and separate measurement evidence.
* A virtual `203 * headroom` destination is a relative playback construction,
  not a claim that the numeric maximum is a measured physical peak.
* HLG remains display-relative; physical-reference HLG accuracy is not claimed.
* macOS Display P3 is only a conservative capability lower bound. Exact
  ICC-derived target chromaticities and SDR-only wide-gamut presentation remain
  deferred.
* Wayland's stable BT.2020/PQ surface description does not yet declare every
  narrower preferred target volume. Compositor-specific double-mapping risk and
  physical output remain validation items.
* Software/GPU readback proves coordinate behavior, not emitted luminance.

## Required regression

A production decoded PQ frame is rendered twice with fixed headroom,
primaries, and zero target minimum while only reference white changes. Producer
samples must remain stable. Those exact surfaces then cross the real final
compositor: Windows output must change by the reference-white ratio, while
macOS output remains unchanged because its final scale is one. Reduced-headroom
compression, positive-minimum behavior, and nominal-100 HDR-to-SDR remain
separate assertions so none can accidentally serve as a white-scaling oracle.

## Alternatives considered

### Keep ADR 0024 and add a viewing curve

Rejected. A gamma, exposure lift, or second tone curve would hide the wrong
coordinate contract and stack image transforms.

### Rewrite source PQ values or metadata into relative units

Rejected. Every related source fact would need identical rescaling and the
result would no longer be retained source truth.

### Apply a Windows scale on macOS or Wayland

Rejected. Their native reference-white contracts are different; duplicating
Windows's `W / 80` mapping would be incorrect.

### Parse display ICC profiles now

Rejected for this repair. macOS has a bounded P3 capability API and Wayland
already supplies parametric target primaries. Exact ICC profile reduction is a
larger policy, especially for LUT profiles, and final calibration remains
system owned.
