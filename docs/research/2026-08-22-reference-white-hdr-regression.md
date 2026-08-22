# Reference-white HDR regression analysis

* Date: 2026-08-22
* Status: implemented and independently reviewed
* Scope: commits `84c8526` and `5f5b737`, normal HDR playback, and the
  Windows, macOS, and managed-Wayland presentation contracts

## Question

How did normal HDR playback stop following the active platform
SDR/reference-white level, which parts of the recent HDR work remain correct,
and what is the smallest cross-platform correction?

## Product invariant

Normal playback uses one shared linear composition meaning:

```text
working RGB 1.0 = active platform SDR/reference white
working highlight ceiling = usable display peak / reference white
```

Video, subtitles, and UI share that anchor. The platform-specific final step
only serializes the shared meaning:

* Windows HDR: extended-linear scRGB, where native `1.0` is 80 nits, so the
  complete composition is multiplied by `W / 80`.
* macOS EDR: extended-linear EDR, where native `1.0` is current SDR white, so
  the final scale is `1`.
* Managed Wayland HDR: BT.2020/PQ with a 203-nit source reference white. The
  compositor anchors that source reference to the preferred output reference
  white, so the application final scale is `1`.

An absolute/reference-monitoring mode is a different viewing intent. It may be
added explicitly later, but it is not the normal-playback default.

## Regression reconstruction

Let:

```text
W = active Windows SDR white in nits
P = physical display peak in nits
H = P / W
L = libplacebo's mapped output luminance in nits
```

Before `5f5b737`, normal HDR rendering expressed the destination as a virtual
libplacebo target:

```text
target max = 203 * H
producer surface = mapped output in the 203-nit coordinate
Windows output = producer surface * W / 80
```

When content fits, a fixed reference region remains stable in the producer
surface and follows `W` at final composition. Highlight capacity still changes
with `H`, so the mapper compresses content that no longer fits.

`5f5b737` instead enabled an automatic physical-target mode for decoded PQ and
mapped Dolby Vision on eligible Windows HDR outputs:

```text
libplacebo target max = W * H = P
libplacebo linear output = L / 203
new producer hook = 203 / W
Windows compositor = W / 80

final Windows value
    = (L / 203) * (203 / W) * (W / 80)
    = L / 80
```

The active SDR-white value cancels exactly. SDR UI still follows `W`, while
decoded PQ/Dolby video does not. This is the reported product regression.

The commit's arithmetic is coherent for absolute/reference-monitoring output
and matches Windows' native scene-referred scRGB convention. The mistake was
changing SunPlayer's normal-playback viewing intent while presenting the change
as a target-unit correction.

## What remains valid from the recent work

`84c8526` added source-side behavior that should be retained:

* decoded HDR10+, static PQ, and Dolby metadata retention and validation;
* stable Dolby/HDR10 base-representation selection per playback generation;
* metadata provenance and conservative fallbacks;
* an explicit 1,000-nit fallback for metadata-less PQ on HDR-to-SDR;
* libplacebo-owned tone and gamut mapping with inverse mapping, peak detection,
  and dithering disabled by policy.

`5f5b737` expanded validated HDR10+ scene/static maxima, mapped-Dolby range
selection, and the explicit metadata-less-PQ fallback onto relative HDR
targets. That source-evidence improvement should remain, without its physical
target/output-normalization branch.

The same commit also corrected one real target-unit problem that must be retained:
PQ/Dolby mapped to a no-headroom SDR/WCG destination should use a nominal
100-nit target rather than treating libplacebo's 203-nit normalization constant
as the intended SDR display maximum. The resulting `203 / 100` uniform output
normalization converts coordinates; it does not cancel a live platform white
because 100 is fixed.

The invalid coupling was extending that mechanism to HDR with
`coordinateWhite = W` and treating automatic physical display telemetry as a
normal-playback viewing-intent switch.

## Test failure analysis

The regression was not blocked for five reasons:

1. The production HDR10+ integration test added by `5f5b737` explicitly
   requires `surface2 * W2 == surface1 * W1`. That is the `L / W` producer
   invariant which cancels Windows' final scale.
2. The real compositor test still requires producer samples to remain stable
   when `W` changes, but it uses the analytic diagnostic producer. The
   diagnostic path never enabled the decoded-only absolute mode.
