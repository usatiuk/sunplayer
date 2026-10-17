# Linux VAAPI / DRM PRIME implementation evidence

Working tree based on `70ed0a4`; no commit or push performed.

Environment: Ubuntu 26.04.1, kernel 7.0.0-38-generic, GCC 15.2,
Qt 6.10.2, FFmpeg 8.0.1 (libavutil 60.8.100 / libavcodec 62.11.100),
libplacebo 7.360.0, libdrm 2.4.131, libva 1.23.0. Weston 14.0.2 runs a
private native-Wayland headless/pixman display; Vulkan uses lavapipe through
`/usr/share/vulkan/icd.d/lvp_icd.json`. The sandbox hides `/dev/dri`; this
initial run supplied no native GPU or audio-device evidence. That sandbox
view must not be confused with the physical host described below.

## Delivered implementation

* QRhi enables supported requested fd/dma-buf/modifier extensions; libplacebo
  imports that subset on the same Vulkan device.
* Vulkan DRM major/minor selects the verified libdrm character-device render
  node; FFmpeg opens VAAPI there. Missing import/device/driver capability
  reports a reason and preserves software decoding.
* Libplacebo's FFmpeg mapper exports direct read-only DRM PRIME planes,
  imports formats/modifiers, and retains source metadata and descriptors.
* The Linux importer retains the active mapping plus at most one retired
  mapping. It polls every plane to completion before releasing a decoder
  surface, and drains before graphics teardown. Shared generation rejection
  and one-shot software restart remain authoritative.
* Diagnostics expose the API, render node in device logs, plane fourcc,
  modifier, pitch/offset, waits, and known transfer/copy counts.

## Validation

```sh
cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/linux-debug --parallel 4
cmake --build build/linux-debug --target all_qmllint --parallel 4
cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/linux-release --parallel 4
```

Debug and Release builds pass; both QML lint targets pass. With
`XDG_RUNTIME_DIR=/tmp/sunplayer-vaapi-wayland`,
`WAYLAND_DISPLAY=wayland-sunplayer`, `QT_QPA_PLATFORM=wayland`, and
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`:

```sh
ctest --test-dir build/linux-debug --output-on-failure -LE device --parallel 4
ctest --test-dir build/linux-debug -R '^ffmpeg-first-frame$' --output-on-failure
```

The 33 tests not requiring an audio device pass. The rendering suite reports
27 passed, 0 failed, 8 skipped: three VAAPI cases need hardware, and five cases
are specific to Metal/D3D11. The no-device case additionally decodes and renders
software with the actual unavailable capability before skipping native
execution. Existing MediaSession tests exercise bounded import restart and
position preservation. Two previously deferred MediaSession rows still skip;
this change does not claim to restore them. The two audio-device CTests were
excluded because this environment lacks a running sound service.

A separate `SUNPLAYER_REQUIRE_VAAPI=1` run of
`sunplayer_ffmpeg_first_frame_tests nativeHardwareDecodeDirectImport` exits 2
with two explicit unavailable-VAAPI failures, proving a hardware lane cannot
silently pass by skipping those rows. clang-format and `git diff --check` pass.

## Native host reproduction and resolution

The host outside the sandbox has Intel Arrow Lake-P [8086:7d51] using i915 and
`/dev/dri/renderD128`, Mesa Vulkan 26.0.8-1ubuntu0.3, GNOME Shell
50.1-0ubuntu1.3 and the native `wayland-0` session. The initial strict run failed
both hardware rows at `vaInitialize` with `Input/output error`. Both Intel
media-driver packages and `iHD_drv_video.so` were absent. The user installed
`intel-media-va-driver-non-free` 26.1.2+ds1-1; the same binary then passed without
`LIBVA_DRIVER_NAME`, `LIBVA_DRIVERS_PATH`, or Vulkan ICD overrides.

Run outside the sandbox with the host's ordinary session environment:

```sh
SUNPLAYER_REQUIRE_VAAPI=1 build/linux-debug/tests/sunplayer_ffmpeg_first_frame_tests nativeHardwareDecodeDirectImport continuousVaapiDecodeAndImport
SUNPLAYER_REQUIRE_VAAPI=1 ctest --test-dir build/linux-debug --output-on-failure --parallel 4
```

Focused result: **5 passed, 0 failed, 0 skipped**. NV12 H.264 and P010 Main10
HEVC decode/import/render captures agree with software within the existing
pixel tolerances, report zero known input transfers/copies, reject a stale
graphics generation and classify an unavailable importer. The 24-frame H.264
turnover and seek complete; target 3.125s starts decoded preroll at the 2s
keyframe and drains 16 frames. Device logs consistently report
`renderNode=/dev/dri/renderD128`.

Full result: **35/35 CTests pass**, no CTest exclusions, 34.08s. This includes
the real default-route cubeb sink and production application A/V playback.
The rendering executable reports **30 passed, 0 failed, 5 skipped**, with
only the Metal/D3D11-specific cases skipped. VAAPI rows do not skip because
hardware is explicitly required. The two previously deferred MediaSession
rows still skip inside that separate executable.

The fix for the reproduced user failure was the missing runtime media driver;
no driver-forcing workaround or change to the decode/import implementation
was needed. Driver installation requires a player restart or fresh graphics
generation because capability construction is generation-scoped.

The user subsequently reported that Release playback appears to work after
the driver installation and player restart. The requested Release rebuild
completed with Ninja reporting the target already up to date.

## Remaining native gates

AMD execution, deliberately unsupported modifier/export faults, busy-surface
asynchronous lifetime stress, device-loss injection, and physical runtime
power/performance remain unvalidated. The tests are provided for capture,
replacement, keyframe preroll, stale-generation rejection and unavailable
importer classification. An absent importer is not a forced native-driver
mapping failure. The sequential offscreen turnover case is not a strong
asynchronous GPU stress test. No new hardware-simulation framework was added.
