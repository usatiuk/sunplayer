# Build and packaging subsystem

## Status

The CMake project builds the Windows player, an Apple-Silicon macOS player,
and a runnable native-Wayland Vulkan player on Ubuntu 26.04. Windows and macOS
use pinned vcpkg libraries; macOS uses the Qt 6.11.1 Online Installer tree.
Linux uses only system Qt, FFmpeg, libplacebo, cubeb, libass, Vulkan, Wayland,
VAAPI, and DRM packages. Embedded subtitle discovery, FFmpeg decoding,
libass/bitmap rendering, shared subtitle state, Wayland capability inventory,
the Linux Vulkan graphics domain, and system-cubeb output are integrated.
Native Linux route-change/recovery evidence, VAAPI/DRM PRIME, managed HDR, and
complete distributable packaging remain open. The macOS development build,
Metal/MoltenVK presentation, VideoToolbox, AudioUnit, and shared tests are
validated on the available Apple M2/macOS 26 host; the macOS 13 dependency
archive floor and self-contained bundle remain packaging gates.

The root-level GitHub Actions workflow is configured for Ubuntu 26.04 system
dependencies and the existing Windows Qt/vcpkg contract. It has been validated
locally as workflow configuration, but no successful GitHub-hosted run has yet
established hosted platform evidence.

The currently validated Windows configuration is:

| Requirement | Current value |
| --- | --- |
| Build system | CMake 3.22 or newer |
| Languages | C11 for the libplacebo/FFmpeg adapter; C++20 elsewhere |
| Application compiler | MSVC in a Visual Studio developer environment |
| Windows dependency compiler | Visual Studio clang-cl through a project-local vcpkg triplet |
| Qt | Exactly 6.11.1 |
| libplacebo | Exactly 7.360.1, D3D11 enabled |
| FFmpeg | Exactly 8.1.2, shared avutil/swresample/avcodec/avformat with zlib |
| libass | Windows registry baseline package |
| cubeb | Upstream commit `ef47ae581df7c2f76058d554b3edde17f9ee7cba` |
| Graphics backend | D3D11 through QRhi |
| Generator in the local configured tree | Ninja |

The validated Linux foundation uses GCC 15.2, Qt 6.10.2, FFmpeg 8.0.1 ABI
libraries, libplacebo 7.360.0, libass 0.17.4, the Ubuntu cubeb snapshot,
Vulkan 1.4.341, Wayland 1.24 with wayland-protocols 1.47, VA-API 1.23, and DRM
2.4.131.

The validated macOS development configuration uses Apple Clang 16, ordinary
CMake/Ninja, Qt 6.11.1 from the Online Installer, the stock vcpkg `arm64-osx`
triplet, FFmpeg 8.1.2 with VideoToolbox, libplacebo 7.360.1 with Vulkan,
MoltenVK 1.4.2, the pinned cubeb AudioUnit backend, and libass. It is a host
validation claim, not yet a distributable macOS 13 compatibility claim.

The Visual Studio C++ Clang tools component is required for Windows dependency
builds. The application and installed Qt package remain MSVC-built; clang-cl
supplies the same Windows ABI and runtime conventions for the vcpkg dependency
graph.

Local absolute Qt, CMake, and developer-shell paths remain machine-specific and
belong in ignored local agent or IDE configuration.

## Dependency management

Windows and macOS use vcpkg manifest mode. The repository owns:

* `vcpkg.json`, including the pinned registry baseline and requested features.
* `vcpkg-configuration.json`, including project-local overlay ports and
  triplets.
* `vcpkg-ports/libplacebo`, because the pinned registry does not provide the
  required D3D11/Vulkan platform configurations.
* `vcpkg-ports/moltenvk`, which packages the pinned macOS Vulkan runtime and
  CMake target used by libplacebo.
* `vcpkg-ports/spirv-cross-c-shared`, because the registry's static-only
  SPIRV-Cross port cannot satisfy libplacebo's shared C dependency.
