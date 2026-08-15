# libplacebo Windows dependency-build research

* Date: 2026-07-29
* Status: Incorporated into `docs/subsystems/build/README.md`

## Question

How should SunPlayer reproducibly build the pinned libplacebo dependency on
Windows without installing project libraries globally, enabling an unrelated
graphics backend, or changing the MSVC toolchain used by Qt and the
application?

## Constraints

* SunPlayer and the installed Qt 6.11.1 package use the MSVC ABI.
* The first libplacebo backend is D3D11; Vulkan and OpenGL are not part of this
  slice.
* Dependency sources and versions must be pinned.
* Build tools may use normal per-user download and binary caches, but must not
  register libraries globally or modify the system `PATH`.
* Runtime DLLs must be staged before CTest starts a process so a missing loader
  dependency cannot produce an interactive Windows dialog.

## Experiments

### Native MSVC did not build the pinned source cleanly

The first project-local vcpkg port used the normal `x64-windows` triplet. The
initial failures included C11 atomic support and GNU-oriented compiler flags.
After addressing those configuration issues locally, compilation continued to
fail on additional source constructs and compiler assumptions. At that point,
carrying a growing project patch set against libplacebo was higher risk than
selecting a compatible Windows compiler for the dependency.

The pinned libplacebo revision's upstream Windows automation uses a MinGW
environment rather than exercising this native MSVC path. Historical support
claims therefore were not sufficient evidence that the current revision and
feature set would remain warning- and patch-free under the installed MSVC
version.

### clang-cl preserves the intended binary boundary

Visual Studio's clang-cl compiled the complete vcpkg dependency graph and both
Debug and Release libplacebo packages. SunPlayer itself remained compiled with
MSVC and linked against the MSVC-built Qt package.

This boundary is deliberately narrow:

* libplacebo exposes a C API.
* clang-cl targets the Windows MSVC ABI and dynamic CRT selected by the local
  triplet.
* The compiler choice lives in a project-local vcpkg triplet and chainloaded
  toolchain, so it participates in package ABI and binary-cache identity.
* A real MSVC-built Qt Test process loads the clang-cl-built libplacebo DLL,
  checks the installed generated feature configuration and version, and
  creates and destroys a public API object.

This is not permission to mix arbitrary C++ ABIs or runtimes across future
dependency boundaries.

### D3D11 shader translation has non-Vulkan dependencies

libplacebo's D3D11 backend still needs to compile its generated shaders and
translate SPIR-V to HLSL. The selected feature graph therefore includes
Shaderc and the shared SPIRV-Cross C API. glslang, SPIRV-Headers, and
SPIRV-Tools arrive as Shaderc implementation dependencies.

Those packages do not enable a Vulkan renderer in SunPlayer. The libplacebo port
passes `vulkan=disabled`, `vk-proc-addr=disabled`, and `opengl=disabled`. The
installed `config.h` records D3D11, Shaderc, and built-in DOVI handling
enabled, with Vulkan, OpenGL, and the optional external libdovi dependency
undefined.

The port also reconstructs libplacebo's pinned Vulkan-Headers source submodule.
Disabled-backend stubs and public declarations require those header types even
when no Vulkan implementation is compiled. This work installed no Vulkan SDK,
loader, or runtime, and the configure did not discover a system SDK. A
dependency inspection of the resulting Debug `libplacebo-360.dll` contains no
Vulkan loader dependency. This does not make claims about graphics-driver
components that may already exist on the machine.

### A shared SPIRV-Cross C package is required

The registry's SPIRV-Cross package is static-only, while the dynamic
libplacebo build expects the shared C library and its runtime. The local
`spirv-cross-c-shared` overlay builds only the required C, GLSL, and HLSL
components.

Its generated Debug pkg-config metadata named the Release import library.
Correcting that generated Debug library name in the overlay port allowed
libplacebo's final Debug link while preserving the upstream-generated CMake
targets.

## Resulting direction

Use vcpkg manifest mode with:

1. A pinned built-in registry baseline.
2. Exact libplacebo and source-submodule revisions.
3. Project-local libplacebo and shared SPIRV-Cross overlay ports.
4. A project-local `x64-windows-clangcl` dependency triplet.
5. MSVC for SunPlayer and Qt.
6. Explicit build-tree and install-tree runtime DLL staging.

The current build contract is documented in
`docs/subsystems/build/README.md`. The libplacebo renderer and QRhi/D3D11
target interop remain a separate implementation slice.

## Revisit when

* A future pinned libplacebo revision has a verified native MSVC build, allowing
  the local compiler split and patches to be reduced.
* Linux and macOS dependency configurations are introduced.
* The project deliberately enables Vulkan and defines its SDK, loader, and
  validation-tool policy.
* Release packaging defines third-party notices, LGPL obligations, compiler
  runtime deployment, and clean-machine verification.
