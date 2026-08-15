# Wayland HDR10 presentation investigation

* Date: 2026-08-03
* Local source scope: Qt 6.10.2, Mesa 26.0.3, Wayland protocols 1.47,
  Mutter 50.1, KWin 6.6.5, and libplacebo 7.360.0
* Additional parallel-source scope: Qt 6.10.3, 6.11, current development,
  Chromium/Ozone, Krita, and Shotcut behavior

This note records evidence and rejected paths. Current accepted behavior is in
[ADR 0021](../decisions/0021-use-hdr10-pq-for-managed-wayland-hdr.md).

## Runtime observations

SunPlayer bound color-management-v1 version 2 and received distinct preferred
descriptions while moving between the connected HDR and SDR outputs. The HDR
description reported a 162-nit reference, 0.005-nit minimum, and 10,000-nit
target maximum; the SDR description reported 80/0.2/80. This proved the
surface-following feedback lifetime. Mutter's 10,000-nit value is the PQ
envelope rather than its panel peak; another compositor may publish a more
output-specific target maximum. SunPlayer consumes the compositor-declared value
without compositor-specific reinterpretation.

The first FP16 implementation failed because QRhi could not find
`R16G16B16A16_SFLOAT + EXTENDED_SRGB_LINEAR_EXT`. Recreating the `QWindow` to
change Qt's requested color space also caused visible window teardown and text-
input leave/re-entry while dragging between displays. That is not an accepted
normal movement path.

The later BT.2020/PQ experiment produced observable HDR behavior on Mutter.
Complete post-rewrite movement and physical-output evidence is still pending.

## Qt Wayland color declarations

Qt maps these named color spaces as follows:

| QColorSpace | Wayland primaries | Wayland transfer |
| --- | --- | --- |
| `SRgb` | `primaries_srgb` | `gamma22` |
| `SRgbLinear` | `primaries_srgb` | `ext_linear` |
| `Bt2100Pq` | `primaries_bt2020` | `st2084_pq` |

Qt does not set source luminances, mastering primaries/luminance, MaxCLL, or
MaxFALL for these named spaces. Its 6.10 implementation creates a parametric
description asynchronously, applies it with perceptual intent, and logs a
TODO rather than exposing a usable application failure result when creation is
rejected. The materially relevant behavior remains in 6.11 and development.

Stock Qt also binds `wp_color_manager_v1` at version 1. It therefore cannot
satisfy SunPlayer's intentional version-2-only use of `ready2` and
`preferred_changed2`, even on a compositor advertising version 2.

Description creation is asynchronous: version 2 promises an eventual
`ready2` or `failed`, not completion before the next display sync callback.
SunPlayer therefore waits for a terminal event from each startup description;
one `wl_display_roundtrip` alone must not be interpreted as rejection.

## QRhi and Vulkan WSI coupling

QRhi recognizes only exact semantic HDR pairs:

```text
HDRExtendedSrgbLinear = R16G16B16A16_SFLOAT
                        + EXTENDED_SRGB_LINEAR_EXT

HDR10                 = A2B10G10R10_UNORM_PACK32
                        + HDR10_ST2084_EXT
```

After selecting a pixel format, Qt's Wayland Vulkan path substitutes
`PASS_THROUGH_EXT` when the same format is available with pass-through. That
prevents Vulkan WSI from creating a second color-management surface when the
application or Qt already owns the declaration.

This creates a real distinction between semantic capability and actual WSI
ownership. A reliable application-owned HDR10 path must require both:

```text
A2B10G10R10_UNORM_PACK32 + HDR10_ST2084_EXT
A2B10G10R10_UNORM_PACK32 + PASS_THROUGH_EXT
```

Ignoring `QRhiSwapChain::isFormatSupported()` is not safe. QRhi can retain the
logical requested enum while falling back to the first available native
format, including an ordinary 8-bit format.

## Mesa, Mutter, and KWin findings

Mesa Wayland WSI always includes sRGB nonlinear and pass-through color spaces.
It exposes additional semantic color spaces from the compositor's advertised
primaries, transfers, and features:

* extended sRGB linear requires sRGB, `ext_linear`, and
  `extended_target_volume`;
* HDR10 requires BT.2020 and PQ but not `extended_target_volume`;
* on presentation it creates its own color surface for semantic color spaces,
  but not for `PASS_THROUGH_EXT`.

Mutter 50.1 advertises parametric descriptions, sRGB, BT.2020, `ext_linear`,
PQ, and luminance support, but not `extended_target_volume`. Its color-state
path expects FP16 for linear buffers and 10-bit buffers for PQ. Mutter updates
preferred feedback when a surface's main monitor changes; it does not change
the client's content description.

The inspected KWin 6.6.5 sources advertise extended-target-volume and related
HDR features, and its linear transfer does not inherently clamp values above
`1.0`. That release exposes protocol version 1, so it is outside SunPlayer's
current latest-only managed path. Its surface state likewise stores the client
description separately from the compositor's preferred output description.

No SunPlayer policy branches on a compositor name. A version-2 KWin global with
the required capabilities uses the same path as Mutter.

## Extended linear versus HDR10

The protocol defines `ext_linear` over real values, so FP16 channel values
above `1.0` and below `0.0` are encoding-valid. Pass-through does not change
their meaning; it only leaves Wayland color declaration ownership with the
application.

The problem is target volume, not numeric validity. Without explicit extended
target volume, Qt's `SRgbLinear` description does not state that highlights
above reference white occupy a larger HDR content volume. KWin may process the
values usefully and Mutter may preserve them, but the declaration is not a
complete compositor-neutral HDR contract. Mesa's exact-format decision
reflects that distinction.

BT.2020/PQ provides the complete common declaration and the public QRhi HDR10
format, so it is the selected path.

