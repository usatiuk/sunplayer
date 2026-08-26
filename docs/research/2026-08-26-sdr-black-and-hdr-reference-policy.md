# SDR target black and HDR reference-policy fact check

Date: 2026-08-26

Status: grounded working note for the active correction plan

## Verdict

One defect is confirmed: SunPlayer collapses an unknown SDR target minimum and
a known physical zero into libplacebo's `PL_COLOR_HDR_BLACK` sentinel. That
changes the target assumed by BT.2446A and can strongly suppress near-black
detail. Fix this state translation first.

The reported macOS HDR darkness is not yet reduced to a confirmed defect.
Apple documents both a 100-nit ordinary PQ-video convention and newer 203-nit
HDR-reference workflows. SunPlayer needs a controlled native A/B before changing
normal macOS playback. Windows HDR and managed Wayland are explicit no-change
baselines for the SDR repair.

## Confirmed: numeric zero and `PL_COLOR_HDR_BLACK` differ

SunPlayer pins libplacebo 7.360.1. Its
[`pl_hdr_metadata` documentation](https://github.com/haasn/libplacebo/blob/v7.360.1/src/include/libplacebo/colorspace.h)
says numeric `min_luma == 0` is unknown and may infer a default 1000:1
contrast for SDR transfer functions. It recommends a small positive value such
as `PL_COLOR_HDR_BLACK` only to signal a zero black point/infinite-contrast
display. The pinned
[`colorspace.c`](https://github.com/haasn/libplacebo/blob/v7.360.1/src/colorspace.c)
implements that default as `peak / 1000` for a non-HDR transfer.

Before this correction, SunPlayer instead did this in
`src/video/libplacebo/LibplaceboRenderContext.cpp`:

```cpp
if (!description.targetMinimumLuminanceKnown ||
    description.targetMinimumLuminanceNits == 0.0f) {
    return PL_COLOR_HDR_BLACK;
}
```

For the nominal 100-nit linear SDR target, the two meanings are therefore:

```text
unknown minimum -> 0.1 nit after libplacebo inference
known zero      -> PL_COLOR_HDR_BLACK, approximately 0.000001 nit
```

This is an adapter-semantics defect, not an exposure or gamma hypothesis.

## Quantitative shadow effect

Using the pinned BT.2446A implementation with a 1000-nit PQ source and 100-nit
SDR target gives approximately:

| Source nits | Known-zero target | Unknown target, inferred 0.1 nit |
| ---: | ---: | ---: |
| 0.01 | 0.0048 | 0.174 |
| 0.05 | 0.0225 | 0.264 |
| 0.1 | 0.0438 | 0.338 |
| 0.5 | 0.203 | 0.707 |
| 1 | 0.388 | 1.040 |
| 5 | 1.69 | 2.89 |

The source lower bound used here follows pinned libplacebo's effective PQ
handling; it is not an assumption that a 0.005-nit mastering-display minimum
passes straight through the calculation. The delta is large enough to match
the reported loss of dark-room texture on Windows SDR.

## Confirmed production and test gap

`WindowsDisplayStateProvider` reads `MinLuminanceInNits`, but currently marks a
display luminance range authoritative only in HDR mode. The production Windows
SDR request consequently has no authoritative target minimum. This does not
claim Windows is incapable of reporting an SDR minimum; broader use of the
property is a separate investigation.

Prior FFmpeg integration coverage defaulted the numeric minimum to zero while
always setting `targetMinimumLuminanceKnown = true`. It therefore exercised
known physical zero, not the production unknown SDR state. The prior BT.2446A
unit vector started at zero and then jumped to 10 nits, so it also could not
detect destruction of the bottom decade.

The mapper must be explicit in new coverage. Current policy is intentionally
not “all PQ to SDR uses BT.2446A”:

* usable HDR10+ source OOTF selects ST 2094-40;
* HDR10+ scene/static MaxCLL/mastering and mapped Dolby ranges select BT.2446A
  for SDR;
* completely metadata-less PQ compatibility fallback selects spline.

## Conservative target-minimum repair

SunPlayer always describes the rendered target to libplacebo as linear. Passing
unknown zero for extended-linear HDR/EDR would make the pinned generic SDR
fallback infer `targetPeak / 1000` there too. The bounded translation is:

```text
unknown and H <= 1  -> 0
unknown and H > 1   -> PL_COLOR_HDR_BLACK (preserve existing behavior)
known zero          -> PL_COLOR_HDR_BLACK
known positive      -> existing physical-range conversion
```

The H>1 choice is preservation pending better target-black authority, not a
claim that an HDR display physically has perfect black.

## Platform reference policies are separate from this defect

### Windows

[Microsoft's Advanced Color documentation](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
defines native HDR scRGB `1.0` as 80 nits and tells applications that mix SDR
and HDR in one surface how to match SDR/UI white using `W / 80`. SunPlayer has
chosen to scale the complete mixed composition once, making normal playback
reference-white adaptive. That is a SunPlayer viewing contract, not a Windows
law requiring all HDR video to be scaled this way.

The reported Windows HDR output currently looks correct. Its existing
`203 * H` producer mapping and single final `W / 80` scale are therefore a
protected native baseline, not a cleanup target.

### macOS

Apple's
[`Performing your own tone mapping`](https://developer.apple.com/documentation/metal/performing-your-own-tone-mapping)
defines EDR `1.0` as current SDR/reference white. Its
[WWDC21 manual PQ guidance](https://developer.apple.com/videos/play/wwdc2021/10161/)
describes dividing absolute PQ luminance by a medium reference white and uses
100 nits for PQ video. Apple's AVFoundation-consistent
[`Using system tone mapping on video content`](https://developer.apple.com/documentation/metal/using-system-tone-mapping-on-video-content)
example uses CAEDRMetadata `opticalOutputScale = 100`. That example makes 100 a
legitimate diagnostic candidate; it does not prove equivalence for SunPlayer's
self-tone-mapped mixed UI/video layer.

Those sources establish a legitimate Apple ordinary-video convention, not a
universal PQ truth. [ITU-R BT.2408](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-6-2023-PDF-E.pdf)
uses 203 cd/m² HDR Reference White, and Apple's newer
[`Authoring Headroom Adaptive Gain Curve metadata`](https://developer.apple.com/documentation/colorsync/authoring-headroom-adaptive-gain-curve-metadata)
guidance for the HAGC/ICC imaging-profile workflow recognizes a standard
203-nit HDR reference with customization for other mastering anchors. This is
evidence of a distinct modern HDR-reference convention, not a second mandate
for ordinary video. The correct SunPlayer normal-playback choice must be
established by same-display comparison with QuickTime/AVFoundation.

### Managed Wayland

The current
[`color-management-v1` ST2084 definition](https://wayland.app/protocols/color-management-v1)
defaults reference luminance to 203 cd/m², derived from BT.2408. SunPlayer's
managed HDR path encodes working `1.0` as PQ 203 and does not override that
named-transfer default. This is internally coherent and remains unchanged.

## Decision for the active patch

Fix unknown SDR black and add direct plus production-path regressions. Retain
nominal SDR normalization and all mapper choices while isolating the defect.
Treat Windows HDR as a no-regression baseline. Leave Wayland unchanged. Keep
macOS 100-versus-203 and BT.2446A-versus-spline as independent native
experiments. Do not add a cross-cutting absolute-video-anchor field until the
macOS policy is actually decided.
