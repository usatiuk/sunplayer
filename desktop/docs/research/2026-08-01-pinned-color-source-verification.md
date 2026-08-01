# Pinned color-pipeline source verification

* Date: 2026-08-01
* Scope: FFmpeg 8.1.2 and libplacebo 7.360.1 as pinned by Sunroom
* Related input:
  [Video-rendering and color-management milestone review](2026-08-01-color.md)

## Question

Which claims from the broader color-management review are supported by the
exact dependency versions and build configuration used by Sunroom, and which
claims need narrower wording or an experiment?

## Sources inspected

The review used the exact libplacebo archive already downloaded by vcpkg at
commit `cee9b076f2c63104ccfd497fa79c39a867293ec4` and the pinned FFmpeg 8.1.2
source/configuration. Relevant implementation points were:

* libplacebo `src/colorspace.c`, `src/shaders/colorspace.c`, public color-space
  and libav helper headers, and generated build configuration;
* FFmpeg `libavcodec/decode.c`, codec-parameter/context propagation, frame side
  data, and the installed feature configuration;
* Sunroom's current FFmpeg-frame importer, D3D11 importer, presentation target,
  and compositor boundaries.

## Confirmed findings

### The product luminance contract remains correct

Normal desktop playback must use a display-relative working space in which
`1.0` means the active platform SDR/reference white. On a Windows HDR Advanced
Color display, scene-referred scRGB uses `1.0 = 80 nits`, so the final
whole-composition conversion is `referenceWhiteNits / 80`. On an SDR Advanced
Color/WCG display, FP16 is display-referred and working white maps to scRGB
`1.0` instead. Content adaptation belongs in libplacebo; the compositor must
not apply a second video-only exposure or tone map.

### The virtual target is a static-PQ bridge, not a universal HDR target

libplacebo defines `PL_COLOR_SDR_WHITE` as 203 cd/m2 and normalizes linear HDR
so `1.0` corresponds to that value. Therefore a destination maximum of

```text
203 * physicalPeakNits / referenceWhiteNits
```

creates the intended numerical output capacity for static PQ: the resulting
linear headroom is `physicalPeakNits / referenceWhiteNits`.

This proves a coordinate relationship, not the exact behavior of every tone
mapper. A 203-nit patch, below-reference-white behavior, monotonicity,
no-expansion behavior, and peak bounds still require real render tests.

### HLG cannot use that virtual destination uncritically

In libplacebo 7.360.1, `pl_color_space_infer_map()` assigns an HDR
destination's `max_luma` to an HLG source. The shader path then derives HLG's
system gamma/OOTF from that value. Supplying a virtual 1218-nit destination
for a physical 600-nit display therefore evaluates HLG for the wrong physical
peak and may change midtones as well as highlights.

HLG remains subject to the same product invariant—adaptation to reference
white and available headroom—but needs a target model that separates the
physical OOTF peak from the output normalization.

HDR10+ and Dolby Vision require distinct acceptance checks. libplacebo maps
HDR10+'s source-authored targeted-system-display maximum to
`hdr.ootf.target_luma`; that physical source value must remain unchanged while
the current physical display peak is supplied separately through destination
`hdr.max_luma`. Libplacebo derives the tone mapper's effective `output_max`
from that destination subject to its mapping policy. The pinned
`pl_map_avdovi_metadata` path maps supported reshaping
when `disable_residual_flag != 0`, plus source minimum/maximum and Level-1
maximum/average metadata. It does not implement target-specific trims or
enhancement-layer residual processing. Dolby Vision acceptance must therefore
identify the supported reshape or base-layer fallback path and must not imply
trim or residual support without additional upstream capability.

### Inverse tone mapping has a narrower guarantee than “identity”

With inverse tone mapping disabled, libplacebo bounds the requested tone-map
range so a lower-peak source is not deliberately expanded to fill a brighter
target. This does not promise pixel identity: gamut mapping, black-point
handling, the selected tone curve, and dynamic peak state may still alter
values. Tests should assert the observable no-expansion and mapping invariants,
not private branch behavior.

### The final FFmpeg frame is the authoritative evidence boundary

FFmpeg 8.1.2 propagates common scalar color fields and coded side data through
codec parameters and the codec context. Its decode core fills unspecified
frame fields from the context while retaining explicit decoder-provided frame
values. By the time Sunroom receives the final `AVFrame`, it cannot reliably
distinguish those two origins.

Consequently, provenance must say “final FFmpeg-decoded frame value, possibly
context-propagated” rather than inventing decoder-versus-stream certainty.
Copying stream metadata onto the private frame again is not a policy engine and
currently fills only unspecified fields or absent side data, but still obscures
provenance and makes ordering part of the policy. `pl_frame_copy_stream_props`
is also not a replacement for an explicit resolution policy: it covers only a
subset and may overwrite already valid HDR fields.

### Frame ownership and import support

`pl_map_avframe_ex` retains an `AVFrame` reference until unmapping. Software
upload callbacks can retain plane references, and reusable upload textures may
outlive one mapping. Sunroom must still retain native hardware resources until
GPU consumption completes.

The exact libplacebo helper has direct paths for DRM PRIME, VAAPI-derived DRM,
and Vulkan frames. It does not make Sunroom's separate D3D11VA or future
VideoToolbox importer seams redundant.

## ICC and display calibration

The current libplacebo build has LCMS disabled, and the current FFmpeg build
does not add LCMS processing. Embedded ICC bytes can be retained and exposed
through mapped frame/profile state, but libplacebo currently falls back to
scalar color tags rather than executing the ICC transform. Sunroom must not
claim source-ICC rendering yet.

Source ICC and display ICC are different responsibilities:

* Source ICC describes content and belongs before the shared working space.
* Display ICC calibrates the final composed surface to a physical monitor.

For modern managed paths, Sunroom should tag the presentation surface and let
the OS/compositor apply display calibration exactly once. This includes
Windows Advanced Color, macOS ColorSync/EDR, and a supporting Wayland color
management compositor. When Windows Advanced Color is inactive, ordinary
DirectX SDR presentation is an unmanaged sRGB-assumed fallback.

If application-managed display ICC is implemented later, it must transform the
entire composed surface after QRhi composition. Applying it inside the video
renderer would calibrate video while leaving UI and subtitles inconsistent.

With LCMS enabled, libplacebo applies a usable RGB source ICC as the source
transform and replaces frame primaries and the complete HDR metadata with
ICC-derived state; it also replaces transfer when detected. It does not then
combine the frame's mastering or dynamic HDR metadata with the profile. Initial
source-ICC support should therefore be limited to validated SDR RGB profiles.
ICC combined with PQ, HLG, HDR10+, or Dolby Vision remains unsupported pending
an explicit target model or upstream-supported integration.

## Consequences for the plan

1. First implement an immutable effective-source description and remove
   redundant mutation of retained frames.
2. Add a small real FFmpeg-decoded static-PQ fixture and validate display-
   relative invariants through production libplacebo and QRhi boundaries.
3. Scope the existing virtual target to static PQ until HLG and dynamic-HDR
   physical target semantics are solved or supported upstream.
4. Evolve the existing presentation state rather than creating speculative
   symmetric display abstractions. Add stable display identity, window/output
   association, revisions, provenance, and stale-query rejection when the live
   Windows display slice begins.
5. Preserve and diagnose embedded source ICC now, but defer ICC transforms.
   Rely on system-managed output calibration where available and label the
   Windows non-Advanced-Color SDR fallback as unmanaged sRGB.
