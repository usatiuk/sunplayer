# macOS display-profile gamut

Investigation: 2026-10-25. Current implementation and validation are tracked in
the [delivery plan](../plans/video-rendering/2026-10-25-macos-profile-gamut.md).

## Native contract

`NSScreen.colorSpace.ICCProfileData` exposes the active profile. Primaries guide
libplacebo gamut mapping; they do not replace the extended-linear sRGB surface
basis or ColorSync's final calibration transform. EDR headroom remains a
separate AppKit observation. A profile describes the configured characterization,
not an independent physical measurement.

ColorSync provides conversion and raw-tag APIs but no direct native-primary
getter in the inspected public SDK. Its relative XYZ conversion yields PCS D50;
its absolute conversion alone does not undo observer adaptation. A prototype
using native matrix tags and SIMD recovered the observed Apple profile, but the
user preferred a library-owned interpretation over maintaining tag decoding.

## Qt limitations verified

The official Qt source tags 6.9.0, 6.10.0, 6.11.0, 6.12.0 and `dev` inspected
on this date declare `PrimaryPoints` fields white/red/green/blue, while
`QColorSpace::primaryPoints()` initializes red/green/blue/white. A standalone
Qt 6.12.0 built-in-sRGB reproducer returned red `(0.64,0.33)` in `whitePoint`,
disagreeing with both `whitePoint()` and `PrimaryPoints::fromPrimaries()`.
This requires no ICC file or platform display API. Other versions were
source-checked, not executed. SunPlayer does not call that getter.

Correcting the ordering alone is insufficient for this task: a custom D60
profile with v4-style D50 `wtpt` and a nonidentity `chad` is parsed with D50
as Qt's reported white. Qt also canonicalizes matrices close to named spaces.
No Qt patch or compatibility workaround is part of the accepted implementation.

## LittleCMS choice

The pinned libplacebo source's `detect_csp` already uses a LittleCMS context
with observer adaptation zero and an absolute-colorimetric RGB-to-XYZ transform
to recover native RGB and white. SunPlayer uses that characterization pattern
for four samples only, accepts matrix RGB profiles, validates the shared
triangle contract, and returns unknown for unsupported/unusable profiles.
No display-profile transform is applied to video pixels.

ICC fixed-point rounding can yield tiny negative native XYZ values at the
spectral boundary: native P3 red Z was approximately `-0.00001734`. Normalize
only negative components within four 16.16 steps of zero before validation;
do not loosen the shared chromaticity contract. Nonpositive/nonfinite XYZ sums
are invalid.

LittleCMS is a macOS-only static dependency via the existing vcpkg registry,
without optional tools/plugins. The port requires only the Meson host tool;
JPEG/TIFF are dependencies of optional utilities, not the core library.
An arm64 Homebrew 2.19 probe added 196,560 bytes (about 192 KiB) after static
linking/dead stripping; that is not an exact SunPlayer release-size measurement.
The application uses pinned registry 2.19.1. skcms exposes a D50 matrix rather
than eliminating native-adaptation recovery; qcms introduces Rust. Neither
simplifies this narrow existing C++/vcpkg integration.

The final helper was also compiled into a read-only native-screen probe, first
against Homebrew LittleCMS 2.19 and then the pinned vcpkg 2.19.1 archive. Both
returned the same known gamut on the built-in Retina display:
R `(0.67999494,0.32000509)`, G `(0.26499692,0.68999529)`,
B `(0.14999962,0.06000526)`, and W `(0.31269881,0.32900333)`.
AppKit reported current headroom `1` and potential `2` during the final probe.
This exercises active-profile extraction, not light output, external displays,
or live profile-change delivery. Pinned-build verification is in the delivery plan.

### External HDR profile can describe an encoding envelope

A subsequent user check on DELL U2725QE shows macOS target R `(0.7080,0.2920)`,
G `(0.1700,0.7970)`, B `(0.1310,0.0460)`, W `(0.3127,0.3290)`: BT.2020.
The final pinned-library native probe independently recovers those same values
from that screen's active profile. It reports current headroom `2.87924` and
potential `6.30392`. This is not a UI constant or the former P3 approximation.
The user reports Windows target R `(0.6762,0.3134)`, G `(0.2380,0.7041)`,
B `(0.1416,0.0479)`, W `(0.3127,0.3290)`.

[Dell specifies approximately P3 coverage](https://www.dell.com/en-sg/shop/monitors/apd/dell-ultrasharp-27-4k-thunderbolt-hub-monitor-u2725qe/u2725qe_monitor/).
The exact BT.2020 profile strongly suggests HDR encoding characterization,
rather than independently established physical-panel primaries. Profile
extraction is verified, but treating every active HDR profile as a physical
gamut is not justified. Do not hard-code a smaller gamut or add an EDID reader
without a separate native-contract investigation. Actual emitted color and
the downstream ColorSync/display mapping remain unmeasured.

## libass build simplification

The baseline libass port auto-detects optional Homebrew packages. The installed
vcpkg Meson helper reads `VCPKG_MESON_CONFIGURE_OPTIONS` from triplet scope.
Set CoreText enabled and fontconfig/libunibreak disabled only for libass in
the existing macOS triplet. This avoids a copied port and leaves upstream's
packaging and platform patches maintained by vcpkg.

## Sources

* [Apple NSScreen](https://developer.apple.com/documentation/appkit/nsscreen)
* [Apple ICC profile data](https://developer.apple.com/documentation/appkit/nscolorspace/iccprofiledata)
* [ICC display white and chromatic adaptation](https://www.color.org/whyd50/)
* [Qt 6.12 header](https://github.com/qt/qtbase/blob/v6.12.0/src/gui/painting/qcolorspace.h)
* [Qt 6.12 implementation](https://github.com/qt/qtbase/blob/v6.12.0/src/gui/painting/qcolorspace.cpp)
* [Pinned libplacebo ICC characterization](https://github.com/haasn/libplacebo/blob/cee9b076f2c63104ccfd497fa79c39a867293ec4/src/shaders/icc.c)
* [LittleCMS](https://littlecms.com/color-engine/)
