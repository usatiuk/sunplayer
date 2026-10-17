# Wayland wide-gamut SDR output

## Baseline and sources

Investigated on Qt 6.10.2, libplacebo 7.360.0, wayland-protocols 1.47,
GNOME Shell 50.1, Mesa 26.0.8 and Intel Arrow Lake on native Wayland.

Primary sources:

* [Wayland color-management protocol](https://raw.githubusercontent.com/wayland-mirror/wayland-protocols/main/staging/color-management/color-management-v1.xml):
  explicit primaries, luminances, version-2 preferred parametric feedback and
  immutable description readiness; installed protocol 1.47 is the build input.
* [Qt 6.10.2 Vulkan QRhi](https://github.com/qt/qtbase/blob/v6.10.2/src/gui/rhi/qrhivulkan.cpp):
  SDR native format selection and pass-through colorspace selection. QRhi has
  no dedicated 10-bit SDR swapchain request.
* [libplacebo 7.360.0 colorspace implementation](https://github.com/haasn/libplacebo/blob/v7.360.0/src/colorspace.c):
  relative-colorimetric RGB conversion adapts white using CAT16. The installed
  public header's Bradford comment is stale. Native D50 captures exposed this
  discrepancy; an independent RGB/XYZ/CAT16 calculation agrees with the source
  and GPU results. Keep using the library transform rather than duplicating it.

## Accepted pipeline

Signed FP16 linear BT.709/D65 is a coordinate basis, not a clipped sRGB gamut.
For example, Display-P3 red is approximately `(1.224940, -0.042057, -0.019638)`.
The existing libplacebo mapper targets the preferred physical gamut while
storing its result in this extended basis. Video, decoded sRGB UI and subtitles
compose in linear light. The final compositor converts once to the declared
output primaries and white point, clamps there to `[0,1]`, then gamma-2.2
encodes. Never clamp extended BT.709 values before this conversion.

The SDR declaration uses explicit preferred target primaries and
`set_luminances(0, 80, 80)`. These describe relative content black/reference
white, not measured panel luminance. The renderer uses zero content black,
one-times headroom and no HDR/scRGB scale. The compositor applies physical
black, reference-white mapping and calibration. This avoids baking physical
black twice or introducing a second ICC conversion. With no valid feedback,
the renderer and declaration both use sRGB primaries until feedback arrives.

WCG SDR requires explicit primaries and luminances plus the existing managed
SDR facilities; it does not require PQ or mastering metadata. Selection is
HDR10/PQ when its full contract exists, otherwise display-primary SDR, then
managed sRGB, then unmanaged sRGB according to available capabilities. Actual
enhanced-description rejection is bounded to the graphics generation.

## Native observations and limits

This compositor advertises color-management-v1 version 2, explicit primaries,
explicit luminances and gamma 2.2, but no mastering-display feature. WCG SDR
therefore works independently of the existing HDR gate.

A production playback trace accepts the exact preferred target coordinates:
R `(0.679687,0.314453)`, G `(0.257812,0.691406)`,
B `(0.143554,0.049804)`, W `(0.313476,0.329101)`.
The ready description is applied before the matching surface buffer commit.
Qt selects native XRGB8888 (DRM fourcc 875713112); this is 8-bit WCG output.
Float composition and correct gamut encoding do not imply 10-bit presentation.
No custom swapchain is warranted for this change.

Independent GPU oracles cover D65 and D50/CAT16 conversion, signed P3 red,
neutral/color UI, subtitles, and FP16/UNORM8 outputs. A synthetic decoded P3
AVFrame is rerendered for P3 → sRGB → P3 without another input import.
Protocol acceptance and pixel captures do not measure physical calibration,
panel gamut, banding, or live movement across multiple monitors.
