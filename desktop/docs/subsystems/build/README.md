# Build and packaging subsystem

## Status

The CMake project builds the Windows player and the shared player sources on
Ubuntu 26.04. Windows retains pinned vcpkg dependencies. Linux uses only system
Qt, FFmpeg, libplacebo, cubeb, libass, Vulkan, Wayland, VAAPI, and DRM
packages. Embedded subtitle discovery, FFmpeg decoding, libass/bitmap
rendering, and shared subtitle state are integrated. The Linux native graphics
and audio implementations are not present yet, so this remains a build
foundation rather than a runnable Linux player. Complete distributable
packaging is not integrated.

The currently validated configuration is:

| Requirement | Current value |
| --- | --- |
| Build system | CMake 3.22 or newer |
| Languages | C11 for the libplacebo/FFmpeg adapter; C++20 elsewhere |
| Application compiler | MSVC in a Visual Studio developer environment |
| Windows dependency compiler | Visual Studio clang-cl through a project-local vcpkg triplet |
| Qt | Exactly 6.11.1 |
| libplacebo | Exactly 7.360.1, D3D11 enabled |
| FFmpeg | Exactly 8.1.2, shared avutil/swresample/avcodec/avformat with zlib |
| libass | Windows registry baseline package; Linux system 0.17.4 |
| cubeb | Upstream commit `ef47ae581df7c2f76058d554b3edde17f9ee7cba` |
| Graphics backend | Windows D3D11 through QRhi |
| Generator in the local configured tree | Ninja |

The validated Linux foundation uses GCC 15.2, Qt 6.10.2, FFmpeg 8.0.1 ABI
libraries, libplacebo 7.360.0, libass 0.17.4, the Ubuntu cubeb snapshot,
Vulkan 1.4.341, Wayland 1.24 with wayland-protocols 1.47, VA-API 1.23, and DRM
2.4.131.

The Visual Studio C++ Clang tools component is required for Windows dependency
builds. The application and installed Qt package remain MSVC-built; clang-cl
supplies the same Windows ABI and runtime conventions for the vcpkg dependency
graph.

Local absolute Qt, CMake, and developer-shell paths remain machine-specific and
belong in ignored local agent or IDE configuration.

## Dependency management

Windows uses vcpkg manifest mode. The repository owns:

* `vcpkg.json`, including the pinned registry baseline and requested features.
* `vcpkg-configuration.json`, including project-local overlay ports and
  triplets.
* `vcpkg-ports/libplacebo`, because the pinned registry does not provide the
  required package.
* `vcpkg-ports/spirv-cross-c-shared`, because the registry's static-only
  SPIRV-Cross port cannot satisfy libplacebo's shared C dependency.
* `vcpkg-ports/cubeb`, because the registry port is older than the reviewed
  audio timing and recovery behavior. The overlay also corrects upstream's
  installed CMake target so consumers receive its public include directory and
  makes disabled WASAPI device switching fail closed on every reconfigure
  event.
* `cmake/vcpkg/triplets/x64-windows-clangcl.cmake` and its chainloaded
  toolchain, so compiler identity participates in vcpkg's package ABI and
  binary-cache key.
* `cmake/SunroomFFmpeg.cmake`, which discovers the vcpkg module before Qt can
  introduce its case-variant finder and wraps component import libraries and
  DLLs in configuration-aware imported targets.
* `cmake/SunroomLibass.cmake`, which exposes vcpkg's pkg-config package on
  Windows and the system pkg-config package on Linux through one project-local
  target without duplicating libass's dependency graph.

The vcpkg executable itself is supplied by Visual Studio rather than cloned
into the repository. CMake uses an explicitly supplied toolchain when present,
then falls back to `VCPKG_ROOT` or the Visual Studio vcpkg discovered from a
sourced developer environment. Windows configurations default to the
`x64-windows-clangcl` dependency triplet while preserving an explicit caller
override.

Manifest installation output lives under the configured build directory's
`vcpkg_installed/` tree. Source downloads and binary archives use vcpkg's
normal per-user cache. Neither path registers libraries globally or modifies
the system `PATH`.

