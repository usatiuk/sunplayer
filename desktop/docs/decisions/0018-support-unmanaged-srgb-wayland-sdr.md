# 0018: Support unmanaged sRGB SDR on native Wayland

* Status: Accepted
* Date: 2026-08-02
* Supersedes:
  [0017: Require color-management-v1 for the first Linux release](0017-require-wayland-color-management-v1.md)
* Related:
  [0013: Rely on system display calibration on managed presentation paths](0013-rely-on-system-display-calibration.md),
  [0015: Target Wayland and leave X11 unsupported](0015-wayland-only-linux-desktop.md),
  [0016: Reconcile output changes by semantic value](0016-reconcile-output-changes-semantically.md)

## Context

Native Wayland Vulkan presentation does not technically require
color-management-v1. Without an image description, the protocol defines
surface color handling as compositor implementation-defined and recommends
that compositors treat the surface as sRGB. This is not a managed or
color-calibrated contract, but it is the established interoperable convention
for ordinary SDR desktop content.

Qt 6.10 also has a natural high-level boundary for this distinction. It only
creates a color-management surface when the requested `QSurfaceFormat` has a
non-empty color space and the compositor exposes a color manager. Leaving the
requested color space unset therefore uses an ordinary undeclared Wayland
surface without adding a second protocol owner or custom commit path.

ADR 0017 rejected that fallback to keep the first release managed-only. That
restriction prevents useful SDR playback on otherwise supported native
Wayland systems and WSLg, while avoiding little implementation complexity:
Sunroom already needs the shared `UnmanagedSrgb` mode and piecewise-sRGB final
transfer for Windows SDR.

## Decision

Linux continues to require native Wayland and does not support X11 or
XWayland. Color-management-v1 is an optional presentation capability:

* When the compositor can create Sunroom's sRGB-primary, `gamma22`, perceptual-
  intent description, Qt remains the sole color-management surface owner.
  Managed SDR requests `QColorSpace::SRgb` and emits the matching pure
  gamma-2.2 encoding, `encoded = linear^(1/2.2)` for normalized non-negative
  values.
* Managed HDR additionally requires `ext_linear`, preferred surface feedback,
  usable description information, and the Vulkan presentation capabilities.
  It requests `QColorSpace::SRgbLinear` and uses the coupled extended-linear
  FP16 path. Missing HDR-only capabilities leave managed SDR available.
* When the global or any capability required to declare managed SDR is absent,
  Sunroom selects `UnmanagedSrgb`, leaves the requested Qt surface color space
  unset, emits exact piecewise sRGB, assumes an SDR target with one-times
  reference-white headroom, and reports managed color and HDR as unavailable.
* The unmanaged choice is made once from the completed startup capability
  inventory and remains fixed for that native window. Sunroom does not poll,
  retry, recreate the window merely to acquire the protocol later, or build a
  second display-transition model.
* The unmanaged path does not bind surface feedback, infer HDR from monitor
  metadata, apply an output ICC profile, or claim calibrated output. It is an
  honest conventional SDR fallback.
* Failure of an optional HDR transition on a compositor with the managed
  capability set still recreates the managed gamma-2.2 SDR surface. It does
  not discard working color management merely because HDR presentation failed.
  That failure suppresses another HDR attempt for the same semantic preferred
  target and graphics-device generation. Retry occurs only after either value
  materially changes; equivalent feedback does not retry. This uses existing
  target and resource-lifetime identities rather than a timer or retry state
  machine.

Pure gamma 2.2 and the sRGB transfer function are deliberately distinct.
Although “sRGB gamma 2.2” is common shorthand, exact sRGB has a linear segment
near black and a scaled power segment. Sunroom uses gamma 2.2 only when Qt
declares `gamma22`; this names the display-side exponent, so Sunroom's encoding
uses its reciprocal rather than `linear^2.2`. An undeclared surface follows the
compositor-recommended sRGB convention and therefore receives piecewise-sRGB
output.

## Consequences

* Ordinary SDR playback can run on native Wayland compositors without
  color-management-v1, including environments useful for software-Vulkan
  lifecycle testing.
* Managed SDR/HDR remains one optional capability branch owned through Qt; the
  fallback adds no Wayland object ownership and no platform renderer.
* Diagnostics must distinguish unmanaged assumed-sRGB SDR from managed
  gamma-2.2 SDR and managed extended-linear HDR, including why the managed
  capability set was unavailable.
* Unmanaged output may vary with compositor policy because the protocol does
  not normatively require sRGB handling. Support claims must describe it as
  assumed sRGB rather than calibrated or managed output.
* HDR controls and claims remain unavailable in unmanaged mode. An HDR monitor
  does not change that result without the usable HDR capability set.
* Tests cover exact piecewise-sRGB output for unmanaged SDR and exact gamma-2.2
  output for managed SDR so the similar curves cannot be accidentally merged.

## Alternatives considered

### Continue requiring color-management-v1 for all Linux playback

Rejected. It turns an optional HDR/color-management capability into a startup
requirement even though native Wayland can present conventional SDR correctly
enough for the fallback claim.

### Emit gamma 2.2 on an undeclared surface

Rejected. No protocol declaration tells the compositor that the pixels use a
pure gamma-2.2 transfer. The documented convention for undeclared surfaces is
sRGB, so exact piecewise sRGB is the coherent encoding.

### Add a Sunroom-owned color-management surface when Qt cannot provide one

Rejected. It duplicates Qt's ownership, risks a protocol error, and does not
help when the compositor lacks the protocol. The fallback needs no additional
Wayland object.

### Fall back through X11 or XWayland

Rejected. This decision changes only the optional color capability on an
already native Wayland connection. ADR 0015 remains unchanged.

## Sources

* [Wayland color-management-v1 protocol](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/color-management/color-management-v1.xml)
* [Qt 6.10 Wayland window color-space integration](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/qwaylandwindow.cpp?h=6.10)
* [Qt 6.10 Wayland color-management mapping](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/wayland/qwaylandcolormanagement.cpp?h=6.10)