3. No test crosses display state, presentation-target calculation, production
   decoded rendering, and final composition in one behavioral assertion.
4. The relevant GPU tests are excluded from hosted Windows CI, and no completed
   hosted Windows result was attached directly to `5f5b737`.
5. The shared FFmpeg capture helper defaults physical-target authority to true,
   so macOS GPU tests model the Windows-only state rather than production
   macOS EDR.

Physical display measurement was not necessary to catch this regression. A
fixed decoded PQ source that fits at two reference-white values, followed by
the known platform serializer, is enough to prove whether normal playback
tracks the reference-white anchor.

## Cross-platform findings

### Windows

Windows is the only current platform that supplies the combination which
activated the new branch: scene-referred presentation, known `W`, and known
physical `P`. Removing the `203 / W` HDR producer normalization restores the
product behavior while retaining Windows' required final `W / 80` conversion.

### macOS

AppKit supplies relative current and potential EDR headroom. The production
engine treats macOS as display-referred and uses final scale `1`. Production
macOS therefore never entered the new automatic physical-target branch, so the
Windows cancellation is not a macOS runtime regression. Some GPU test helpers
did synthesize that impossible authority and must be corrected. The runtime
correction must leave EDR `1.0 = current SDR white` intact.

Physical-nit reference monitoring cannot be inferred from the current AppKit
state and is outside this correction.

The existing 2026-08-19 color research identifies a separate bounded gamut
fact that does not require ICC parsing: `NSScreen.canRepresentDisplayGamut:`.
Publishing standard Display-P3 primaries only when AppKit reports P3 support,
and BT.709 when sRGB is representable, gives libplacebo a conservative usable
gamut for EDR. The extended-linear-sRGB layer and ColorSync final conversion
remain unchanged. Exact profile-derived native chromaticities and SDR-only
wide-gamut presentation remain outside this correction.

### Managed Wayland

The final shader encodes working `1.0` at PQ's default 203-nit source-reference
coordinate. This is intentional: color-management-v1 requires the compositor
to anchor different image-description reference whites to the same output
level. No application-side `W / 203` scale should be added.

One independent implementation defect is in scope because the preferred
description already exposes the necessary data: target primaries are parsed
but dropped before `RenderedVideoSurfaceDescription`, causing libplacebo to
fall back to BT.709 target primaries. The preferred target primaries should be
propagated without changing the PQ reference-white bridge.

The attached BT.2020/PQ description currently omits the narrower preferred
target volume used during application mapping. That is real metadata
imprecision, but visible double mapping is compositor-dependent and is not
proven by the present evidence. A post-change review confirmed the exact
residual: protocol defaults advertise a BT.2020/10,000-nit target volume after
libplacebo has already mapped to the preferred gamut and `203 * H` ceiling.
Correcting it is not a one-line propagation fix. The protocol's mastering-
display requests are optional capabilities, while SunPlayer currently creates
its stable HDR image description before the surface's asynchronous preferred
description arrives. A safe change therefore needs feature tracking,
preferred-description-driven image-description replacement, rejection
fallback, and native compositor evidence. That remains a separate bounded
Wayland presentation task; this correction does not claim end-to-end native
Wayland gamut or tone-map validation.

## Independent research cross-check

A separate ChatGPT source/code review supplied by the user independently
reached the same main conclusion and sharpened several supporting findings:

* Pinned libplacebo uses PQ's nominal 10,000-nit coding maximum when no better
  source maximum is available. The explicit, diagnosed 1,000-nit compatibility
  fallback from `84c8526` therefore addresses a separate real dark-output cause
  and must remain on SDR and HDR targets.
* Generic `PL_HDR_METADATA_ANY` can move between static HDR10, HDR10+ scene, and
  CIE-Y/Dolby evidence as progressively richer metadata is present. The
  metadata-first representation/family selection prevents base-layer guidance
  from being mixed with a mapped Dolby image.
* The 203-nit headroom-one destination was independently identified as the
  second HDR-to-SDR unit bug: an approximately 80-nit scene naturally stored as
  `80 / 203` and was then misread as 39% of desktop white. Nominal 100 nits plus
  fixed `203 / 100` normalization is the correct retained repair.
