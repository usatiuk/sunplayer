# Ubuntu system dependency foundation

Date: 2026-08-02

Change: this implementation change, deeply reconciled with upstream `5357d96`

## Environment

Ubuntu 26.04 under WSL, GCC 15.2, CMake/Ninja. This environment has WSLg but
no usable `/dev/dri` device, so the results below are compile, link, generated-
protocol, platform-neutral behavior, and QML evidence only.

Resolved direct dependencies:

| Dependency | Version |
| --- | --- |
| Qt | 6.10.2 |
| FFmpeg libraries | avutil 60.8.100, swresample 6.1.100, avcodec 62.11.100, avformat 62.3.100 |
| libplacebo | 7.360.0 |
| libass | 0.17.4 |
| cubeb | Ubuntu snapshot `0.0~git20250401.975a727+ds` |
| Vulkan | 1.4.341 |
| Wayland / protocols | 1.24.0 / 1.47 |
| VA-API | 1.23.0 |
| DRM | 2.4.131 |

## Commands and results

The following clean configurations and complete builds passed:

```sh
cmake -S . -B <debug-tests> -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build <debug-tests> --parallel 8

cmake -S . -B <release-tests> -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build <release-tests> --parallel 8

cmake -S . -B <debug-no-tests> -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF
cmake --build <debug-no-tests> --parallel 8

cmake -S . -B <release-no-tests> -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build <release-no-tests> --parallel 8
```

Debug and Release each passed all 22 registered Linux CTests:

```sh
ctest --test-dir <debug-tests> --output-on-failure --parallel 8
ctest --test-dir <release-tests> --output-on-failure --parallel 8
```

QML lint passed without diagnostics after synchronizing the shell test type
with the production `videoColorPolicy` property:

```sh
cmake --build <debug-tests> --target all_qmllint --parallel 8
```

The Linux dependency test compiled Qt-generated client code from the system
`staging/color-management/color-management-v1.xml` and linked the actual
`Vulkan::Vulkan`, `Wayland::Client`, `PkgConfig::VAAPI`, `PkgConfig::DRM`, and
`Qt6::WaylandClientPrivate` targets without opening a display or device.
Release `readelf -d` verification shows `libcubeb.so.0` in the cubeb test's
`NEEDED` entries and `libvulkan.so.1`, `libwayland-client.so.0`,
`libva-drm.so.2`, and `libdrm.so.2` in the Linux platform test's entries.
After rebasing over embedded subtitle playback, the same matrix compiles the
production subtitle sources and passes the shared subtitle-source, real
FFmpeg ASS/SubRip/PGS decode, session, QML menu, and system-libass rendering
boundaries. Release linkage shows `libass.so.9` in both the dependency test
and production executable.

A Release install-tree check initially exposed Qt's cross-platform deployment
script trying to copy and rewrite Ubuntu's system QML plugins. Linux now skips
that Windows-oriented deployment step and installs only project-owned
artifacts, preserving the system-package ownership boundary. A clean install
then succeeded with the executable and bundled Lucide third-party license in
the expected GNU install directories; its dynamic dependencies resolve from
`/usr`. Project-level packaging and license installation remain deferred.

## Defects found and corrected

Release testing exposed an FFmpeg frame-buffer allocation hidden inside
`Q_ASSERT`; the allocation now executes in every build and fails immediately
if test setup cannot establish its invariant. Ubuntu cubeb does not expose the
Windows overlay's backend-enumeration extension, so the shared dependency test
uses common public ABI symbols on Linux and leaves selected-backend reporting
to the production audio slice.
The upstream subtitle menu also exposed an implicit outer-component lookup to
Qt 6.10's linter; declaring bound component behavior makes the delegate's
scope explicit and restores warning-free QML lint.

## Review disposition

Independent correctness, dependency/delivery, and simplicity reviews found
that address-only ABI assertions were optimized away in Release. The tests now
store each symbol through a volatile function pointer, and the Release dynamic
sections above prove that the intended libraries remain linked. The delivery
review also added explicit FFmpeg VAAPI/DRM PRIME public-header coverage and
the missing `avutil_version()` runtime comparison. Focused re-review caught
that those new headers were initially included on Windows, whose pinned
FFmpeg deliberately omits VAAPI; the includes now follow their Linux-only
assertions, preserving the existing Windows dependency contract.

The simplicity review removed three unnecessary constraints: LCMS is observed
rather than required, the Wayland/VA-API/DRM package floors no longer mirror
one Ubuntu snapshot without an API reason, and Qt's Wayland components remain
the single discovery path for their exported client/scanner targets. The
Ubuntu package versions are still recorded as evidence. A reproducible apt
package list was added to the build subsystem documentation.
Focused delivery review additionally made the `qt6-wayland` runtime package
explicit because Ubuntu's Wayland development packages do not depend on the
native client QPA plugin.

## Known gaps

The supported Windows configuration has not yet been rerun. The Linux Vulkan
backend, Qt/Wayland runtime capability checks, cubeb sink, VAAPI device/import
path, package metadata and installed-player launch, native compositor behavior,
real GPU validation, and HDR display evidence remain separate checklist gates.
Ubuntu's Qt package also emits warnings for unavailable static QML plugin
targets; dynamic QML loading and the QML component test pass, so no local
workaround was added.
The subtitle renderer itself compiles on Linux and the libass boundary runs,
but QRhi subtitle-surface capture remains paired with the future Linux Vulkan
graphics domain.
