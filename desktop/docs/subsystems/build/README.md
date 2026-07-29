# Build and packaging subsystem

## Status

The current CMake project builds the Windows presentation prototype and its
focused Qt Test targets. Qt, a pinned D3D11-only libplacebo dependency, and a
minimal official FFmpeg dependency are integrated. libass, audio, and complete
distributable packaging are not.

The currently validated configuration is:

| Requirement | Current value |
| --- | --- |
| Build system | CMake 3.21 or newer |
| Languages | C11 for the libplacebo/FFmpeg adapter; C++20 elsewhere |
| Application compiler | MSVC in a Visual Studio developer environment |
| Windows dependency compiler | Visual Studio clang-cl through a project-local vcpkg triplet |
| Qt | Exactly 6.11.1 |
| libplacebo | Exactly 7.360.1, D3D11 enabled |
| FFmpeg | Exactly 8.1.2, shared avutil/avcodec/avformat |
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

The FFmpeg dependency uses the official registry port with only `avcodec` and
`avformat`; `avutil` is core. The Windows port enables D3D11VA, D3D12VA,
DXVA2, and Media Foundation without a separate manifest feature. It excludes
Vulkan, swscale, swresample, filter/device libraries, command-line tools,
vendor SDKs, and external codec libraries in this slice.

## Qt dependency

The executable currently requires:

* `Qt6::Quick`.
* `Qt6::QuickControls2`.
* `Qt6::ShaderTools`.
* `Qt6::GuiPrivate` for QRhi.

Qt is pinned to exactly 6.11.1 because QRhi is a private API with limited
compatibility guarantees. A Qt upgrade is an explicit maintenance task that
must rebuild and runtime-test redirected Quick rendering, swapchain HDR state,
texture and shader resources, surface loss, and device recovery.

## Targets and resources

The project defines the `sunroom` executable plus two production static
libraries: `sunroom_media` for the FFmpeg frame/decode boundary and
`sunroom_video_pipeline` for the shared graphics, libplacebo producer/importer,
target, and final-compositor code. The application and GPU tests link these
same compiled production libraries rather than maintaining parallel source
lists.

`qt_add_qml_module()` packages `src/app/Main.qml`, `AppShell.qml`, and
`pages/HdrLabPage.qml` under the `Sunroom` module. `qt_add_shaders()`
precompiles and packages the fullscreen vertex, diagnostic video producer, and
compositor shaders from `src/presentation/shaders/` under `/shaders`.

Production sources are grouped under `src/app`, `src/graphics`, `src/media`,
`src/platform`, `src/presentation`, and `src/video`. Focused tests currently
live under `tests/unit/media`, `tests/unit/presentation`, `tests/unit/ui`,
`tests/unit/video`,
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
the libplacebo, SPIRV-Cross, and three selected FFmpeg shared runtime artifacts
are installed explicitly before `qt_generate_deploy_qml_app_script()` supplies
the current Qt deployment step with:

* No compiler-runtime deployment.
* No translation deployment.
* No unsupported-platform configuration error.

Build-tree application and test targets stage their transitive runtime DLLs
with `TARGET_RUNTIME_DLLS`. Sunroom's config-aware FFmpeg component targets make
its three DLLs participate in that standard traversal. This prevents loader
dialogs and makes both dependency boundaries reproducible during development.

This remains scaffolding rather than a complete distributable package. It does
not yet define:

* Final third-party notices, corresponding-source/build-recipe handling, and
  codec licensing/patent policy for FFmpeg and libass.
* A complete third-party notice and source-offer workflow for shipped
  libplacebo and other LGPL components.
* Windows installer or portable layout.
* macOS signing, notarization, and bundle policy.
* Linux package formats and compositor/runtime requirements.
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

A separate FFmpeg dependency test verifies the three selected DLLs, pinned
major versions, D3D11VA availability, native H.264/HEVC decoders, and the
absence of Vulkan and swscale. The first-frame integration target then
exercises real demux, software decode, libplacebo upload, and final QRhi
composition.

On Windows, each test target stages its transitive runtime DLLs beside the test
executable with CMake's `TARGET_RUNTIME_DLLS` support. This is a build-tree test
convenience, not an installed test package. It prevents the Windows loader from
opening a missing-DLL dialog when CTest launches the test.

See [../testing/PLAN.md](../testing/PLAN.md).

## Verification

The manifest configure, focused Debug build, and nine registered CTest targets pass
in the current Windows/MSVC/Ninja environment after initializing the Visual
Studio developer environment. The dependency graph is built under the
project-local clang-cl triplet; the Sunroom executable remains MSVC-built.
A build-local install-tree generation also succeeds and stages the expected
Qt runtime, `libplacebo-360.dll`, `spirv-cross-c-sharedd.dll`, and selected
FFmpeg DLLs.

A prior build-tree GUI startup liveness smoke also passed with the configured
Qt runtime available; the harness terminated the process after four seconds
without user interaction.

No Release application build, installed-application launch, clean-machine
deployment audit, or cross-platform build is recorded yet. These remain
coverage gaps rather than implied support.