Linux does not auto-select vcpkg and needs no vcpkg variables. It consumes the
distribution's CMake config packages and pkg-config imported targets directly;
an explicitly supplied CMake toolchain remains honored, but no Linux vcpkg
configuration is currently claimed or tested. FFmpeg is wrapped by the same
`sunroom_ffmpeg` target on both platforms because Windows needs explicit DLL
staging while Linux consumes the system pkg-config target. Linux configures
only when the expected FFmpeg 8 ABI majors, libplacebo API 360 family, Qt
6.10 family, libass, Vulkan 1.2+, Wayland client/scanner and
color-management-v1 XML, VA-API DRM, DRM, and `cubeb::cubeb` are present.

The Ubuntu 26.04 reference development install is:

```sh
sudo apt install \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-private-dev \
  qt6-declarative-dev qt6-declarative-private-dev \
  qt6-declarative-dev-tools \
  qt6-wayland qt6-wayland-dev qt6-wayland-private-dev \
  qt6-shadertools-dev qt6-shader-baker spirv-tools \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-dialogs qml6-module-qtquick-layouts \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
  libplacebo-dev libcubeb-dev libass-dev libvulkan-dev \
  libwayland-dev libwayland-bin wayland-protocols \
  libva-dev libdrm-dev
```

These are the direct build/test requirements plus the native Wayland QPA
runtime for the accepted Linux port slices. GPU drivers, VA drivers,
validation layers, and compositor/runtime packages are environment-specific
validation dependencies rather than configure-time requirements.

The initial libplacebo feature set enables D3D11, Shaderc, the shared
SPIRV-Cross C API, and libplacebo's built-in DOVI handling. The optional
external libdovi dependency, Vulkan, and OpenGL remain disabled. glslang and
SPIRV-Tools are transitive implementation dependencies of the Shaderc build,
not enabled Sunroom graphics backends. The port also stages libplacebo's
pinned Vulkan-Headers source submodule because its disabled Vulkan stub and
public header require the types; this is not a Vulkan SDK, loader, runtime, or
enabled backend. The compiler and dependency experiments behind this choice
are recorded in
[the Windows dependency-build research note](../../research/2026-07-29-libplacebo-windows-dependency-build.md).

The FFmpeg dependency uses the official registry port with `avcodec`,
`avformat`, `swresample`, and `zlib`; `avutil` is core. zlib is required for
Matroska tracks using `ContentCompression`, including real embedded PGS files.
The Windows port enables
D3D11VA, D3D12VA, DXVA2, and Media Foundation without a separate manifest
feature. It excludes Vulkan, swscale, filter/device libraries, command-line
tools, vendor SDKs, and external codec libraries in this slice.

The cubeb overlay is explicitly Windows-only and builds the native WASAPI
backend with tests, tools, and Rust backends disabled. `BUNDLE_SPEEX=OFF`
prefers an external SpeexDSP package, but cubeb retains its embedded Speex
fallback when none is present; the overlay ships both notices. This does not
require a Rust toolchain on Windows. macOS and Linux backend packaging must be
decided and validated on those platforms; current upstream choices may require
pinned Rust submodules and Cargo inputs there. vcpkg downloads and binary
caches remain project-build or per-user cache state and do not install cubeb
system-wide.

The runtime patch is intentionally narrower than a Sunroom-owned WASAPI
backend: it only prevents cubeb from silently replacing an `IAudioClient` when
the stream requested disabled switching. Any upstream update must check whether
equivalent behavior landed before refreshing or dropping the patch.

## Qt dependency

The executable currently requires:

* `Qt6::Quick`.
* `Qt6::QuickControls2`.
* `Qt6::QuickDialogs2`.
* `Qt6::ShaderTools`.
* `Qt6::GuiPrivate` for QRhi.

Qt is pinned to exactly 6.11.1 because QRhi is a private API with limited
compatibility guarantees. A Qt upgrade is an explicit maintenance task that
must rebuild and runtime-test redirected Quick rendering, swapchain HDR state,
texture and shader resources, surface loss, and device recovery.