* `useAbsoluteTargetLuminance` mixed source evidence with viewing intent, and
  the diagnostic and decoded entry points consequently exercised different HDR
  modes. Removing that field and deriving the small target-coordinate result in
  the common render context closes the architectural split.
* Pinned libplacebo couples an HLG source OOTF to the HDR destination maximum.
  This remains a known model/measurement risk, not evidence for routing HLG
  through PQ's nominal-SDR or a new absolute-HDR branch.
* Hosted Windows CI excludes the real GPU compositor and FFmpeg first-frame
  executables. Splitting hardware-import coverage is possible future test
  infrastructure work, but is not required for this repair: the exact
  production-used `H = 1`/`H > 1` target calculation now has a hosted unit
  oracle, while the end-to-end decoded/compositor assertion remains a native
  GPU gate.

One proposed oracle in that review held physical peak constant by changing
`W = 100, H = 6` to `W = 200, H = 3` and expected the producer surface to stay
fixed. That still confounds white scaling with a legitimate tone-map-target
change: the working highlight ceiling is `H`. The accepted regression holds
`H`, primaries, source frame, and target minimum fixed while changing only `W`.
Reduced-headroom compression is asserted separately.

The review classified macOS gamut as an unresolved authority gap. Existing
project research provides a narrower answer: AppKit's P3/sRGB representability
query is authoritative for a conservative lower bound even though it is not
exact native xy. That bounded path is included; ICC-profile reduction remains
deferred.

## Chosen correction boundary

The smallest coherent correction is:

1. Remove physical-HDR target authority from the rendered-video surface and
   color-policy decision. It is not needed for normal playback.
2. After libplacebo source-color inference, use nominal-100 target construction
   only for PQ/Dolby sources rendered to the sole no-headroom value `H = 1`.
3. Use the existing `203 * H` relative target whenever `H > 1`, on every
   platform.
4. Allow HDR10+ source OOTF only where its physical target units are coherent;
   on relative HDR targets use validated scene/static evidence instead.
5. Make analytic and decoded render entry points use the same target-coordinate
   rule so HDR Lab cannot silently validate a different luminance contract.
6. Propagate managed-Wayland target primaries already supplied by the
   compositor.
7. Publish the existing research's conservative AppKit P3/BT.709 target-gamut
   lower bound and refresh it when `NSScreen` color space changes.

No second tone mapper, post-map viewing curve, exposure compensation, or new
platform renderer is required.

## Required evidence

The correction is complete only when tests demonstrate:

* a fixed production-decoded PQ source rendered at fixed `H`, primaries, and
  zero/unknown target minimum produces stable surface samples when only `W`
  changes;
* the same production test passes those surfaces through linear final
  composition and proves `composed = surface * W / 80` at both whites;
* a reduced-headroom target still compresses highlights into the declared
  ceiling;
* a separate nonzero-target-minimum case preserves the relative minimum
  conversion rather than conflating shadow adaptation with the white-level
  regression;
* nominal-100 HDR-to-SDR behavior remains numerically covered;
* macOS remains display-referred with final scale `1` and never requires
  physical-nit authority; AppKit-confirmed P3/BT.709 propagation does not add a
  display ICC transform;
* managed Wayland keeps the PQ-203 source-reference bridge and propagates
  preferred target primaries;
* policy tests assert typed decisions or pixel behavior rather than treating a
  human diagnostic sentence as the primary correctness oracle.

## Primary references

* [Microsoft Advanced Color and scRGB guidance](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
* [Apple EDR tone-mapping guidance](https://developer.apple.com/documentation/metal/performing-your-own-tone-mapping)
* [Apple `NSScreen.canRepresent(_:)`](https://developer.apple.com/documentation/appkit/nsscreen/canrepresent%28_%3A%29)
* [Apple `NSScreen.colorSpaceDidChangeNotification`](https://developer.apple.com/documentation/appkit/nsscreen/colorspacedidchangenotification)
* [Wayland color-management-v1](https://wayland.app/protocols/color-management-v1)
* [Pinned libplacebo 7.360.1 color-space definitions](https://code.videolan.org/videolan/libplacebo/-/blob/v7.360.1/src/include/libplacebo/colorspace.h)