* `vcpkg-ports/cubeb`, because the registry port is older than the reviewed
  audio timing and recovery behavior. The overlay also corrects upstream's
  installed CMake target so consumers receive its public include directory.
* `cmake/vcpkg/triplets/x64-windows-clangcl.cmake` and its chainloaded
  toolchain, so compiler identity participates in vcpkg's package ABI and
  binary-cache key.
* `cmake/SunPlayerFFmpeg.cmake`, which discovers the vcpkg module before Qt can
  introduce its case-variant finder and wraps component import libraries and
  DLLs in configuration-aware imported targets.
* `cmake/SunPlayerLibass.cmake`, which exposes vcpkg's pkg-config package on
  Windows and the system pkg-config package on Linux through one project-local
  target without duplicating libass's dependency graph.
* the manifest's Windows-only `pkgconf` host dependency and explicit
  `x64-windows` host triplet, which let CMake discover vcpkg's libass metadata
  through vcpkg's standard host-tool path.

On Windows the vcpkg executable may be supplied by Visual Studio. On macOS the
developer supplies a normal vcpkg checkout. CMake uses an explicitly supplied
toolchain when present, then falls back to `VCPKG_ROOT` or the Visual Studio
vcpkg discovered from a sourced developer environment. Windows configurations
default to `x64-windows-clangcl`; macOS defaults to stock `arm64-osx`; both
preserve an explicit caller override.

Manifest installation output lives under the configured build directory's
`vcpkg_installed/` tree. Source downloads and binary archives use vcpkg's
normal per-user cache. Neither path registers libraries globally or modifies
the system `PATH`.

Linux does not auto-select vcpkg and needs no vcpkg variables. It consumes the
distribution's CMake config packages and pkg-config imported targets directly;
an explicitly supplied CMake toolchain remains honored, but no Linux vcpkg
configuration is currently claimed or tested. FFmpeg is wrapped by the same
`sunplayer_ffmpeg` target on both platforms because Windows needs explicit DLL
staging while Linux consumes the system pkg-config target. Linux configures
only when the expected FFmpeg 8 ABI majors, libplacebo API 360 family, Qt
6.10 family, libass, Vulkan 1.3+, Wayland client/scanner and
color-management-v1 XML, VA-API DRM, DRM, and `cubeb::cubeb` are present.

The Ubuntu 26.04 reference development install is:

