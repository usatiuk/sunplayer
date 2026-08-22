# 0008: Anchor normal HDR playback to the platform reference white

* Status: Accepted
* Date: 2026-07-30
* Scope amendments: 2026-08-01, 2026-08-15, 2026-08-22
* Amended by:
  [0025: Keep normal HDR reference-white adaptive](0025-keep-normal-hdr-reference-white-adaptive.md)

## Context

SunPlayer composes video, subtitles, and UI in one linear surface where `1.0`
means the active platform SDR/reference-white luminance. On Windows the final
FP16 scene-referred scRGB conversion for an HDR Advanced Color output
multiplies that composed value by `referenceWhiteNits / 80`. SDR Advanced
Color/WCG FP16 is display-referred and uses working white `1.0` directly.

Libplacebo's linear convention is fixed at `1.0 = 203 nits`. Before this
decision, the adapter rendered against the display's physical peak and then
multiplied video by `203 / referenceWhiteNits`. On Windows that factor canceled
the final presentation scale for PQ video. HDR brightness therefore remained
effectively absolute while SDR UI followed the system SDR-white control.

ADR 0024 briefly restored that cancellation for decoded PQ/Dolby under the
name of absolute-target correction. ADR 0025 supersedes that normal-HDR branch:
its arithmetic described reference monitoring, not this application's accepted
reference-white-adaptive playback intent. Display peak still bounds available
headroom, and inverse tone mapping remains disabled so content is not expanded
merely to consume it.

Libplacebo has no independent destination-reference-white field. Its target
minimum and maximum luminance fields control tone/gamut mapping, and its linear
output remains in the fixed 203-nit coordinate system.

## Decision

Normal playback uses one shared reference-white-relative surface:

* Relative SDR video white and ordinary SDR UI white map to composition value
  `1.0`; subtitle RGB is scaled to `0.8` in linear light without changing
  authored alpha.
* Composition value `1.0` means the active platform SDR/reference-white
  luminance.
* Available highlight headroom is
  `displayPeakNits / referenceWhiteNits`.
* Retained decoded source values and metadata remain unchanged. The render
  adapter preserves source gamut information but removes absolute and dynamic
  luminance candidates from the effective metadata of relative SDR transfers,
  then normalizes their range to libplacebo's 203-nit coordinate anchor.
  PQ and Dolby metadata remain source-authored physical cd/m²; HLG remains
  relative and display-dependent. Source units do not change the normal
  playback surface anchor.
* Libplacebo owns source transfer interpretation, tone mapping, and gamut
  mapping.
* Inverse tone mapping is disabled: a source that fits is not stretched to the
  display peak.
* The final compositor performs no second video tone map. A producer-side
  uniform scale may convert libplacebo's output coordinate unit, but it cannot
  change image intent.

Every normal HDR target, plus relative SDR and HLG, uses the display-relative
destination expressed through libplacebo's existing 203-nit coordinate system:

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

For PQ and mapped Dolby at headroom one, ADR 0025 retains a nominal 100-nit SDR
destination. The producer converts libplacebo's fixed output coordinate only
by the fixed `203 / 100` factor:

```text
surfaceRgb = mappedOutputNits / 100
surface maximum = 1
```

This uniform post-gamut scale is a coordinate conversion, not a viewing gamma,
exposure change, or second tone map. It is not installed at HDR headroom and
there is no producer factor involving live reference white.

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

An optional absolute/reference-monitoring mode may later preserve physical PQ
luminance independently of desktop SDR white, but it must be explicit and must
not be confused with normal viewing adaptation or target-unit conversion.

## Consequences

Benefits:

* SDR, HDR video, subtitles, and UI follow the live platform reference-white
  anchor during normal playback.
* Display peak and reference white jointly define surface highlight headroom;
  a smaller headroom can still cause libplacebo to compress highlights.
* With inverse tone mapping disabled, the renderer does not deliberately
  expand a lower-peak source merely to consume target headroom. Gamut mapping,
  black-point handling, and the selected tone curve can still modify pixels.
* The retained source signal and its metadata are never rewritten to encode
  display state. A render-local copy discards absolute mastering-luminance and
  stale dynamic-luminance interpretation for relative SDR transfers so encoded
  SDR white remains surface `1.0`; source mastering primaries remain available
  for gamut policy, and the normalization does not depend on the active
  display.
* ADR 0025 removes the decoded-only physical-HDR exception. Transfer semantics
  select only the fixed nominal-100 no-headroom case; every HDR target uses the
  shared 203-nit coordinate and no live-white producer normalization.
* The shared video-surface and final-compositor contracts remain unchanged.

Costs and limitations:

* HDR destination luminance numbers are reference-white-relative coordinates
  derived from libplacebo's fixed 203-nit normalization, not a claim of
  measured physical reference monitoring.
* Windows Advanced Color supplies validated target display primaries separately
  from the BT.709/scRGB coordinate basis. macOS supplies a conservative
  AppKit-confirmed P3 or BT.709 target for EDR, and managed Wayland supplies its
  preferred target primaries with a primary-volume fallback.
* The virtual target is not assumed valid as a universal HDR construction. In
  libplacebo 7.360.1 the HDR destination `max_luma` becomes the HLG source's
  physical target peak for OOTF inference; a virtual maximum can therefore
  change HLG contrast using the wrong physical peak. HLG remains playable
  through the common renderer, but its color-correctness claim is blocked on a
  controlled target-response experiment and, if the mismatch is material, a
  libplacebo integration that separates physical OOTF peak from output
  normalization.
* HDR10+'s source-provided targeted-system-display luminance remains unchanged.
  Its source OOTF is selected for nominal SDR only; reference-white-adaptive HDR
  retains scene guidance with spline rather than inventing a physical target.
* The pinned Dolby Vision helper supports selected reshaping metadata but not
  target-specific trims or enhancement-layer residuals. Support claims must be
  limited to verified reshape/profile or compatible base-layer fallback paths.
* Dynamic peak detection remains disabled pending quality and performance
  validation.
* Software capture cannot prove emitted luminance; physical HDR validation
  remains required.

## Alternatives considered

### Force PQ source metadata to use the desktop SDR-white level

Rejected. Source pixels and metadata remain source truth. The destination is
reference-white adaptive without rewriting authored luminance values.

### Scale source PQ or HLG values before libplacebo

Rejected. Pixels and all related static/dynamic metadata would have to be
rewritten consistently, and HLG's target-dependent rendering would become more
fragile.

### Tone-map normal playback to physical nits and add a viewing curve afterward

Rejected. A second gamma, exposure multiplier, or highlight compressor after
libplacebo would change image intent and could decouple luminance from gamut
mapping. The only retained producer scalar is the fixed nominal-SDR
`203 / 100` coordinate conversion.

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
