# Native-Wayland Vulkan presentation foundation

Date: 2026-08-02

Change: this implementation change, based on upstream `01257c6`

## Scope

This report records the Linux behavior that can be exercised in the current
WSLg environment: native-Wayland startup, unmanaged assumed-sRGB selection,
the Qt-owned Vulkan/QRhi/libplacebo presentation path, application chrome,
fullscreen/restoration, QML loading, system-library linkage, and teardown.
It is not evidence for native-GPU behavior, managed color, HDR output, VAAPI,
physical audio, a native compositor matrix, or the deferred application-chrome
window perimeter.

## Environment

Ubuntu 26.04 runs under WSL2 kernel
`6.18.33.2-microsoft-standard-WSL2` with WSLg on `wayland-0`. Neither
`/dev/dri` nor `/dev/dxg` is present. The production path therefore selects
Mesa llvmpipe software Vulkan. WSLg advertises neither the required managed-
color capability set nor xdg-decoration, so startup resolves to unmanaged
assumed-sRGB SDR with application chrome.

Resolved direct dependency versions:

| Dependency | Version |
| --- | --- |
| Qt Core / Wayland Client | 6.10.2 / 6.10.2 |
| FFmpeg libraries | avcodec 62.11.100, avformat 62.3.100, avutil 60.8.100, swresample 6.1.100 |
| libplacebo | 7.360.0 |
| libass | 0.17.4 |
| Vulkan headers/loader | 1.4.341 |
| Wayland / protocols | 1.24.0 / 1.47 |

## Build, test, and install evidence

Fresh Debug and Release configurations with tests enabled and disabled all
complete against this implementation. The primary test/install trees plus the
two complementary matrix trees are:

```sh
cmake -S . -B /tmp/sunroom-linux-repro-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/sunroom-linux-repro-debug --parallel 2

cmake -S . -B /tmp/sunroom-linux-ship-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/sunroom-linux-ship-install
cmake --build /tmp/sunroom-linux-ship-release --parallel 2
cmake --install /tmp/sunroom-linux-ship-release

cmake -S . -B /tmp/sunroom-linux-matrix-debug-off -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF
cmake --build /tmp/sunroom-linux-matrix-debug-off --parallel 2

cmake -S . -B /tmp/sunroom-linux-matrix-release-on -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /tmp/sunroom-linux-matrix-release-on --parallel 2
```

All 24 registered Linux tests and both QML lint targets pass:

```sh
ctest --test-dir /tmp/sunroom-linux-repro-debug \
  --output-on-failure --parallel 2
cmake --build /tmp/sunroom-linux-repro-debug \
  --target all_qmllint --parallel 2
```

The test set includes real FFmpeg media and subtitle fixtures, system
dependency linkage, unmanaged-sRGB versus managed-gamma-2.2 surface selection,
application-chrome layout/state behavior, and
the production executable's packaged QML module. One earlier parallel-eight
run exposed the existing timing sensitivity in the media-session late-video
case; its serial rerun passed, and the final parallel-two run passed 24/24.

Release install and installed-module verification pass:

```sh
cmake --install /tmp/sunroom-linux-ship-release
/tmp/sunroom-linux-ship-install/bin/sunroom \
  --verify-qml --no-log-file
```

Linux keeps the Sunroom QML module embedded and resolves Qt Quick imports from
the system installation. Windows retains the stricter verification that
confines imports and plugins to its deployed application tree. `ldd` on the
installed Linux executable resolves Qt, FFmpeg, libplacebo, libass, Vulkan,
and Wayland libraries from `/usr`; no private copy is installed. Linux cubeb
remains a build/test dependency until the physical audio sink is implemented.

## Production scenario

The installed Release executable passed the bounded video-only scenario with
the Khronos validation layer and synchronization validation enabled:

```sh
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
VK_LAYER_VALIDATE_SYNC=1 \
  /tmp/sunroom-linux-ship-install/bin/sunroom \
  --fullscreen-smoke --no-log-file \
  tests/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv
```

The scenario opened the real Qt Wayland window, selected unmanaged assumed-
sRGB SDR and application chrome, presented the software-decoded fixture,
entered and restored fullscreen from normal and maximized states, exercised
pause/resume and cursor hiding, continued presenting frames, and returned
status zero.

Two separate runs timed out while waiting for cursor-state convergence despite
continuing to present 235 or 240 frames; immediate repeats passed. WSLg also
emits zero-sized maximized/fullscreen configure
warnings, an xdg-surface buffer/configure diagnostic, and a final broken-pipe
message after the scenario reports completion. These observations prevent a
clean native-compositor claim. No WSLg-specific state machine is added; the
native compositor/GPU transition matrix remains open.

## Defects found and corrected

The first install verification exposed that `--verify-qml` enforced the
Windows deployed-QML layout on Linux even though Linux intentionally uses the
embedded application module and system Qt imports. The verification boundary
now follows each platform's actual delivery model and is registered on Linux.

Review also found that swapchain creation honored the surface contract's
`extendedLinearAllowed` value while output-characteristic reconciliation did
not. Reconciliation now applies the same contract, preventing a Linux SDR
surface from being repeatedly discarded when QRhi reports an extended-linear
format that the selected Wayland declaration does not permit.

Ubuntu's LCMS-enabled libplacebo would also have consumed an embedded source
ICC profile even though Sunroom's accepted cross-platform policy defers source
ICC transforms. Rendering now clears both ICC representations on the temporary
libplacebo frame copy on every platform while retaining the decoded profile
bytes and diagnostics on the source frame.

The first explicit synchronization-validation run exposed a write-after-write
hazard between libplacebo's color-attachment write and QRhi's later layout
transition. Queue submission order alone did not supply the required memory
dependency. The target now signals its timeline semaphore after prior QRhi
work before releasing the image to libplacebo, then records one synchronization2
same-layout memory barrier in the current QRhi command buffer before fragment
sampling. The corrected scenario presented 240 frames without a Vulkan
validation message, and its immediate repeat completed with status zero.

## Known gaps

The supported Windows build has not been rerun after this cross-platform
change. Linux cubeb output, VAAPI/DRM PRIME import, native GPU drivers,
color-management-v1 managed SDR/HDR, preferred-output transitions, external
display validation, and a distributable Ubuntu package remain separate gates.
The application-chrome perimeter is explicitly deferred in `docs/DEFERRED.md`;
this report makes no claim that an outline is rendered.