On Linux the corresponding contract is Qt `>=6.10,<6.11` and additionally
requires `Qt6::WaylandClientPrivate` plus Qt's Wayland scanner tools. Private
Base, Declarative, and Wayland targets therefore resolve from one distro Qt
family. The dependency test uses Qt's standard protocol generator against the
system color-management-v1 XML; the production display adapter will consume
the same generated boundary when implemented.

## Targets and resources

The project defines the `sunroom` executable plus three production static
libraries: `sunroom_audio` for PCM and sink contracts, `sunroom_media` for the
FFmpeg frame/decode boundary, and `sunroom_video_pipeline` for the shared
graphics, libplacebo producer/importer, target, and final-compositor code. The
application and integration tests link these same compiled libraries rather
than maintaining parallel source lists.

`qt_add_qml_module()` packages `src/app/Main.qml`, `AppShell.qml`,
`pages/VideoPage.qml`, `pages/PlayerPage.qml`, and `pages/HdrLabPage.qml` under
the `Sunroom` module.
Qt's cross-target foreign-type generation exposes QML-marked source contracts
from `sunroom_video_pipeline` without compiling their QObject metadata into the
application again. `qt_add_shaders()` precompiles and packages the fullscreen
vertex, diagnostic video producer, and compositor shaders from
`src/presentation/shaders/` under `/shaders`.

Production sources are grouped under `src/app`, `src/audio`, `src/graphics`,
`src/media`, `src/playback`, `src/platform`, `src/presentation`, and
`src/video`. Focused tests currently live under `tests/unit/audio`,
`tests/unit/media`, `tests/unit/playback`,
`tests/unit/presentation`, `tests/unit/ui`, `tests/unit/video`,
`tests/integration/audio`, `tests/integration/media`,
`tests/integration/presentation`, `tests/integration/ui`, and
`tests/integration/video`; new trees should follow concrete execution classes
rather than speculative subsystem placeholders.

The target links Windows Runtime and dispatcher support libraries only on
Windows:

* `CoreMessaging`.
* `RuntimeObject`.
* `WindowsApp`.

The application is marked as a Windows GUI executable and a macOS bundle at the
CMake target level, but the source currently implements only Windows graphics
startup. Successful CMake generation on another platform would not imply a
supported player.

## Installation

CMake installs the executable or bundle through `GNUInstallDirs`. Linux keeps
Qt, QML modules, and native/media libraries system-owned and does not run Qt's
deployment copier; the eventual distro package must express their runtime
package dependencies. On Windows, vcpkg's app-local dependency walker installs the executable's complete
transitive runtime-DLL set before `qt_generate_deploy_qml_app_script()` supplies
the current Qt deployment step with:

* No compiler-runtime deployment.
* No translation deployment.
* No unsupported-platform configuration error.

Build-tree application and test targets use vcpkg's app-local walker and CMake's
`TARGET_RUNTIME_DLLS`; the install path uses vcpkg's corresponding
`x_vcpkg_install_local_dependencies()` helper. That executable-import traversal
includes libass, its shaping/font libraries, FFmpeg, zlib, cubeb, libplacebo,
and graphics dependencies even when pkg-config does not expose them as CMake
runtime targets. Sunroom's config-aware FFmpeg component targets also make its
four DLLs participate in CMake's standard traversal. This prevents loader
dialogs and makes dependency boundaries reproducible during development.

This remains scaffolding rather than a complete distributable package. It does
not yet define:

* Final third-party notices, corresponding-source/build-recipe handling, and
  codec licensing/patent policy for FFmpeg and libass.
* A complete third-party notice and source-offer workflow for shipped
  libplacebo and other LGPL components.
* Windows installer or portable layout.
* macOS signing, notarization, and bundle policy.
* Wayland Linux package formats and compositor/runtime requirements. X11 and
  XWayland compatibility are not packaging targets.
* Runtime feature and dependency-version reporting.
* Clean-machine package verification.

## Testing integration

