# Color-managed wide-gamut SDR on native Wayland

Status: Delivered; native WCG validation passes, unrelated media test flake recorded

## Requirements and baseline

Implement full WCG SDR behavior independently of HDR, including source color
mapping, neutral sRGB UI/subtitles, buffer encoding/declaration agreement,
paused-frame reconciliation, native lifecycle, diagnostics and honest fallback.
Use the user's native baseline: Qt 6.10.2, wayland-protocols 1.47,
color-management-v1 version 2, GNOME 50.1, Mesa 26.0.8, Intel Arrow Lake.
No new backward-compatibility branches or installations. Physical colorimeter
accuracy cannot be inferred from GPU captures or protocol success.

## Delivered implementation

* Keep FP16 linear-sRGB working coordinates and the existing libplacebo
  physical-gamut mapping. Use the compositor's preferred target primaries,
  not source metadata or a guessed panel profile.
* Add managed display-primary gamma-2.2 SDR presentation when explicit
  primaries are supported. This is independent of PQ/mastering capabilities.
  Prefer the existing HDR path where its complete contract is available.
* Convert the complete linear-light composition once into the declared
  output primaries, then clamp and gamma-2.2 encode in that output basis.
  Derive the matrix through libplacebo, including white-point adaptation.
  Do not clamp extended/negative linear-sRGB coordinates before conversion.
* Keep the existing SDR swapchain and Qt's pass-through behavior. This avoids
  depending on a Vulkan HDR colorspace or creating another surface owner.
  SDR output precision follows Qt's selected SDR format; no invented 10-bit
  swapchain API or custom presentation backend.
* Let managed SDR consume valid preferred gamut/white/black feedback without
  asserting HDR presentation or granting HDR headroom. HDR-to-SDR policy stays
  the accepted SDR compatibility policy; only the available target gamut grows.
* Extend the existing async immutable declaration preparation to SDR primaries.
  Apply ready declarations immediately before the matching buffer commit;
  latest semantic target changes rerender paused video. Unknown feedback
  starts with an honest sRGB declaration; update when feedback arrives.
* Recover only native description rejection through the existing canonical
  managed-sRGB path. Keep rejection bounded by graphics generation; no new
  epochs, duplicate target snapshots, profile manager or selector UI.
* Keep compositor calibration authoritative. No second ICC transform.

## Acceptance

Policy tests separate WCG SDR from HDR, capability gaps and bounded fallback.
Analytic GPU captures independently verify target-primary conversion, signed
working values, gamma-2.2, neutral/color UI and subtitle composition, and
source WCG preservation vs sRGB fallback. Retain the existing real decoded fixture coverage and add a synthetic P3
AVFrame retained-input target-change test. Verify declared protocol coordinates and native
swapchain output on the host outside the sandbox; run full CTests with VAAPI
required, QML lint and Debug/Release builds. Review correctness/color,
lifecycle/simplicity, and tests/evidence with independent read-only reviewers.

## Delivery evidence

The implementation keeps existing device/surface ownership, FP16 working
storage, libplacebo physical-gamut mapping and one async pending declaration.
Startup prepares only canonical managed sRGB; actual HDR/WCG descriptions use
the existing asynchronous frame preparation path. Independent correctness,
simplicity/lifetime and evidence reviews found no blocking issues. Accepted
review improvements removed a redundant startup PQ declaration and added
UNORM8 coverage.

* Debug and Release Ninja builds pass with four build workers.
* `all_qmllint` passes.
* Focused native `wcgSdrFinalComposition wcgSdrRetainedFrameTargetChanges`
  passes: seven QtTest results, no skips. D65/D50 and FP16/UNORM8 cover signed
  P3 red, black/white/gray, neutral/color sRGB UI and subtitle blending.
* The first D50 oracle failed because the installed libplacebo header says
  Bradford. Pinned source uses CAT16; independently computing CAT16 matches
  all nine matrix coefficients and GPU captures. Production already used the
  correct library transform. Independent review confirmed the correction.
* Native Debug production playback smoke succeeds, advances audio/video and
  accepts exact preferred target primaries with `(0,80,80)` luminances.
  `WAYLAND_DEBUG=client` confirms `ready2`, matching declaration before buffer
  commit, preserved native surface and XRGB8888 buffers (8-bit).
* A full parallel native suite run concurrent with Release compilation passed
  rendering but hit media seek/cancellation timing and playback-smoke timeout
  failures. All three failed CTests pass when rerun serially.
* Final serial full suite with `SUNPLAYER_REQUIRE_VAAPI=1`: **34/35 pass**
  in 44.06 seconds. Only the untouched MediaSession inter-frame seek test
  fails its immediate `!seeking()` assertion after waiting for `Ready`; this
  also passed the preceding serial rerun. Rendering, VAAPI, WCG policy, native
  application playback and all dependency tests pass. The known timing issue
  is recorded in `docs/DEFERRED.md`, not hidden by another rerun.
* `git diff --check` and clang-format validation pass.

All native GPU/device tests run outside the sandbox; nothing was installed.
Evidence logs during this run live in `/tmp/sunplayer-wcg-{focused,native-ctest,
rerun,native-ctest-final,native-smoke-final,release,qmllint-final}.log`.

## Remaining limits

Qt's selected SDR swapchain is 8-bit on this host. There is no dedicated QRhi
10-bit SDR request; no custom swapchain was introduced. GPU captures and
protocol acceptance do not measure physical monitor calibration, banding or
live multi-monitor movement. Those limits remain in `docs/DEFERRED.md`.
See [research](../../research/2026-10-17-wayland-wcg-sdr.md) and
[ADR 0032](../../decisions/0032-support-display-primary-wayland-sdr.md).