```sh
sudo apt install \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-private-dev \
  qt6-declarative-dev qt6-declarative-private-dev \
  qt6-declarative-dev-tools \
  qt6-wayland qt6-wayland-dev qt6-wayland-private-dev \
  qt6-shadertools-dev qt6-shader-baker qt6-svg-plugins spirv-tools \
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

The Windows libplacebo feature set enables D3D11, Shaderc, the shared
SPIRV-Cross C API, and libplacebo's built-in DOVI handling. The optional
external libdovi dependency, Vulkan, and OpenGL remain disabled. glslang and
SPIRV-Tools are transitive implementation dependencies of the Shaderc build,
not enabled SunPlayer graphics backends. The port also stages libplacebo's
pinned Vulkan-Headers source submodule because its disabled Vulkan stub and
public header require the types; this is not a Vulkan SDK, loader, runtime, or
enabled backend. The compiler and dependency experiments behind this choice
are recorded in
[the Windows dependency-build research note](../../research/2026-07-29-libplacebo-windows-dependency-build.md).

The Windows overlay patches libplacebo's allocator to preserve 16-byte
alignment in `NDEBUG` builds. Win64's `max_align_t` expresses only 8-byte
alignment even though the CRT allocator provides 16-byte alignment and
libplacebo's D3D11 pass structures require it. The patch applies that
requirement consistently to allocator payloads and public/private object
offsets, with a compile-time layout check; other platforms retain the upstream
`max_align_t` alignment.

The FFmpeg dependency uses the official registry port with `avcodec`,
`avformat`, `swresample`, and `zlib`; `avutil` is core. zlib is required for
Matroska tracks using `ContentCompression`, including real embedded PGS files.
The Windows port enables
D3D11VA, D3D12VA, DXVA2, and Media Foundation without a separate manifest
feature. It excludes Vulkan, swscale, filter/device libraries, command-line
tools, vendor SDKs, and external codec libraries in this slice.

The project-local cubeb overlay builds the native WASAPI backend on Windows and
AudioUnit on macOS, with tests, tools, and Rust backends disabled.
`BUNDLE_SPEEX=OFF`
prefers an external SpeexDSP package, but cubeb retains its embedded Speex
fallback when none is present; the overlay ships both notices. This does not
require a Rust toolchain on Windows. Linux instead consumes the distribution's
system cubeb package; WSLg verifies that package's Pulse backend through the
real sink and application. macOS bundle deployment remains deferred. vcpkg
downloads and binary caches remain project-build or per-user cache state and
do not install cubeb system-wide.

An ordinary macOS development configuration is:

```sh
cmake -S . -B build/macos-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/macos \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build/macos-debug
ctest --test-dir build/macos-debug --output-on-failure
```

The project defaults Apple-Silicon dependency builds to vcpkg's stock
`arm64-osx` triplet. No custom triplet, wrapper CMake, or Homebrew-installed
project library is required. The Qt Online Installer tree and vcpkg checkout
remain developer-supplied; this command does not define the later package.

## Qt dependency

The executable currently requires:

* `Qt6::Widgets` for platform-styled About and total-presentation/startup
  fallback dialogs.
* `Qt6::Quick`.
* `Qt6::QuickControls2`.
* `Qt6::QuickDialogs2`.
* `Qt6::ShaderTools`.
* `Qt6::GuiPrivate` for QRhi.

Qt is pinned to exactly 6.11.1 because QRhi is a private API with limited
compatibility guarantees. A Qt upgrade is an explicit maintenance task that
must rebuild and runtime-test redirected Quick rendering, swapchain HDR state,
texture and shader resources, surface loss, and device recovery.

Windows package generation also requires the matching Qt Sources archives for
`qtbase`, `qtdeclarative`, `qtimageformats`, and `qtsvg`; their `LICENSES`
directories are the offline notice inputs paired with the local module SPDX
metadata. Hosted CI caches those sources with the binary Qt tree and verifies
the required Qt build tools before configuration. SPDX paths identify the
owning module. SPDX files are not copied into the package.

On Linux the corresponding contract is Qt `>=6.10,<6.11` and additionally
requires `Qt6::DBus`, `Qt6::WaylandClientPrivate`, and Qt's Wayland scanner
tools. Private Base, Declarative, and Wayland targets therefore resolve from
one distro Qt family. Qt's standard protocol generator compiles the system
color-management-v1 XML into the production Linux capability inventory and
the focused dependency boundary.

## Targets and resources

The project defines the `sunplayer` executable plus shared production static
libraries: `sunplayer_diagnostics`, `sunplayer_audio`, `sunplayer_media`, and
`sunplayer_video_pipeline`. Linux additionally builds `sunplayer_linux_platform`
for the native Wayland/Vulkan boundary. The application and integration tests
link these same compiled libraries rather than maintaining parallel source
lists.

`qt_add_qml_module()` packages `src/app/Main.qml`, `AppShell.qml`,
`pages/VideoPage.qml`, `pages/PlayerPage.qml`, and `pages/HdrLabPage.qml` under
the `SunPlayer` module together with the optional Wayland application chrome and
its explicitly listed Lucide fallbacks.
Qt's cross-target foreign-type generation exposes QML-marked source contracts
from `sunplayer_video_pipeline` without compiling their QObject metadata into the
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
CMake target level. Windows, Apple-Silicon macOS, and native-Wayland Linux all
have production graphics startup.

## Installation

CMake installs the executable or bundle through `GNUInstallDirs`. Linux embeds
the SunPlayer QML module in the executable while keeping Qt's imported QML
modules and native/media libraries system-owned; it does not run Qt's
deployment copier. The eventual distro package must express those runtime
package dependencies. On Windows, vcpkg's app-local dependency walker installs
the executable's complete transitive runtime-DLL set before
`qt_generate_deploy_qml_app_script()` supplies the current Qt deployment step
with:

* No translation deployment.
* No unsupported-platform configuration error.
* No app-local software OpenGL, D3D, or DXC shader-compiler deployment.

SunPlayer's Store package intentionally requires Windows 11 24H2 (build 26100)
or newer. Its Windows backend is D3D11-only, and `d3dcompiler_47.dll` is an
operating-system component. Both Qt's
D3D11 QRhi path and libplacebo prefer that serviced System32 copy; SunPlayer does
not expose Qt's D3D12/Shader Model 6 path that would use the separately
distributed `dxcompiler.dll` and `dxil.dll`. The deployment flags therefore
exclude all three redundant app-local copies instead of deleting them after
staging.

The Qt deployment script generates the Windows install tree's relative
`bin/qt.conf`. After a successful hosted Windows Debug build, lint, and
deterministic test subset, CI builds and installs a separate Release
application and runs its `--verify-qml` probe. That probe obtains its deployed
import directory from `QLibraryInfo`, so the same boundary follows the build
tree's local `qt.conf` and Qt's standard install layout without duplicating
either path. The complete Release install tree is the authoritative input for
both CI artifacts described below. CI rejects a tree containing the excluded
D3D or DXC compiler DLLs. Qt's compiler-runtime deployment is disabled because
it would place a dead `vc_redist.x64.exe` installer in the MSIX payload. The
manifest instead declares Microsoft's Store-serviced
`Microsoft.VCLibs.140.00.UWPDesktop` framework at minimum version
`14.0.33728.0`.

Build-tree application and test targets use vcpkg's app-local walker and CMake's
`TARGET_RUNTIME_DLLS`; the install path uses vcpkg's corresponding
`x_vcpkg_install_local_dependencies()` helper. That executable-import traversal
includes libass, its shaping/font libraries, FFmpeg, zlib, cubeb, libplacebo,
and graphics dependencies even when pkg-config does not expose them as CMake
runtime targets. SunPlayer's config-aware FFmpeg component targets also make its
four DLLs participate in CMake's standard traversal. This prevents loader
dialogs and makes dependency boundaries reproducible during development.

After vcpkg and Qt deployment finish, the install runs
`packaging/windows/Generate-ThirdPartyNotices.ps1`. It reads installed vcpkg
SPDX/copyright files, Qt module SPDX/source notices, and Lucide's colocated
metadata. Every deployed dependency DLL/EXE must have one exact metadata-path
owner. Generation writes:

```text
share/sunplayer/LICENSE
share/sunplayer/PRIVACY.md
share/sunplayer/ThirdPartyNotices.txt
```

The notice file contains component versions, source locations, and the local
dependency-provided text. No machine inventory or third-party license copy is
stored in the repository. The Visual C++ runtime is supplied by the declared
Store framework rather than the application payload.

Remaining packaging work is operational:

* A non-Store Windows installer or portable layout.
* macOS signing, notarization, and bundle policy.
* Wayland Linux package formats and compositor/runtime requirements. X11 and
  XWayland compatibility are not packaging targets.
* Clean-machine package verification.

The Store package path stays on this install boundary. The small
`packaging/windows/Package-WindowsStore.ps1` entry point substitutes the four
release-specific manifest values and calls the installed Microsoft `winapp`
CLI. `winapp` owns PRI generation, architecture stamping, optional signing, and
MSIX creation. The packaging script does not download tools or duplicate
MakeAppx/Partner Center validation.

For package-identity development, the Windows-only
`sunplayer_run_with_identity` target installs the active configuration into an
isolated build-tree prefix, stages it with the resolved manifest and package
assets, and passes that complete input to `winapp run` with the development
identity. This ensures WinApp's generated PRI indexes the same package assets
as the MSIX path. WinApp owns loose-layout synchronization, registration, and
AUMID activation. This path needs Developer Mode but no certificate, uses the
shell-visible name `SunPlayer (Dev)`, and remains distinct from construction
and installation of the real MSIX. Its separate package identity permits
side-by-side installation with the future Store package.

## Testing integration

CTest and Qt Test are configured only under `BUILD_TESTING`, keeping test-only
dependencies out of production-only configurations. Separate test executables
cover presentation-target policy, active viewport state, real QML shell
publication, rendered-video surface validity/reuse, and a real D3D11 QRhi
producer/compositor capture. A shared dependency integration test verifies the
platform-specific libplacebo version and feature contract plus real log
create/destroy lifecycle. Windows requires its pinned D3D11/Shaderc
configuration; Linux requires API 360, Vulkan, a shader compiler, built-in
DOVI, and records LCMS availability. macOS requires the pinned Vulkan/Shaderc
configuration, MoltenVK Metal-object interop, and built-in DOVI handling.

A separate FFmpeg dependency test verifies the four selected DLLs, pinned
major versions, D3D11VA availability, native H.264/HEVC decoders, and the
absence of Vulkan and swscale on Windows. The same source verifies the system
ABI majors plus VAAPI and DRM hardware-device support on Linux; on macOS it
verifies VideoToolbox and the absence of unrelated Vulkan decode integration.
A shared cubeb
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

On macOS, the GPU/FFmpeg target additionally covers direct VideoToolbox NV12
and P010 import plus multi-frame native-surface lifetime; the shared subtitle
renderer target runs through Metal, and the device-backed audio target requires
AudioUnit. The application playback smoke exercises the production Metal,
MoltenVK, VideoToolbox-capable media, QML, subtitle, and default-audio wiring.

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

## Continuous integration

[The CI workflow](../../../.github/workflows/ci.yml) uses two explicit jobs
because dependency ownership and runtime capabilities differ materially by
platform. Both configure from the repository root into a runner-temporary
Debug build tree, build all production and test targets, run `all_qmllint`, and
invoke CTest directly.

The Linux job runs the official Ubuntu 26.04 container on `ubuntu-24.04`, uses
the documented system packages, and intends to run all registered Linux tests.
It creates a private headless native-Wayland Weston instance, selects Mesa
lavapipe through Ubuntu's packaged `lvp_icd.json`, and provides system cubeb a
real Pulse protocol server with one named null sink. This is software-hosted
integration evidence, not native-GPU, VAAPI/DRM PRIME import, managed-color,
HDR, physical-display, physical-audio, route-migration, or acoustic-sync
evidence.

The Windows job uses `windows-2022`, exact Qt 6.11.1 installed by a pinned
`aqtinstall` revision with Qt 6.11 repository-layout support, MSVC for SunPlayer,
and the root manifest's clang-cl vcpkg triplet for dependencies. It caches only
the exact Qt tree and vcpkg's ABI-addressed binary archives. It builds all GPU
and device code, runs QML lint, then excludes CTests labeled `device` or `gpu`
because a generic hosted runner does not satisfy SunPlayer's hardware-only D3D11
or default-audio-device contracts. That exclusion also drops the software/HDR
cases bundled into the mixed `ffmpeg-first-frame` executable; splitting a
hosted software subset is deferred while the complete test remains dedicated-
machine coverage.

The workflow requests only read-only repository contents, disables persisted
checkout credentials, and pins actions by full commit SHA. Every event stages,
probes, and packages the Release tree with the unsigned
`SunPlayerDevelopment` identity. Trusted main pushes and manual dispatches
upload that tree and MSIX as separate seven-day artifacts; pull requests upload
neither. Microsoft's pinned setup action provides `winapp` v0.6.0, and CI
checks every file in that release archive against a reviewed SHA-256 digest
before execution. The packaging script remains installation-free. The workflow
does not publish releases and does not cache build trees, the shared in-job
`vcpkg_installed` tree, downloads, artifacts, or credentials. Until the new
Release path succeeds in a hosted run, it is implemented and locally reviewed
configuration rather than a claim that hosted packaging passes.

The short-lived artifacts are project-internal developer output. Partner
Center identity, signing, certification, clean-machine verification, and an
actual Store submission remain separate release steps.

## Verification

The current Windows CMake tree registers 36 CTest cases. On 2026-08-23 the
RelWithDebInfo tree built successfully and all 36 passed together at bounded
parallelism in the Windows/MSVC/Ninja environment after initializing the Visual
Studio developer environment. Both production and test QML lint targets pass.
The dependency graph is built under the
project-local clang-cl triplet; the SunPlayer executable remains MSVC-built.
A bounded application scenario additionally opens an audio-first fixture in
the built executable and observes live default-device clock progress plus two
distinct video revisions reaching the swapchain before automatic exit.
A build-local install-tree generation also succeeds and stages the expected Qt
runtime plus libass, FreeType, FriBidi, HarfBuzz, zlib, cubeb, libplacebo,
SPIRV-Cross, and selected FFmpeg DLLs.

A prior build-tree GUI startup liveness smoke also passed with the configured
Qt runtime available; the harness terminated the process after four seconds
without user interaction.

On Ubuntu 26.04 under WSL, clean Debug and Release builds pass with system
dependencies. The last validated tree passed all 26 then-registered Linux tests
and QML lint; the current tree has additional platform-neutral support and
packaging coverage and awaits a new Linux run. Existing evidence includes the
system-cubeb sink, real application audio-first playback, shared embedded-
subtitle behavior, system libass rendering, exact Wayland SDR surface
selection, application-chrome layout behavior, and packaged-QML verification.
The final gamma-2.2 pixel readback remains in the Windows-only compositor
target. A Release install-tree generation also succeeds and keeps system
libraries under distribution ownership.

WSLg production smoke testing creates the real Qt Wayland window, Vulkan 1.3
QRhi domain, imported libplacebo device, direct RGBA16F target, redirected QML
scene, and swapchain. The installed Release executable verifies its embedded
SunPlayer module against system Qt imports. A prior video-only fixture run passed
fullscreen/restoration; the current audio-bearing explicit run ended in an
unresolved buffer/configure protocol failure before its final assertion. The
environment uses
llvmpipe and exposes neither `/dev/dri` nor color-management-v1, so this proves
the unmanaged assumed-sRGB software path and lifecycle only. The same installed
player opens WSLg's Pulse-compatible default route through system cubeb and
advances its cubeb-backed A/V clock. Native PulseAudio/PipeWire-Pulse route
changes, native GPU behavior, VAAPI/DRM PRIME, managed gamma-2.2 compositor
declaration, HDR, and physical displays remain open.
The current RelWithDebInfo build passes all 36 registered tests and both QML
lint targets. A fresh install generated notices for 21 components while
resolving all 94 dependency runtimes, passed packaged-QML verification, and
produced an unsigned MSIX. Clean-machine framework resolution, signing,
certification, and Store submission remain release gates.

On the Apple M2/macOS 26 host, Debug and clean Release source builds succeed
with tests enabled, and QML lint plus focused Metal, SDR/HDR, VideoToolbox,
subtitle, audio, seek, and production application scenarios pass. The prior
final Debug tree passed all 26 then-registered tests; the current tree has
additional platform-neutral support coverage, and its macOS rerun remains
pending. The Release executable itself declares macOS 13, but
cached vcpkg static archives were built for the newer host target; rebuilding
and validating the
complete dependency graph for macOS 13 is intentionally deferred to packaging,
and no release-support claim is made from that binary.
