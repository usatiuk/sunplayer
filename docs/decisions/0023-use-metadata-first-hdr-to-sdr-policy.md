# 0023: Use a metadata-first HDR-to-SDR policy

* Status: Accepted
* Date: 2026-08-21
* Implementation status: Implemented for PQ input targeting SDR/WCG
* Amends:
  [0012: Use final decoded frames as source-color truth](0012-use-final-decoded-frames-as-color-evidence.md)

## Context

The retained FFmpeg frame and libplacebo remain the source-color and rendering
authorities, but libplacebo 7.360.1's default spline/`ANY` combination is not a
complete product policy. It does not consume an HDR10+ Bezier OOTF, can mix its
metadata choice across coexisting Dolby and HDR10+ families, and treats PQ
without usable luminance metadata as a possible 10,000-nit signal. The latter
can make ordinary HDR material impractically dark on an SDR desktop.

SunPlayer needs to choose the library operation and coherent metadata family.
It does not need another curve implementation, image-analysis pipeline, or
platform-specific color policy.

## Decision

The shared decoded-video producer resolves one metadata-first policy before
rendering PQ into an SDR/WCG target. It inspects the retained frame before
libplacebo inference, validates mapped metadata, and dispatches existing
libplacebo operators:

* A supported application-version 0 or 1, one-window HDR10+ OOTF uses
  `st2094-40`. Version-specific anchor limits and a conservative
  nondecreasing-control-point predicate protect the pinned implementation.
* Other usable HDR10+ scene metadata uses libplacebo's generalized BT.2446A
  EETF.
* Static HDR10 uses valid MaxCLL, then mastering maximum, with the same EETF.
* A mapped Dolby representation uses only Dolby L1/CIE-Y or its mapped source
  range. Base-layer HDR10 metadata is not mixed into that representation.
* PQ without a usable maximum uses an explicit, diagnosed 1,000-nit
  compatibility fallback instead of the implicit 10,000-nit transfer limit.

A proven Profile 8.1 HDR10-compatible base with a supported HDR10+ OOTF may be
rendered as that base on SDR/WCG so the source OOTF can be used coherently. The
one-bit choice is stable for the playback generation and is part of the
imported-frame cache identity. HDR targets return to the existing mapped-Dolby
path. Unknown compatibility stays on the Dolby representation.

SDR, HLG, and HDR-target behavior remain unchanged. Peak detection, inverse
mapping, and dithering remain disabled. Perceptual gamut mapping remains in
libplacebo. Platform adapters supply target facts only; this policy is shared
by Windows, macOS, and Linux.

The retained `AVFrame` remains authoritative. The only added stream fact is a
validated optional boolean describing whether a version-1 Dolby configuration
proves the narrow Profile 8.1 HDR10-compatible base required by the
representation choice. Unknown Dolby configuration versions remain unknown.
Effective maximum overrides exist only on a render-local copy.

## Consequences

Benefits:

* HDR10+ target guidance is used when the pinned library can represent it.
* Metadata precedence is explicit, coherent, deterministic, and visible in
  existing diagnostics.
* Metadata-less PQ receives an explicit, diagnosed 1,000-nit compatibility
  mapping instead of implicit 10,000-nit inference, without temporal image
  analysis or hidden state.
* Display moves can remap a paused dual-format frame without introducing a
  second decoder or renderer.
* Future platform display providers can reuse the policy unchanged.

Costs and limitations:

* The 1,000-nit fallback is a diagnosed compatibility assumption, not authored
  metadata, and can compress content above that level.
* Pinned libplacebo cannot fully apply zero-anchor or local-window HDR10+
  OOTFs; SunPlayer diagnoses them and uses coherent scene/static fallback. It
  also conservatively rejects a mapped non-monotonic anchor sequence; this is
  a product representability check, not a claim that every descending control
  sequence is forbidden by ST 2094-40.
* Open libplacebo Dolby support applies reshape and L1 guidance, not licensed
  Dolby L2/L8 target trims or certified display management.
* Libplacebo's BT.2446A operator is a generalized EETF combined with its own
  chroma/gamut pipeline, not a claim of full BT.2446 Method A conformance for
  every source and target range.
* Physical exact-frame comparison remains required before making broader
  perceptual or Dolby-certification claims.

## Alternatives considered

### Increase exposure or SDR white

Rejected. It obscures metadata errors, changes UI/SDR meaning, and can clip
highlights.

### Enable dynamic peak detection

Deferred. It can help unreliable metadata but adds image analysis, temporal
state, scene-cut handling, and possible pumping. This policy deliberately uses
metadata and a deterministic fallback first.

### Keep the generic spline for every format

Rejected for PQ-to-SDR. It ignores a representable HDR10+ OOTF and produced
unusable field behavior for metadata-less PQ.

### Implement Dolby trims or custom tone curves

Rejected. Those require proprietary display-management semantics or new color
science and would duplicate the selected library boundary.

### Add a user setting

Rejected for this change. Metadata interpretation and a safe missing-data
fallback are baseline correctness policy, not a preference.
