# 0029: Configure HDR source reference white

* Status: Accepted
* Date: 2026-09-20
* Amends the fixed PQ/Dolby coordinate choices in ADRs 0025, 0027, and 0028.

## Decision

Decoded PQ, HDR10+, and Dolby Vision playback uses a persistent source
reference-white setting R, from 100 to 203 nits, default 100. Playback Settings
provides a slider, numeric field, and 100/203-nit presets. This is an explicit
presentation convention, not a measurement of the display or inferred source
metadata. It applies on SDR and HDR outputs on every supported platform.

For headroom H, libplacebo maps into a destination maximum R*H. Its linear
output uses nits/203, so the existing producer normalization hook multiplies
by 203/R. The resulting surface still has 1.0 at platform white and peak H.
Known physical black uses the same target-coordinate conversion. Source
pixels and mastering/dynamic metadata are unchanged; existing library tone
and gamut mapping fit highlights before normalization. No post-composition
exposure multiplier or new tone curve is added.

At R=100 the existing PQ/Dolby nominal SDR conversion is retained. At R=203
the existing adaptive-HDR convention is retained. In uncompressed PQ regions,
working output is approximately source nits/R; lowering R brightens those
regions. Authored tone curves and compression still determine the complete
rendered response. The source setting does not cancel platform brightness.
In particular, an authored HDR10+ curve need not brighten monotonically as R
decreases: pinned libplacebo adapts its knee and control points to the new
destination. The UI does not promise a uniform exposure gain, and tests compare
retained and fresh rendering instead of overriding that authored response.

SDR input, HLG, and HDR Lab retain their previous behavior. HLG is scene-relative
and its library OOTF depends on destination peak; changing it would be a separate
appearance policy. UI/subtitle brightness, surface meaning, output encoding,
and Wayland composition declarations remain unchanged.

`DecodedVideoSource` owns the live value and canonical bounds/default. Its
existing content revision and update signal invalidate paused output without
recreating the producer or remapping an unchanged imported frame. Application
settings validate and persist the value; no parallel reload mechanism is added.

## Evidence and limits

Unit tests cover coordinate math, headroom continuity, known black, persistence,
and UI edits. Real decoded GPU captures exercise preset changes on a retained
frame, highlight bounds, unchanged SDR/HLG, and the uncompressed PQ ratio.
Native macOS/Wayland execution and physical display luminance remain separate
validation requirements.

The [SMPTE discussion](https://github.com/SMPTE/st2094-50/issues/85#issuecomment-4305114922)
clarifies that ST2094-50's 203-nit encoding convention is not a universal default
requirement for legacy content without that metadata. This setting is SunPlayer
presentation policy, not a claim of ST2094-50 implementation or reference-monitor
equivalence. No movie-specific workaround or dependency patch is introduced.
