# 0027: Separate rendering intent from available headroom

* Status: Accepted
* Date: 2026-09-20
* Amends ADRs 0021, 0023, 0025, and 0026.

## Decision

The rendered-video surface carries one explicit `VideoRenderingMode`.
SDR compatibility retains nominal 100-nit PQ/Dolby mapping and fixed 203/100
normalization. Adaptive HDR always uses 203 times headroom, including exactly
one, with no producer scale involving live reference white. Mode participates
in existing surface equality and paused-frame reuse.

The presentation contract selects this from the actual HDR presentation path
and platform semantics, never current headroom. Windows WCG remains SDR
compatibility. macOS EDR with current headroom one remains adaptive when the
screen retains HDR potential. Stable managed Wayland PQ remains adaptive on
SDR outputs. Actual SDR presentation fallback selects SDR compatibility.

Unknown target black uses SDR inference only in SDR compatibility; adaptive
HDR uses the existing HDR-black sentinel at every headroom. Known physical
zero and positive minima retain their prior conversions.

Ordinary PQ uses BT.2446A in SDR compatibility and spline in adaptive HDR,
regardless of whether the effective maximum came from metadata or the 1000-nit
fallback. Authored supported HDR10+ OOTFs remain an SDR exception. Source
representation selection follows mode, not headroom. Metadata provenance and
representation coherence remain intact.

Pinned libplacebo overwrites HLG source maximum with destination maximum only
when the destination is classified as HDR (>203 nits for linear output).
Explicitly set the render-local adaptive HLG maximum to 203H at every H,
extending its existing virtual-peak model continuously to one. This neither
adds an OOTF nor claims physically calibrated HLG reference appearance.

## Wayland composition declarations

The encoding stays BT.2020/PQ with reference white 203. Managed HDR additionally
requires the mastering-display-primaries feature (which also gates mastering
luminance). Declare composition minimum zero, not the video mapper's black.
For ordinary D65 target gamuts containing sRGB and contained in BT.2020, declare
the target gamut and ceil(203H) maximum. This covers video, UI, and subtitles.
For same-D65 gamuts not contained in BT.2020 (including P3), declare BT.2020
with a conservative encoded peak from the positive row sums of libplacebo's
RGB conversion matrix. Cube-corner tests verify the target RGB cube bound; UI lies within the union.
For different white points, retain the full BT.2020/10000-nit transport volume.
A single target triangle cannot safely describe every gamut union.
HDR Lab permits mapping bypass and therefore declares the transport peak.

One latest pending immutable native description is prepared asynchronously.
Readiness requests a window update; it does not mutate surface state. Complete
layer texture rebinding before postponing presentation, so resize invalidation
is not lost. Apply the ready description immediately before the matching WSI
present, using the existing double-buffered surface protocol. Rejected target
descriptions use the existing device-generation-bounded managed SDR fallback.
Missing mastering capability selects managed SDR at startup.

## Evidence and limits

The controlled 1000-to-100-nit scalar comparison (target black 0.1) gives
BT.2446A/spline outputs of 15.1083/25.5889 nits for a 50-nit input and
43.1382/53.9764 for 203 nits. Both preserve increasing neutral values. This
confirms appearance differences, not universal superiority; retain the
established SDR policy instead of changing it to whichever is brighter.

Production decoded PQ, HLG, HDR10+, and Dolby fixtures sweep H=1,1.0001,1.001,
1.01,1.1 with known-zero and unknown target black and final composition capture.
A retained dual-format frame verifies representation stability across adaptive
headroom changes and remapping only at explicit SDR/adaptive mode transitions.

After reviewing upstream spline/peak-detection guidance, the user explicitly
chose to retain this SDR baseline for now. See
[the mapper comparison](../research/2026-09-20-sdr-mapper-comparison.md).

No peak detection, comfort limit, dithering, ICC rendering, or exposure control
is added. Native display transitions and emitted luminance remain hardware
validation tasks. Accurate Wayland declarations do not guarantee compositor
pass-through or eliminate every possible perceptual adjustment.

## Sources

- [Pinned libplacebo inference](https://github.com/haasn/libplacebo/blob/cee9b076f2c63104ccfd497fa79c39a867293ec4/src/colorspace.c)
- [Wayland color-management protocol](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/color-management/color-management-v1.xml)