## Stable content encoding across outputs

A Wayland image description labels the buffer's content encoding. It is not a
request to set a monitor mode. The compositor may convert that source
description to each output's description. Preferred feedback only indicates a
potentially more efficient or accurate client encoding.

Consequently an HDR-capable SunPlayer window keeps its BT.2020/PQ description
and HDR10 swapchain when it moves among HDR and SDR outputs or spans both.
Changing encoding based on the dominant output would create brightness jumps,
unnecessary swapchain churn, and a risk of one buffer being interpreted with
the other mode's description. The compositor already owns the per-output
mapping.

Managed SDR remains a complete fallback after a genuine HDR10 format,
render-pass, creation, or resize failure. A new graphics-device generation may
probe HDR once again. Output movement and preferred-description changes are
not retry triggers. A future explicit SDR-only session/content policy can use
the same bidirectional tuple transition without making it output-driven.

An SDR output's valid 1x preferred description still supplies reference white
and target minimum/maximum while the content surface remains PQ. SunPlayer uses
those target fields without treating `hdrActive == false` as missing data.

For the exceptional PQ-to-SDR fallback, the replacement swapchain and encoded
frame are prepared before `set_image_description` is sent. The request is
queued immediately before Vulkan present in the same event-loop turn, so Qt
cannot insert an unrelated bufferless surface commit between the new
description and matching buffer.

## Final linear-sRGB to BT.2020/PQ encoder

All layers remain FP16 linear sRGB until the final opaque draw. D65 is common
to sRGB/BT.709 and BT.2020, so no chromatic adaptation is required:

```text
[R2020]   [0.627403896  0.329283038  0.043313066] [R709]
[G2020] = [0.069097289  0.919540395  0.011362316] [G709]
[B2020]   [0.016391439  0.088013308  0.895595253] [B709]
```

Do not clamp the extended linear-sRGB input before the matrix. Negative or
greater-than-one components can represent a valid color after the primary
conversion. Clamp negative light only after conversion.

For each non-negative BT.2020 component `S`, SunPlayer uses inverse ST 2084:

```text
L = S * 203
Y = min(L / 10000, 1)

m1 = 2610 / 16384
m2 = 2523 / 32
c1 = 3424 / 4096
c2 = 2413 / 128
c3 = 2392 / 128

E = ((c1 + c2 * Y^m1) / (1 + c3 * Y^m1))^m2
```

Working `1.0` becomes PQ code approximately `0.580688881`. The fixed 203-nit
number is the source description's reference coordinate. Wayland perceptual
anchoring maps it to the active output reference white, so Linux does not use
`/80`, `referenceWhite / 80`, or display-peak scaling. The representable
working range is bounded by `10000 / 203` before libplacebo target provisioning
so the renderer cannot target values the final encoder would collapse. This is
an encoding-coordinate bound, not special handling for Mutter: a compositor
that supplies a lower, output-specific target continues to constrain the
renderer to that lower value.

The RGB10A2 output is opaque. Optional PQ-code-space dithering around half a
10-bit code value may be investigated if physical testing shows banding; it is
not part of the initial implementation.

## Libplacebo boundary

Libplacebo can perform BT.709/BT.2020/PQ conversion, but its normal color-map
configuration can also introduce tone and gamut mapping. SunPlayer needs one
fixed presentation encoder after video, subtitle, and UI composition, with the
desktop compositor retaining final output tone mapping.

The accepted split is therefore:

```text
libplacebo:
    source decode/color interpretation and video rendering
    -> FP16 linear sRGB, 1.0 = reference white

SunPlayer final QRhi shader:
    linear composition -> BT.2020 matrix -> PQ -> RGB10A2
```

Encoding only the video layer in libplacebo would make later UI/subtitle
composition nonlinear or require another conversion pass.

## Production precedents and upstream limitations

Krita's custom Wayland color-management integration is the closest Qt
precedent: it directly owns protocol objects, tracks platform-surface lifetime,
queries preferred descriptions, and applies descriptions only after successful
asynchronous construction. Shotcut's HDR preview uses QRhi HDR10 rather than
scRGB on Linux, though its Qt Quick property is private API. Chromium/Ozone
implements color-management-v1 directly and is evidence for the native
Wayland/Vulkan architecture, not for QRhi's extended-linear support check.

Useful upstream Qt issues exposed by this work are:

1. semantic HDR capability checks do not model the later pass-through choice;
2. an HDR request may silently fall back to an ordinary native format;
3. applications cannot query the actual selected native format/colorspace;
4. the Wayland client binding remains version 1;
5. image-description failure is not exposed publicly;
6. `QColorSpace` cannot express luminance/mastering/MaxCLL/MaxFALL metadata;
7. Linux `QRhiSwapChain::hdrInfo()` does not provide useful output limits.

## Primary source locations

* Qt: `src/plugins/platforms/wayland/qwaylandcolormanagement.cpp`
* Qt: `src/plugins/platforms/wayland/qwaylandwindow.cpp`
* Qt: `src/gui/rhi/qrhivulkan.cpp`
* Mesa: `src/vulkan/wsi/wsi_common_wayland.c`
* Wayland protocols: `staging/color-management/color-management-v1.xml` and
  its appendix
* Mutter: `src/wayland/meta-wayland-color-management.c` and color-state code
* KWin: `src/wayland/colormanagement_v1.cpp` and client tests
* libplacebo: `src/shaders/colorspace.c`
* [Vulkan WSI specification](https://github.khronos.org/Vulkan-Site/spec/latest/chapters/VK_KHR_surface/wsi.html)
* [Chromium Wayland color-management implementation](https://chromium.googlesource.com/chromium/src/+/07c9a59c2a5256ce49c22445a6c5108182c7da11)
