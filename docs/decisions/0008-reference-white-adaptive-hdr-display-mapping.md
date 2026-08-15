# 0008: Anchor normal HDR playback to the platform reference white

* Status: Accepted
* Date: 2026-07-30
* Scope amendments: 2026-08-01, 2026-08-15

## Context

SunPlayer composes video, subtitles, and UI in one linear surface where `1.0`
means the active platform SDR/reference-white luminance. On Windows the final
FP16 scene-referred scRGB conversion for an HDR Advanced Color output
multiplies that composed value by `referenceWhiteNits / 80`. SDR Advanced
Color/WCG FP16 is display-referred and uses working white `1.0` directly.

Libplacebo's linear convention is fixed at `1.0 = 203 nits`. The previous
adapter rendered against the display's physical peak and then multiplied video
by `203 / referenceWhiteNits`. On Windows that factor canceled the final
presentation scale for PQ video. HDR brightness therefore remained effectively
absolute while SDR UI followed the system SDR-white control.

Normal desktop playback must adapt HDR reference white to the current platform
reference-white level. Display peak remains a hard output capability, and
content that fits must not be expanded merely to consume it.

Libplacebo has no independent destination-reference-white field. Its target
minimum and maximum luminance fields control tone/gamut mapping, and its linear
output remains in the fixed 203-nit coordinate system.

## Decision

Normal playback uses one shared, reference-white-anchored display-mapping
policy:

* SDR video white, HDR reference white, and ordinary SDR UI white map to
  composition value `1.0`; subtitle RGB is scaled to `0.8` in linear light
  without changing authored alpha.
* Composition value `1.0` means the active platform SDR/reference-white
  luminance.
* Available highlight headroom is
  `displayPeakNits / referenceWhiteNits`.
* Retained decoded source values and metadata remain unchanged. The render
  adapter preserves source gamut information but removes absolute and dynamic
  luminance candidates from the effective metadata of relative SDR transfers,
  then normalizes their range to libplacebo's 203-nit coordinate anchor. PQ
  absolute-luminance metadata and HLG or dynamic-HDR source metadata remain
  unchanged; HLG itself remains relative and display-dependent.
* Libplacebo owns source transfer interpretation, tone mapping, and gamut
  mapping.
* Inverse tone mapping is disabled: a source that fits is not stretched to the
  display peak.
* The final compositor performs no second video tone map or video-only
  brightness scale.

For relative SDR and static PQ, SunPlayer currently expresses the desired
display-relative destination through libplacebo's existing 203-nit coordinate
system:

```text
targetMaxLuma =
    PL_COLOR_SDR_WHITE * targetPeakHeadroom

targetMinLuma =
    max(
        PL_COLOR_HDR_BLACK,
        PL_COLOR_SDR_WHITE
            * physicalTargetMinLuma
            / referenceWhiteNits)
```

Unknown or known-zero target minimum uses `PL_COLOR_HDR_BLACK` at the
libplacebo boundary because zero means unknown to libplacebo. Shared state
continues to preserve the platform value and its known state.

No pre-output normalization hook is used. For the capture-validated analytic
SDR/static-PQ cases, libplacebo's resulting linear numbers satisfy SunPlayer's
surface contract:

```text
1.0 = active platform reference white
targetPeakHeadroom = physical display peak / reference white
```

Platform presentation converts the complete composed surface into native
output coordinates exactly once. For a Windows HDR Advanced Color output, the
conversion is:

```text
scRGB = composedLinear * referenceWhiteNits / 80
```

For Windows SDR Advanced Color/WCG, working white maps to display-referred
scRGB `1.0`; ordinary SDR with Advanced Color inactive uses the unmanaged sRGB
fallback.

The renderer constructs this destination without a separate SunPlayer path for
each source transfer. HLG, HDR10+, and Dolby Vision are accepted through the
same FFmpeg/libplacebo route; diagnostics identify the observed reshape,
dynamic-metadata, or base-layer result. Representative acceptance tests scope
our color-correctness claims without duplicating library parsers or mapping
implementations.

The policy and composition contract are platform-independent. macOS and
Wayland Linux adapters must preserve them while using their native
EDR/color-management representations.

An optional reference-monitoring mode may later preserve nominal PQ luminance,
but it must be explicit and must not change normal playback defaults.

## Consequences

Benefits:

* HDR video follows the same live platform reference-white anchor as SDR video,
  UI, and future subtitles.
* Display peak and reference white jointly define the actual highlight
  headroom seen by libplacebo.
* With inverse tone mapping disabled, the renderer does not deliberately
  expand a lower-peak source merely to consume target headroom. Gamut mapping,
  black-point handling, and the selected tone curve can still modify pixels.
* The retained source signal and its metadata are never rewritten to encode
  display state. A render-local copy discards absolute mastering-luminance and
  stale dynamic-luminance interpretation for relative SDR transfers so encoded
  SDR white remains surface `1.0`; source mastering primaries remain available
  for gamut policy, and the normalization does not depend on the active
  display.
* The custom reference-white shader and its cancellation are removed.
* The shared video-surface and final-compositor contracts remain unchanged.

Costs and limitations:

* The destination luminance numbers are virtual coordinates derived from
  libplacebo's fixed 203-nit normalization, not literal physical target nits.
  This adapter behavior must remain documented and capture-tested.
* SunPlayer does not yet supply actual target display primaries, so the current
  libplacebo target gamut is inferred as BT.709.
* The virtual target is not assumed valid as a universal HDR construction. In
  libplacebo 7.360.1 the HDR destination `max_luma` becomes the HLG source's
  physical target peak for OOTF inference; a virtual maximum can therefore
  change HLG contrast using the wrong physical peak. HLG remains playable
  through the common renderer, but its color-correctness claim is blocked on a
  controlled target-response experiment and, if the mismatch is material, a
  libplacebo integration that separates physical OOTF peak from output
  normalization.
* HDR10+'s source-authored targeted-system-display luminance must remain
  unchanged while the current display peak is supplied separately through the
  destination; it must not inherit the static-PQ construction by default.
* The pinned Dolby Vision helper supports selected reshaping metadata but not
  target-specific trims or enhancement-layer residuals. Support claims must be
  limited to verified reshape/profile or compatible base-layer fallback paths.
* Dynamic peak detection remains disabled pending quality and performance
  validation.
* Software capture cannot prove emitted luminance; physical HDR validation
  remains required.

## Alternatives considered

### Preserve nominal PQ luminance in normal playback

Rejected. It prevents the platform reference-white setting from adapting HDR
video to the viewing environment and makes video brightness diverge from SDR
UI and subtitles.

### Scale source PQ or HLG values before libplacebo

Rejected. Pixels and all related static/dynamic metadata would have to be
rewritten consistently, and HLG's target-dependent rendering would become more
fragile.

### Tone-map to physical nits and repair the result afterward

Rejected. A second global multiplier or highlight compressor after libplacebo
breaks the single display-mapping stage and can decouple luminance from gamut
mapping.

### Implement a SunPlayer tone mapper

Rejected. Libplacebo already owns the required mapping algorithms and metadata
handling. SunPlayer should describe the destination correctly rather than
duplicate them.

### Patch or fork libplacebo immediately

Not selected for static PQ. The existing API can express and capture-prove the
current SDR/static-PQ policy. Source inspection identifies a plausible HLG
integration limitation, but the production-boundary experiment comes before a
patch decision. If the mismatch is material and no supported configuration
solves it, an upstream destination-reference-white/physical-peak separation is
preferable to a narrowly maintained dependency patch. A project-local
tone-mapping fork remains rejected.
