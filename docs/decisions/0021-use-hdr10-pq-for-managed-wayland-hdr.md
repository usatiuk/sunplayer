# 0021: Use a stable HDR10/PQ content surface on managed Wayland

* Status: Accepted
* Date: 2026-08-03
* Amends the Linux choice in:
  [0002: Prefer extended-linear sRGB presentation with explicit SDR-white mapping](0002-extended-linear-srgb-presentation.md)
* Related:
  [0013: Rely on system display calibration on managed presentation paths](0013-rely-on-system-display-calibration.md),
  [0018: Support unmanaged sRGB SDR on native Wayland](0018-support-unmanaged-srgb-wayland-sdr.md)

## Context

SunPlayer composites video, subtitles, and UI in linear sRGB, where `1.0` is
the active platform reference white. The final Linux presentation step must
serialize that working value without changing its meaning.

Qt's Wayland `SRgbLinear` declaration supplies sRGB primaries and `ext_linear`
but no extended target volume. Values above `1.0` are valid `ext_linear`
values, but the declaration under-describes their intended HDR target volume.
Mutter does not advertise `extended_target_volume`, so Mesa does not expose
the exact Vulkan extended-sRGB-linear pair that QRhi requires.

BT.2020/PQ is the complete common contract. The compositor capabilities expose
BT.2020 and PQ, Mesa exposes the Vulkan HDR10 pair, and QRhi can select the
matching RGB10A2 swapchain. A PQ source description uses a 203-nit reference-
white coordinate. Wayland perceptual mapping anchors that source reference to
the active output reference; 203 is not a Linux display calibration value and
does not introduce Windows' `1.0 = 80 nits` convention.

The image description describes the surface's content, not its current
monitor. A BT.2020/PQ surface remains BT.2020/PQ when it is composited onto an
HDR output, an SDR output, or multiple outputs. Preferred-description feedback
is a rendering/efficiency hint, not a command to change the content encoding.

Stock Qt binds color-management-v1 version 1 and does not expose image-
description readiness or failure. SunPlayer's accepted latest-only version-2
contract therefore cannot use Qt as the protocol-object owner.

## Decision

When version 2, parametric descriptions, perceptual intent, named sRGB and
BT.2020 primaries, gamma 2.2, and PQ are available, SunPlayer owns the one
`wp_color_management_surface_v1`. It pre-creates and validates managed sRGB
and BT.2020/PQ descriptions using `ready2`, leaves Qt's requested Wayland color
space unset, and lets QRhi use Vulkan `PASS_THROUGH_EXT` while SunPlayer owns the
surface declaration.

The presentation window starts and remains in managed HDR10 mode independent
of output movement:

```text
linear-sRGB video + subtitles + UI
    -> linear-light composition
    -> linear BT.709/sRGB to linear BT.2020
    -> ST 2084, with working 1.0 encoded at the 203-nit source reference
    -> opaque RGB10A2 QRhi HDR10 swapchain
    -> compositor mapping to each HDR or SDR output
```

The Vulkan capability gate requires both
`A2B10G10R10_UNORM_PACK32 + HDR10_ST2084_EXT` and the same pixel format with
`PASS_THROUGH_EXT`. QRhi's public HDR10 check supplies the first semantic gate;
the explicit second check prevents Vulkan WSI from creating a competing color-
management surface or silently choosing an incoherent tuple.

Preferred-description changes update the retained video target and
diagnostics. They do not change the surface declaration, final encoder, or
swapchain format. Normal HDR/SDR display movement therefore preserves the
`QWindow`, `wl_surface`, graphics device, and HDR10 swapchain.

If the HDR10 format, render pass, initial swapchain creation, or later resize
actually fails without device loss, SunPlayer atomically rolls back to the
complete managed-SDR tuple: gamma-2.2 image description, SDR swapchain, and
gamma-2.2 final encoding. The protocol description is pending surface state
and is sent immediately before presenting the first matching buffer, so WSI's
next commit applies the description and pixels together. One failure suppresses
another HDR attempt for that graphics-device generation; a new device
generation may probe once again. Monitor movement and preferred values do not
clear the rejection.

When the managed-SDR capability set is absent, ADR 0018's unmanaged assumed-
sRGB path remains the startup fallback.

## Consequences

* Mutter and a future version-2 KWin use the same compositor-neutral path.
  SunPlayer contains no compositor-name checks.
* A window spanning HDR and SDR monitors has one stable content encoding;
  dominant-output changes cannot cause presentation-mode flicker.
* The compositor remains the final output tone/gamut mapper and calibration
  owner. SunPlayer does not apply `/80`, `referenceWhite / 80`, or display-peak
  scaling to the Linux working coordinate.
* Linux uses nonlinear 10-bit presentation while all internal layers remain
  FP16 linear sRGB. Windows and macOS keep their existing extended-linear
  presentation paths.
* The fixed final conversion is an output encoder, not display tone mapping.
  Libplacebo retains source interpretation and video rendering policy.
* SunPlayer assumes no HDR support from a version-1 global. Compatibility can be
  added later without changing the v2 ownership or rendering contract.

## Alternatives considered

### Reconfigure from HDR10 to SDR when the window moves

Rejected. The description labels content rather than an output mode. A window
can span outputs, preferred feedback is advisory, and a dominant-output flip
would create unnecessary format churn and risk mismatched pixels and metadata.

### Force FP16 plus Vulkan `PASS_THROUGH`

Rejected. Extended-linear values are encoding-valid, but Qt's declaration does
not describe SunPlayer's larger intended target volume consistently across the
target compositors, and QRhi's public format check rejects the available pair.

### Let stock Qt own the color surface

Rejected for the latest-only implementation. Qt binds version 1 and does not
provide a public readiness/failure result. A narrow SunPlayer-owned v2 object is
the smallest boundary that can enforce the required protocol and fallback
contract.

### Encode the video layer to PQ in libplacebo

Rejected at the current boundary. Video is rendered before subtitles and Qt
Quick UI are composed. The existing final compositor is the only place that
can encode the complete opaque surface once after linear-light composition.

## Evidence

The source findings, exact conversion, production precedents, and runtime
observations are recorded in
[the managed Wayland HDR research note](../research/2026-08-03-wayland-hdr10-presentation.md).
