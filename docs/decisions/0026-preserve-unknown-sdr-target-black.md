# 0026: Preserve unknown SDR target black at the libplacebo boundary

* Status: Accepted
* Date: 2026-08-26
* Amends:
  [0003: Normalize rendered video to one display-targeted linear surface](0003-display-targeted-video-surface.md),
  [0008: Anchor normal HDR playback to the platform reference white](0008-reference-white-adaptive-hdr-display-mapping.md),
  and
  [0025: Keep normal HDR reference-white adaptive](0025-keep-normal-hdr-reference-white-adaptive.md)

## Context

SunPlayer preserves target minimum luminance as a known flag plus a value.
The libplacebo adapter previously translated both an unknown minimum and a
known physical zero to `PL_COLOR_HDR_BLACK`.

Pinned libplacebo 7.360.1 assigns different meanings to those inputs. Numeric
`min_luma == 0` is unknown; a non-HDR target then defaults to 1000:1 contrast.
`PL_COLOR_HDR_BLACK` is a small positive sentinel for a known effectively-zero
black point. On the nominal 100-nit HDR-to-SDR target, replacing unknown with
the sentinel changes the inferred target minimum from 0.1 nit to approximately
0.000001 nit. BT.2446A consumes that minimum and materially suppresses PQ
near-black output.

The target passed to libplacebo is linear even at extended headroom. Sending
numeric zero for an unknown HDR/EDR minimum would therefore also invoke the
generic `targetPeak / 1000` fallback there, changing an unreported path while
repairing SDR.

## Decision

Keep the shared known/value state and translate it at the existing renderer
boundary:

```text
unknown and target headroom <= 1 -> 0
unknown and target headroom > 1  -> PL_COLOR_HDR_BLACK
known zero                       -> PL_COLOR_HDR_BLACK
known positive                   -> existing physical-range conversion
```

Unknown HDR/EDR and known zero intentionally converge only at the libplacebo
boundary. Preserving the HDR/EDR result is conservative behavior pending
authoritative target-black data; it is not a claim of physically perfect black.

This decision does not change target maximum, nominal 100-nit SDR
normalization, BT.2446A/spline selection, Windows HDR `203 * H`, the final
Windows `W / 80` composition scale, macOS EDR policy, or managed Wayland PQ203.

## Consequences

* Windows SDR with no authoritative minimum uses libplacebo's documented
  1000:1 inference and preserves substantially more PQ shadow separation.
* A measured physical zero remains distinguishable in shared state and keeps
  its effectively-zero libplacebo representation.
* Tests cover the translation table, pinned nominal-luminance inference,
  near-black BT.2446A numbers, and an in-memory high-bit-depth PQ frame through
  the production decoded producer with explicit unknown minimum.
* macOS 100-versus-203 reference policy and BT.2446A-versus-spline appearance
  remain separate native experiments.
