# AppImage

Build and smoke-test an x86-64 AppImage with Docker:

```sh
packaging/appimage/build.sh
```

Only Docker is needed on the build host. Source is mounted read-only; compilers,
Ubuntu packages and packaging tools live in the builder image. Incremental
build state lives in the `sunplayer-appimage-build` Docker volume. The optional
first argument selects the output directory (default `out/appimage`). Do not
run concurrent builds sharing that volume. Remove the volume to start clean:

```sh
docker volume rm sunplayer-appimage-build
```

The output is `SunPlayer-<VERSION.txt>-ubuntu26.04-x86_64.AppImage` plus its `.sha256` file.
The filename identifies the build/runtime baseline, not universal Linux compatibility.
The existing application icon and CMake install rules are reused. Rootful
Docker runs the build as your UID/GID; rootless Docker uses container root,
which maps to your host user. No host build packages, FUSE mounts, privileged
containers or GPU access are needed to build.

## Runtime contract

The initial baseline is Ubuntu 26.04 x86-64 or a separately validated system
with compatible glibc, C++ runtime and native graphics libraries. This is not
an older-distro or universal-Linux compatibility claim.

The image bundles Qt 6.10, imported QML modules, native Qt Wayland plugins,
FFmpeg, libplacebo, cubeb, libass and their non-system dependencies. The host
provides the Wayland compositor, Vulkan 1.3-capable driver/loader, libva/libva-drm/libva-x11
(build baseline VA-API 1.23), DRM, GBM, EGL/GL and Wayland system libraries.
VAAPI additionally requires the appropriate host media driver and accessible
DRM render node. No vendor drivers, ICD manifests or fixed driver paths are
shipped or selected by the launcher. Hardware import, software-decode fallback
and HDR capability policy remain the player's existing behavior.
The libva-x11 dependency is unconditional Ubuntu FFmpeg linkage, not an X11
presentation path.

Qt plugins and imported QML come from the bundle, not the host's Qt SDK.
The matching SVG icon engine and GTK/desktop-portal platform themes are
included; icon assets and desktop settings come from the host. The launcher
respects `QT_QPA_PLATFORMTHEME`. Otherwise it checks the session portal's
FileChooser interface (one-second timeout) and selects the portal theme when
available. Without it, Qt keeps its normal desktop theme selection, including
GTK on GNOME. Window controls keep the player's existing design and use
system symbolic icons where available.
The QML scanner receives `src/app` even though application QML is embedded.
Wayland helpers are copied explicitly; the default xcb platform plugin is
removed. Ubuntu package copyright files, common license texts and the full
builder package/version inventory are included under `usr/share/`.

The Ubuntu base digest and AppImage tool SHA-256 values are checked in.
Ubuntu apt updates remain enabled: this is not a bit-for-bit reproducible or
frozen dependency build. Upstream tools use mutable continuous URLs, so changed
downloads fail checksum validation until `tools.sha256` is reviewed and updated.

## Verification and CI

The build command also creates a separate smoke image containing the declared
host graphics libraries, Weston, lavapipe and a Pulse null sink. It has no Qt or
QML installation. PulseAudio brings some FFmpeg libraries transitively, so the
smoke also checks that the application resolves its Qt and media libraries
from the bundle. A test-only Qt probe renders all four window-control icons
using host Adwaita assets and the bundled plugins. It stays in the build
volume, outside the AppImage. The packaged `--verify-qml` and `--playback-smoke`
scenarios run there against the existing A/V fixture. Extraction avoids needing
FUSE in Docker; the runtime payload and AppRun are unchanged.

On a native Wayland desktop, verify the output separately:

```sh
cd out/appimage
sha256sum --check SunPlayer-*.AppImage.sha256
chmod +x SunPlayer-*.AppImage
./SunPlayer-<version>-ubuntu26.04-x86_64.AppImage --verify-qml
./SunPlayer-<version>-ubuntu26.04-x86_64.AppImage --debug-log /path/to/video.mkv
```

If FUSE is unavailable, use the runtime's `--appimage-extract-and-run` option.
Check diagnostics for the intended Vulkan GPU, VAAPI and DRM PRIME import where
supported, and play/seek/fullscreen with audio and subtitles. Test on Intel and
AMD and verify NVIDIA Vulkan presentation without claiming a new NVIDIA decode
backend. HDR needs a capable compositor/display. The Docker smoke proves
software-hosted packaging and playback, not those hardware/display behaviors.

CI invokes the same build command and uploads the AppImage and checksum. The
generated release notes state the Ubuntu 26.04 baseline and host-library/driver
requirements alongside the automatically generated change list. The
AppImage job follows Windows because Windows currently owns creation of the
unpublished release commit and Git bundle. In release mode it checks out that
exact bundle revision before building. Publication waits for both jobs and
attaches the AppImage/checksum alongside the existing Store upload; re-runs
retain existing nonempty assets and upload missing ones. An existing AppImage
or checksum must match the current artifact byte-for-byte before recovery can
continue, preventing a rebuilt checksum from describing an older binary.
No separate updater,
signing, release workflow or automatic desktop installation is added.

Current delivery evidence is recorded in the
[AppImage delivery plan](../../docs/plans/cross-cutting/2026-10-17-docker-appimage.md).
