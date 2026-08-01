# Build and packaging subsystem

## Status

The current CMake project builds the Windows player prototype and its focused
Qt Test targets. Qt, a pinned D3D11-only libplacebo dependency, minimal official
FFmpeg components including libswresample, and a pinned cubeb dependency are
integrated. cubeb provides production default-device output on Windows. libass
and complete distributable packaging are not integrated.

The currently validated configuration is:

| Requirement | Current value |
| --- | --- |
| Build system | CMake 3.22 or newer |
| Languages | C11 for the libplacebo/FFmpeg adapter; C++20 elsewhere |
| Application compiler | MSVC in a Visual Studio developer environment |
| Windows dependency compiler | Visual Studio clang-cl through a project-local vcpkg triplet |
| Qt | Exactly 6.11.1 |
| libplacebo | Exactly 7.360.1, D3D11 enabled |
| FFmpeg | Exactly 8.1.2, shared avutil/swresample/avcodec/avformat |
| cubeb | Upstream commit `ef47ae581df7c2f76058d554b3edde17f9ee7cba` |
| Graphics backend | Windows D3D11 through QRhi |
| Generator in the local configured tree | Ninja |

The Visual Studio C++ Clang tools component is required for Windows dependency
builds. The application and installed Qt package remain MSVC-built; clang-cl
supplies the same Windows ABI and runtime conventions for the vcpkg dependency
graph.

Local absolute Qt, CMake, and developer-shell paths remain machine-specific and
belong in ignored local agent or IDE configuration.

## Dependency management

Sunroom uses vcpkg manifest mode. The repository owns:

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
`avformat`, and `swresample`; `avutil` is core. The Windows port enables
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

CMake installs the executable or bundle through `GNUInstallDirs`. On Windows,
the libplacebo, SPIRV-Cross, and four selected FFmpeg shared runtime artifacts
are installed explicitly before `qt_generate_deploy_qml_app_script()` supplies
the current Qt deployment step with:

* No compiler-runtime deployment.
* No translation deployment.
* No unsupported-platform configuration error.

Build-tree application and test targets stage their transitive runtime DLLs
with `TARGET_RUNTIME_DLLS`. Sunroom's config-aware FFmpeg component targets make
its four DLLs participate in that standard traversal. This prevents loader
dialogs and makes both dependency boundaries reproducible during development.

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
producer/compositor capture. A dependency integration test verifies the pinned
libplacebo version, installed feature configuration, and real log
create/destroy lifecycle across the MSVC-to-clang-cl DLL boundary. That
configuration enables D3D11, Shaderc, and built-in DOVI handling while
disabling Vulkan, OpenGL, and external libdovi.

A separate FFmpeg dependency test verifies the four selected DLLs, pinned
major versions, D3D11VA availability, native H.264/HEVC decoders, and the
absence of Vulkan and swscale. A cubeb dependency test compiles and links its
public C ABI without requiring COM initialization or an available device.
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

The complete Debug build and all 24 registered CTest cases pass in the current
Windows/MSVC/Ninja environment after initializing the Visual Studio developer
environment. The dependency graph is built under the project-local clang-cl
triplet; the Sunroom executable remains MSVC-built. This includes the cubeb
ABI/backend check, FFmpeg libswresample boundary, one-pass synchronized decode,
bounded controlled sink, real D3D11VA decode/import, and GPU compositor tests.
A bounded application scenario additionally opens an audio-first fixture in
the built executable and observes live default-device clock progress plus two
distinct video revisions reaching the swapchain before automatic exit.
A build-local install-tree generation also succeeds and stages the expected
Qt runtime, `libplacebo-360.dll`, `spirv-cross-c-sharedd.dll`, and selected
FFmpeg DLLs.

A prior build-tree GUI startup liveness smoke also passed with the configured
Qt runtime available; the harness terminated the process after four seconds
without user interaction.

No Release application build, installed-application launch, clean-machine
deployment audit, or cross-platform build is recorded yet. These remain
coverage gaps rather than implied support.
