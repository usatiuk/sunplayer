# Wayland HDR regression: optional mastering metadata

## Cause

`git log -S masteringDisplayPrimaries -- src/platform/linux/WaylandColorManagement.cpp`
identifies September 20 commit `e7abecb`: it added mastering support as an HDR
prerequisite. The pre-WCG implementation already selected SDR without it.
The current native compositor advertises PQ, BT.2020, explicit luminances and
features 0–4, but not feature 5 (mastering). Before the fix it selects managed
display-primary SDR even with HDR-enabled preferred feedback. Reference white
383 and headroom 26.1097 are compositor feedback, not measured panel limits.

## Primary sources

* [Wayland protocol XML](https://github.com/wayland-mirror/wayland-protocols/blob/main/staging/color-management/color-management-v1.xml),
  checked against installed wayland-protocols 1.47: omitted mastering primaries
  and luminances inherit the primary volume. Both mastering requests require
  the mastering feature. PQ primary defaults are minimum 0.005, maximum 10000
  and reference white 203 nits. Explicit `(0,10000,203)` avoids undefined
  below-primary-volume black values from the existing zero-black compositor.
* [Qt 6.10.2 Vulkan QRhi](https://github.com/qt/qtbase/blob/v6.10.2/src/gui/rhi/qrhivulkan.cpp):
  HDR10 selects the 10-bit BT.2020/PQ tuple. The application separately checks
  the same Vulkan format's HDR10 and pass-through colorspace pairs so native
  protocol declaration and buffer interpretation agree.
* [Microsoft Advanced Color](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
  and [SetHDRMetaData](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/nf-dxgi1_5-idxgiswapchain4-sethdrmetadata):
  metadata also exists on Windows, but our Windows path uses FP16 linear scRGB
  with its own white-level contract. It has no Linux-style mastering feature
  eligibility check. Our macOS path uses Qt extended-linear EDR and AppKit
  headroom, likewise without this gate. No other-OS runtime validation was
  performed for this Linux-only fix.

## Result and limits

The fixed native trace has one ready named BT.2020/PQ enhanced description,
explicit luminances `(0,10000,203)`, zero mastering requests, and reuse of that
same description before XR30 (fourcc 808665688) buffer commits. Four fixture
frames decode, and the user observes HDR in the test window. The process exits
zero, but has no `application.playback_smoke_complete` marker; do not call this
a completed automated playback-smoke scenario.

Independent reviews found no further concrete adjacent HDR eligibility defect.
They corrected the initial assumption that PQ defaults to zero-black. Physical
luminance, compositor mapping quality and multi-monitor movement remain
separate validation gaps. Native execution of the mastering-supported branch
is unavailable on this compositor; policy and existing volume tests cover it.
