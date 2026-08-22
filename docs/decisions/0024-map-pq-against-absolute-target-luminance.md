# 0024: Map PQ against absolute target luminance

* Status: Superseded for normal HDR playback by
  [0025: Keep normal HDR reference-white adaptive](0025-keep-normal-hdr-reference-white-adaptive.md)
* Date: 2026-08-22
* Implementation status: Retained only for PQ and mapped Dolby input on the
  nominal-SDR target; the automatic physical-HDR branch was removed by ADR 0025
* Amends:
  [0008: Anchor normal HDR playback to the platform reference white](0008-reference-white-adaptive-hdr-display-mapping.md)
  and
  [0023: Use a metadata-first HDR-to-SDR policy](0023-use-metadata-first-hdr-to-sdr-policy.md)

ADR 0025 retains this decision's nominal-100 PQ/Dolby HDR-to-SDR construction
and fixed `203 / 100` coordinate conversion. It rejects the automatic physical
HDR branch as normal-playback behavior because its `203 / referenceWhite`
producer scale cancels the platform reference-white anchor.

## Context

SunPlayer's video surface is linear extended BT.709 with `1.0` equal to the
active platform reference white `W`. Libplacebo's linear output instead uses a
fixed unit where `1.0 = 203 cd/m²`.

The previous adapter used `203 * targetPeakHeadroom` as libplacebo's target
maximum. That made libplacebo's output numbers fit the surface without a final
unit conversion, but the same field also controls physical tone and gamut
mapping. Absolute PQ pixels and HDR metadata remained in cd/m² while the
destination changed with `W` as if its virtual number were cd/m².

The virtual target and the independent implicit-10,000-nit missing-PQ fallback
jointly explain the reported failure modes. A low-peak HDR10+ scene could fit
below the virtual 203-nit SDR target and remain much too dark on a nominal SDR
display. On HDR, metadata-less PQ was compared with an implicit 10,000-nit
source range and a virtual destination; that combination is consistent with
the reported dull Soul sample, although the visual observation is not a
measurement. ST 2094-40 also requires its source-provided target and actual
display target to use the same physical unit.

## Decision

Absolute PQ processing uses a real target luminance when the surface contract
contains authoritative physical target luminance. The shared policy marks PQ
and mapped Dolby decisions as absolute only in that state; the renderer derives
the target from the already shared surface description:

```text
R = 100 cd/m²                         when targetPeakHeadroom == 1
R = referenceWhiteNits               when targetPeakHeadroom > 1

libplaceboTargetMax = R * targetPeakHeadroom
libplaceboTargetMin = max(
    PL_COLOR_HDR_BLACK,
    libplaceboTargetMax
        * physicalTargetMin
        / (referenceWhiteNits * targetPeakHeadroom))
```

At headroom one, this is a nominal 100-nit SDR/WCG destination and needs no
measured monitor peak. With HDR headroom, it is the display's physical peak
only when automatic target-peak selection is active, presentation is
scene-referred, that luminance range is known, and its peak is not below active
reference white. Manual headroom, display-referred preferred-target values,
and incoherent telemetry remain relative even when their contracts contain
luminance-like numbers. Source PQ pixels, source maxima, and dynamic-metadata
target luminance remain unchanged in physical cd/m².

Peak authority and minimum-luminance knowledge remain separate surface facts.
A relative or display-referred target may still provide a valid minimum for
libplacebo's contrast model even though its peak cannot authorize absolute PQ
mapping.

After libplacebo completes tone mapping and perceptual gamut mapping, one
uniform linear scale converts only the coordinate unit:

```text
surfaceRgb = libplaceboRgb * 203 / R
```

Libplacebo emits `outputNits / 203`; the scale therefore stores
`outputNits / R`. It is not a gamma adjustment, exposure control, or second
tone mapper. Uniform scaling after gamut mapping preserves chromaticity and
extended-BT.709 wide-gamut coordinates.

On Windows HDR, the existing whole-composition conversion remains:

```text
scRgb = surfaceRgb * referenceWhiteNits / 80
```

For absolute HDR video the two coordinate conversions reduce to
`outputNits / 80`, while SDR UI remains anchored to Windows' current SDR-white
level. Windows still owns calibrated scRGB-to-display conversion.

Relative SDR and HLG sources retain the existing display-relative target and
receive no output normalization. PQ/Dolby on an HDR target whose physical
luminance is unknown also retains that relative path and diagnoses the missing
authority; source-provided ST 2094-40 target adaptation is not selected against
invented nits. The target facts and policy remain shared; Windows is only the
first physical acceptance platform.

## Consequences

Benefits:

* On eligible automatic scene-referred targets, libplacebo receives physical
  source and target luminance in one unit for PQ, including ST 2094-40
  adaptation.
* With authoritative physical target luminance, a reference-white change cannot
  silently change the physical HDR mapping; it changes only the surface
  coordinate used to represent that result.
* Headroom-one PQ is mapped directly to nominal 100-nit SDR rather than to a
  virtual 203-nit destination followed by a corrective viewing curve.
* The implementation adds no tone curve, image analysis, temporal state,
  platform color policy, or user setting.
* Target primaries remain independent of the extended-BT.709 storage basis.

Costs and limitations:

* Normal PQ video on eligible automatic scene-referred HDR is now
  absolute/display-light relative while SDR UI remains
  platform-reference-white relative. This matches the Windows Advanced Color
  contract but narrows ADR 0008's earlier statement that HDR video itself
  follows the SDR-white control.
* Nominal 100 cd/m² is a signal/viewing convention, not a claim that an
  uncalibrated SDR desktop was physically measured at 100 cd/m².
* HLG retains its prior display-relative path. Its display-dependent OOTF and
  physical-target separation remain a distinct validation item.
* macOS currently reports relative EDR headroom without authoritative physical
  target luminance. It therefore retains the relative HDR path rather than
  treating the 80-nit scRGB coordinate fallback as measured display data.
* Managed Wayland PQ is display-referred, and manual target headroom is a user
  coordinate rather than measured display peak. Both retain the relative HDR
  path.
* Physical screenshots cannot prove emitted luminance; exact-frame viewing and
  measurement remain required for perceptual claims.

## Alternatives considered

### Keep the virtual target and add a gamma lift

Rejected. The explored `1/1.15` and `1/1.08` post-curves changed image intent
without correcting the physical target supplied to ST 2094-40. The stronger
value belongs to a different BT.2408 workflow; Annex 11's optional value is for
an already-derived 203-nit SDR signal, not a storage-coordinate workaround.

### Rescale source pixels or dynamic metadata into virtual units

Rejected. It would require rewriting every related source fact consistently
and would be more fragile than supplying the real destination and converting
only the final linear output unit.

### Tune exposure or make the reported 357-nit object white

Rejected. That would spend unknown highlight headroom based on one scene. The
deterministic metadata-first baseline must be judged before considering a
different mapper or measured peak detection.

### Implement another tone mapper

Rejected. Libplacebo already owns the selected ST 2094-40, BT.2446A, spline,
and gamut operations. SunPlayer's responsibility is to provide coherent facts
and select among those operations.
