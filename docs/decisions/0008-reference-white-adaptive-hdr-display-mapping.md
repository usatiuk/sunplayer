# 0008: Anchor normal HDR playback to the platform reference white

* Status: Accepted
* Date: 2026-07-30
* Scope amendments: 2026-08-01, 2026-08-15, 2026-08-22
* Amended by:
  [0024: Map PQ against absolute target luminance](0024-map-pq-against-absolute-target-luminance.md)

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
effectively absolute while SDR UI followed the system SDR-white control. This
decision initially removed that distinction. The 2026-08-22 evidence recorded
in ADR 0024 showed that making absolute PQ target luminance virtual instead was
dimensionally inconsistent with libplacebo's physical source metadata, so this
decision is now narrowed to distinguish relative SDR/HLG from absolute PQ.

Normal desktop playback needs one surface coordinate for video, UI, and
subtitles, but that does not make absolute PQ luminance relative. Display peak
remains a hard output capability, and content that fits must not be expanded
merely to consume it.

Libplacebo has no independent destination-reference-white field. Its target
minimum and maximum luminance fields control tone/gamut mapping, and its linear
output remains in the fixed 203-nit coordinate system.

## Decision

Normal playback uses one shared reference-white-relative surface with two
source-appropriate target-luminance contracts:

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
  Absolute PQ and Dolby metadata remain in physical cd/m²; HLG remains relative
  and display-dependent.
* Libplacebo owns source transfer interpretation, tone mapping, and gamut
  mapping.
* Inverse tone mapping is disabled: a source that fits is not stretched to the
  display peak.
* The final compositor performs no second video tone map. A producer-side
  uniform scale may convert libplacebo's output coordinate unit, but it cannot
  change image intent.

Relative SDR and HLG retain the display-relative destination expressed through
libplacebo's existing 203-nit coordinate system:

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

For absolute PQ and mapped Dolby, ADR 0024 supplies nominal 100-nit SDR or the
physical HDR peak from an automatic scene-referred target with known luminance.
The producer then converts only
libplacebo's fixed `nits / 203` output unit into `nits / targetWhite`,
preserving the same surface contract:

```text
surfaceRgb = mappedOutputNits / targetWhiteNits
surface maximum = physical display peak / reference white
```

This uniform post-gamut scale is a coordinate conversion, not a viewing gamma,
exposure change, or second tone map. Relative SDR-source and HLG-source paths
do not receive it.
When an HDR platform exposes only relative component headroom, uses a
display-referred contract, or has manual headroom selected, PQ/Dolby retains
the relative target and diagnoses that physical target luminance is unavailable.

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

An optional viewing-adaptation mode may later move absolute PQ reference white
toward the desktop SDR-white level, but it must be explicit and must not be
confused with target-unit conversion.

## Consequences

Benefits:

* Relative SDR/UI follow the live platform reference-white anchor while
  absolute PQ remains physically meaningful on HDR output.
* Display peak and reference white jointly define surface highlight headroom;
  libplacebo receives the real peak for absolute PQ when the luminance range is
  authoritative.
* With inverse tone mapping disabled, the renderer does not deliberately
  expand a lower-peak source merely to consume target headroom. Gamut mapping,
  black-point handling, and the selected tone curve can still modify pixels.
* The retained source signal and its metadata are never rewritten to encode
  display state. A render-local copy discards absolute mastering-luminance and
  stale dynamic-luminance interpretation for relative SDR transfers so encoded
  SDR white remains surface `1.0`; source mastering primaries remain available
  for gamut policy, and the normalization does not depend on the active
  display.
* ADR 0024 deliberately restores the old adapter's output-unit algebra only
  for absolute PQ and mapped Dolby. Unlike the former universal policy, target
  mapping is selected by transfer semantics: libplacebo receives physical
  target luminance before its tone/gamut map, and the post-map scalar only
  converts units without altering the selected curve. Relative SDR-source and
  HLG-source paths do not receive it.
* The shared video-surface and final-compositor contracts remain unchanged.

Costs and limitations:

* Relative SDR/HLG destination luminance numbers remain virtual coordinates
  derived from libplacebo's fixed 203-nit normalization. Absolute PQ uses
  literal target cd/m² when known; headroom-only HDR targets retain the
  diagnosed relative fallback.
* Windows Advanced Color supplies actual target display primaries separately
  from the BT.709/scRGB coordinate basis. Unknown targets still fall back to a
  BT.709 gamut; macOS and Wayland target-gamut population remain open.
* The virtual target is not assumed valid as a universal HDR construction. In
  libplacebo 7.360.1 the HDR destination `max_luma` becomes the HLG source's
  physical target peak for OOTF inference; a virtual maximum can therefore
  change HLG contrast using the wrong physical peak. HLG remains playable
  through the common renderer, but its color-correctness claim is blocked on a
  controlled target-response experiment and, if the mismatch is material, a
  libplacebo integration that separates physical OOTF peak from output
  normalization.
* HDR10+'s source-provided targeted-system-display luminance remains unchanged
  while an authoritative physical display peak is supplied separately through
  an eligible destination.
* The pinned Dolby Vision helper supports selected reshaping metadata but not
  target-specific trims or enhancement-layer residuals. Support claims must be
  limited to verified reshape/profile or compatible base-layer fallback paths.
* Dynamic peak detection remains disabled pending quality and performance
  validation.
* Software capture cannot prove emitted luminance; physical HDR validation
  remains required.

## Alternatives considered

### Force PQ reference white to the desktop SDR-white level

Rejected for the absolute HDR path. It would require another viewing-intent
transform and would make source and ST 2094-40 target luminance cease to be
physical cd/m². Platform SDR-white still controls SDR UI and the coordinate
used to store the mapped HDR result.

### Scale source PQ or HLG values before libplacebo

Rejected. Pixels and all related static/dynamic metadata would have to be
rewritten consistently, and HLG's target-dependent rendering would become more
fragile.

### Tone-map to physical nits and add a viewing curve afterward

Rejected. A second gamma, exposure multiplier, or highlight compressor after
libplacebo would change image intent and could decouple luminance from gamut
mapping. ADR 0024's uniform scalar is distinct: it converts `nits / 203` to the
surface coordinate after the selected map without reshaping the result.

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
