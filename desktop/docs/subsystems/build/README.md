# Build and packaging subsystem

## Status

The current CMake project builds the Windows presentation prototype and its
focused Qt Test targets. It discovers Qt only; FFmpeg, libplacebo, libass,
audio, and packaging policy beyond Qt's basic deployment script are not
integrated.

The currently validated configuration is:

| Requirement | Current value |
| --- | --- |
| Build system | CMake 3.21 or newer |
| Language | C++20 |
| Compiler/toolchain | MSVC in a Visual Studio developer environment |
| Qt | Exactly 6.11.1 |
| Graphics backend | Windows D3D11 through QRhi |
| Generator in the local configured tree | Ninja |

Local absolute tool paths and developer-shell setup are machine-specific and
belong in ignored local agent or IDE configuration, not in the shared build
contract.

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

The project defines one executable target, `sunroom`.

`qt_add_qml_module()` packages `src/app/Main.qml` under the `Sunroom` module.
`qt_add_shaders()` precompiles and packages the fullscreen vertex, diagnostic
video producer, and compositor shaders from
`src/presentation/shaders/` under `/shaders`.

Production sources are grouped under `src/app`, `src/platform`, and
`src/presentation`. Focused tests currently live under
`tests/unit/presentation`; new trees should follow concrete execution classes
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

CMake installs the executable or bundle through `GNUInstallDirs`.
`qt_generate_deploy_qml_app_script()` supplies the current Qt deployment step
with:

* No compiler-runtime deployment.
* No translation deployment.
* No unsupported-platform configuration error.

This is scaffolding, not a complete distributable package. It does not yet
define:

* Dependency bundling and licensing for FFmpeg, libplacebo, or libass.
* Windows installer or portable layout.
* macOS signing, notarization, and bundle policy.
* Linux package formats and compositor/runtime requirements.
* Runtime feature and dependency-version reporting.
* Clean-machine package verification.

## Testing integration

CTest and Qt Test are configured only under `BUILD_TESTING`, keeping test-only
dependencies out of production-only configurations. Separate test executables
cover presentation-target policy and rendered-video surface validity/reuse.

On Windows, each test target stages its transitive runtime DLLs beside the test
executable with CMake's `TARGET_RUNTIME_DLLS` support. This is a build-tree test
convenience, not an installed test package. It prevents the Windows loader from
opening a missing-DLL dialog when CTest launches the test.

The application follows a different contract:

* Running from the development build tree relies on the configured Qt
  development environment.
* Installing the application runs Qt's QML deployment script to populate the
  install tree with required Qt libraries, plugins, and QML runtime content.

See [../testing/PLAN.md](../testing/PLAN.md).

## Verification

The `sunroom` Debug target builds successfully in the current configured
Windows/MSVC/Ninja environment after initializing the Visual Studio developer
environment.

No clean configure, Release build, install-tree launch, deployment audit, or
cross-platform build is recorded yet. These remain coverage gaps rather than
implied support.