CTest and Qt Test are configured only under `BUILD_TESTING`, keeping test-only
dependencies out of production-only configurations. Separate test executables
cover presentation-target policy, active viewport state, real QML shell
publication, rendered-video surface validity/reuse, and a real D3D11 QRhi
producer/compositor capture. A shared dependency integration test verifies the
platform-specific libplacebo version and feature contract plus real log
create/destroy lifecycle. Windows requires its pinned D3D11/Shaderc
configuration; Linux requires API 360, Vulkan, a shader compiler, built-in
DOVI, and records LCMS availability.

A separate FFmpeg dependency test verifies the four selected DLLs, pinned
major versions, D3D11VA availability, native H.264/HEVC decoders, and the
absence of Vulkan and swscale on Windows. The same source verifies the system
ABI majors plus VAAPI and DRM hardware-device support on Linux. A shared cubeb
dependency test compiles and links its common public C ABI without requiring
COM, a live sound server, or an available device. A Linux-only native
dependency test links Vulkan, Wayland client, VA-API DRM, DRM, and Qt's private
Wayland API while compiling generated color-management-v1 client code. The
shared libass dependency test renders the same embedded-font ASS cue on both
platforms, and the platform-neutral FFmpeg/media tests exercise ASS, converted
SubRip, and ordinary/zlib-compressed PGS fixtures on Linux.
The FFmpeg integration targets then
exercises real image and continuous compressed-video demux, software and
D3D11VA decode, libplacebo upload, and final QRhi composition. Additional focused targets cover
aspect fitting, active-source routing, media-session cancellation/generation,
the real Player/HDR-Lab QML shell, one-pass A/V decode with libswresample, and
the bounded controlled audio sink.

On Windows, the application and each test target stage their transitive runtime
DLLs beside the executable with CMake's `TARGET_RUNTIME_DLLS` support. The
build-tree application then runs Qt's `windeployqt --qmldir` so its platform
plugin, imported QML modules, Controls style libraries, and Dialogs support are
staged as one consistent runtime. A relative build-tree `qt.conf` restricts
plugin and QML lookup to that deployed tree instead of silently using the Qt
SDK. This is a developer convenience, not the installed package layout, and
prevents loader or QML-import dialogs at startup.
The QML component test additionally stages Qt's offscreen platform plugin,
selects the Basic Controls style, and receives the configured Qt binary and
QML-module roots through its CTest/CMake harness. Installed artifacts use Qt's
generated deployment script and their deployed relative paths.

See [../testing/PLAN.md](../testing/PLAN.md).

## Verification

The complete Debug build and all 28 registered CTest cases pass in the current
Windows/MSVC/Ninja environment after initializing the Visual Studio developer
environment. The dependency graph is built under the project-local clang-cl
triplet; the Sunroom executable remains MSVC-built. This includes the cubeb
ABI/backend check, FFmpeg libswresample boundary, one-pass synchronized decode,
bounded controlled sink, real D3D11VA decode/import, and GPU compositor tests.
A bounded application scenario additionally opens an audio-first fixture in
the built executable and observes live default-device clock progress plus two
distinct video revisions reaching the swapchain before automatic exit.
A build-local install-tree generation also succeeds and stages the expected Qt
runtime plus libass, FreeType, FriBidi, HarfBuzz, zlib, cubeb, libplacebo,
SPIRV-Cross, and selected FFmpeg DLLs.

A prior build-tree GUI startup liveness smoke also passed with the configured
Qt runtime available; the harness terminated the process after four seconds
without user interaction.

On Ubuntu 26.04 under WSL, clean Debug and Release builds pass with
`BUILD_TESTING` both enabled and disabled. All 22 registered Linux tests and
QML lint pass, including shared embedded-subtitle decoding/state and real
system-libass rendering. A Release install-tree generation also succeeds and
keeps system libraries under distribution ownership. This proves compilation,
install mechanics, and platform-neutral behavior only: the Linux Vulkan
presentation backend, real Wayland protocol/runtime behavior, cubeb sink,
hardware decoding, installed-player launch, and native-hardware HDR validation
remain unimplemented or untested. Windows has not yet been rerun after the
cross-platform CMake change and remains an explicit regression gate.
