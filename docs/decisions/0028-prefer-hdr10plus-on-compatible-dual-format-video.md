# 0028: Prefer HDR10+ on compatible dual-format video

* Status: Accepted
* Date: 2026-09-20
* Amends the HDR10+ and representation-selection parts of ADRs 0023, 0025 and 0027.
* Amended by [ADR 0029](0029-configure-hdr-source-reference-white.md) for the
  destination reference white; format selection and source metadata stay unchanged.

## Decision

One persistent preference, disabled by default, prefers HDR10+ over Dolby Vision.
It applies only when Dolby metadata and recognized HDR10+ metadata coexist
on a proven HDR10-compatible base. A supported authored curve is not required
for format selection. Unknown/incompatible bases retain Dolby reconstruction.
Disabling the preference prefers Dolby without disabling HDR10+-only playback.

Use the existing playback-generation latch to retain the established base
choice across frames missing HDR10+ metadata. A generation or preference change
resets it. Target mode and headroom do not participate in representation choice.
The decoded source owns the preference; its existing content revision and update
signal invalidate paused output. The existing importer remaps only when the
selected representation changes. No decoder restart or new reload subsystem.

Supported authored curves use libplacebo ST2094-40 for both SDR and adaptive HDR.
Keep source metadata, including authored target luminance, unchanged. Libplacebo
adapts that curve to the existing destination: nominal 100 nits for SDR and
203 times headroom for adaptive HDR. This is a deliberate white-relative model,
not a claim of physical HDR10+ reference-display equivalence. Unsupported curves
retain the existing validated scene/static fallback. Dolby-mapped pixels never
consume base HDR10+ guidance. Other SDR/HDR mapping and output scaling are unchanged.

## Why

Provide one predictable format preference using supported library operations,
without display-specific selection, quality scoring, exposure compensation,
peak detection, or a new rendering subsystem. This is a product preference,
not a claim that HDR10+ always reproduces a better picture than Dolby Vision.

## Validation

Persistence and UI tests, policy tests including scene-only and late metadata,
real decoded paused preference changes, and existing HDR neutral/color sweeps
exercise this path. Physical appearance and native macOS/Wayland output remain
separate validation requirements.

A subsequent [user-reported mpv comparison](../research/2026-09-20-hdr10plus-external-playback-comparison.md)
reproduced a problematic source's HDR10+ brightness jumps outside SunPlayer.
The source-versus-libplacebo cause remains unresolved; no application workaround
is warranted by that comparison.

## Authored-curve coordinates and validation

Format detection is independent of scene statistics. Validate imported curve
numbers once; use finite [0,1] parameters and supported anchor counts (1..15 for
version 0,1..9 for version 1). ST2094-40:2020 §8.7.3.2 explicitly imposes the latter
limit. A previous audit using only generic FFmpeg/libplacebo headers missed it.
No raw/mapped duplicate validator or sorted-control-point heuristic is needed.
Keep supported single-window processing and coherent base-image selection.

Authored curves retain PL_HDR_METADATA_HDR10PLUS and the imported metadata;
libplacebo owns luminance inference and target adaptation. A zero average does
not make an otherwise usable curve unavailable. It can still cause libplacebo
to use static luminance information. Generic scene/static and mapped-Dolby
policies remain unchanged.

No custom source normalization, metadata repair, temporal smoothing, or
dependency patch is introduced. The preference changes representation selection;
it does not promise equivalent appearance between metadata families.

Playback source details show both formats; output details publish after
successful presentation. Native Settings-open visual refresh remains a manual
validation item.

Sources:
- [SMPTE ST2094-40:2020](https://pub.smpte.org/doc/st2094-40/20200409-pub/st2094-40-2020.pdf), §8.7.3.2, §8.7.4, Annex B.3.
- [Pinned libplacebo inference](https://github.com/haasn/libplacebo/blob/cee9b076f2c63104ccfd497fa79c39a867293ec4/src/colorspace.c).
- [Pinned curve implementation](https://github.com/haasn/libplacebo/blob/cee9b076f2c63104ccfd497fa79c39a867293ec4/src/tone_mapping.c).
