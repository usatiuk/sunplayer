# macOS profile-derived target gamut

Replace the macOS provider's fixed P3/BT.709 eligibility guesses with the active
screen profile's RGB primaries and white point. Keep the existing
DisplayStateProvider → VideoColorTarget contract, EDR headroom, extended-linear
sRGB presentation, and ColorSync calibration ownership.

1. Use LittleCMS for native matrix-profile characterization without observer
   adaptation. Verify known and custom profiles, including non-D65 white.
2. Remove named-gamut guessing. Unsupported or invalid profiles publish unknown
   gamut and use the existing shared fallback. Verify malformed-profile cases.
3. Build and run focused macOS tests, query the active native profile, review the
   diff, and synchronize graphics/research/testing documentation.

The user selected a macOS-only LittleCMS dependency to remove the prototype's
manual ICC decoding and matrix math. There is no WCG SDR enablement, Qt patch,
display-mode change, or application-owned display calibration.

The user also requested simplification of an existing local libass fix:
replace the copied port with three libass-specific Meson settings in the
existing macOS triplet, preserving qtkeychain configuration. Verify a rebuild
with Homebrew present and inspect the resulting pkg-config dependencies.

## Evidence

Upstream `main` fetched over HTTPS on 2026-10-25 (the configured SSH endpoint
was unreachable); the checkout was already current. The pre-existing build-doc
and libass-overlay work is now incorporated only to the extent explicitly
requested above.

* CMake configuration with manifest installation enabled passes on macOS 27.0.1
  with Qt 6.12.0 and pinned LittleCMS 2.19.1. The triplet change triggers a
  clean dependency rebuild; all requested vcpkg packages install successfully.
* `cmake --build cmake-build-debug --target sunplayer
  sunplayer_mac_display_gamut_tests sunplayer_presentation_target_tests
  sunplayer_libass_dependency_tests -j 6` passes.
* `ctest --test-dir cmake-build-debug -R
  '^(mac-display-gamut|presentation-target|libass-dependency)$'
  --output-on-failure` passes 3/3 suites. The gamut executable passes all six
  QtTest cases including init/cleanup: native profiles, custom D60 v2/v4,
  shared target and invalid input. Formatting and `git diff --check` pass.
* A native-screen probe of the final helper returns known, approximately P3
  primaries and D65 white for the built-in Retina display. The final probe links
  the pinned vcpkg LittleCMS archive; numerical results and research are in the
  [research note](../../research/2026-10-25-macos-display-profile-gamut.md).
* The upstream registry libass 0.17.5 Debug/Release builds pass with Homebrew
  fontconfig/libunibreak installed. Meson reports Core Text and both optional
  dependencies disabled. Both generated `libass.pc` files omit fontconfig and
  libunibreak; an isolated pkg-config static-link query succeeds using only
  vcpkg metadata. The copied libass overlay has been removed.
* Three independent reviews covered correctness, simplicity and test/build
  evidence. No blocking findings remain. The boundary retains narrow handling
  for tiny ICC roundoff; the intermediate manual decoder/SIMD approach is gone.

Physical output, live profile changes and external displays remain unverified;
LUT-only profiles remain outside the existing RGB-triangle target contract.
Subsequent external-Dell observation verifies extraction but exposes a target
authority limit: its active macOS HDR profile reports BT.2020, unlike the
narrower user-reported Windows gamut. Profile-derived gamut must not be called
verified physical gamut; see the research note's external-HDR section.
The pre-existing dependency deployment-floor warnings remain: current host
archives target macOS 27 while the player declares macOS 13. This delivery does
not establish older-macOS compatibility or package/bundle closure.
