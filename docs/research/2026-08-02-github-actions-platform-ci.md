# GitHub Actions platform CI research

Status: Accepted implementation input

## Question

What is the smallest GitHub Actions design that continuously proves Sunroom's
accepted Windows and native-Wayland Linux builds without pretending that a
hosted virtual machine covers physical audio, hardware decode, or HDR output?

The answer must preserve the project's existing dependency contracts:

* Windows uses Qt 6.11.1, MSVC for Sunroom, and the project-local clang-cl
  vcpkg triplet for dependencies.
* Linux uses Ubuntu 26.04 system Qt 6.10, FFmpeg 8, libplacebo, cubeb, libass,
  Vulkan, Wayland, VA-API, and DRM packages.
* CTest and `all_qmllint` remain the shared public build/test entry points.

Research began on 2026-08-02 against the repository at `c684bc9`. The project
was subsequently flattened into the Git root in `137c64a`; the path
consequences below reflect that accepted layout.

## Runner and dependency findings

GitHub's stable x64 Linux runner is Ubuntu 24.04. Ubuntu 26.04 is available as
a public-preview runner, but preview images do not carry the same stability
commitment as generally available images. A job container is supported on a
Linux runner, and all run steps can use an explicitly selected shell. Running
the Linux job in the official `ubuntu:26.04` image on `ubuntu-24.04` therefore
keeps both the runner label and Sunroom's distribution ABI explicit.

The pinned `windows-2022` image provides Visual Studio Enterprise 2022,
MSVC/clang-cl, CMake, Ninja, Python, and a vcpkg checkout at `C:\vcpkg` through
`VCPKG_INSTALLATION_ROOT`. Qt is not part of the runner contract. Qt's official
repository publishes the required 6.11.1 MSVC 2022 archives and the
`qtshadertools` add-on.

`aqtinstall` can install those public Qt archives without Qt account secrets.
The released 3.3.0 package predates Qt 6.11's Windows per-architecture repository
layout, so CI pins immutable upstream merge commit
`8c3695d4a4e1ceabf6a74dc6c79681656dc6b74b`, which contains the accepted Qt 6.11
layout support, rather than using a movable branch. A normal PowerShell step
avoids a composite action whose internally referenced actions are tag-pinned
even when the outer action is commit-pinned. The installed Qt directory can be
cached by an official, commit-pinned `actions/cache` step with a key containing
the exact Qt version, architecture, and cache-layout revision.

Current vcpkg documentation no longer treats the old `x-gha` provider as a
supported binary-cache backend. GitHub Packages' NuGet provider requires
package-write policy or a separate token and is unnecessary for this
repository's first CI slice. A filesystem binary cache under `runner.temp`,
persisted with `actions/cache`, uses vcpkg's supported `files` provider and
requires only read-only repository permissions. The cache key covers the
manifest, registry baseline, configuration, overlay ports, and custom triplet.
Restoring an older prefix is safe because vcpkg validates each archive by its
own package ABI before reuse. Raw source downloads are not cached: a successful
binary hit already avoids them, and duplicating those archives would consume
cache quota without improving the normal path.

Windows project configuration also consumes libass's vcpkg-installed
pkg-config metadata. The manifest therefore declares `pkgconf` as a Windows
host tool and CMake declares the `x64-windows` host triplet before `project()`.
The vcpkg toolchain installs manifest dependencies before adding host tools to
`CMAKE_PROGRAM_PATH`, so CMake's standard `FindPkgConfig` module can discover
`pkgconf` without a runner package manager or a hard-coded internal path.

GitHub recommends an explicit least-privilege token policy and full commit SHA
pins for actions. The workflow needs only `contents: read`. `pull_request`, a
push to `main`, and manual dispatch provide useful coverage without scheduled
noise. Per-ref concurrency can cancel an obsolete run while allowing distinct
branches to proceed independently.

Primary sources:

* [GitHub-hosted runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
* [GitHub job containers](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/run-jobs-in-a-container)
* [GitHub Actions security hardening](https://docs.github.com/en/code-security/tutorials/secure-your-organization/protect-against-threats)
* [Windows 2022 runner image contents](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md)
* [vcpkg binary-cache providers](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching)
* [vcpkg host dependencies](https://learn.microsoft.com/en-us/vcpkg/users/host-dependencies)
* [Qt 6.11.1 Windows repository](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6111/)
* [aqtinstall 3.3.0 command reference](https://aqtinstall.readthedocs.io/en/v3.3.0/cli.html)
* [aqtinstall Qt 6.11 Windows layout fix](https://github.com/miurahr/aqtinstall/pull/1000)
* [Mesa Vulkan driver selection](https://docs.mesa3d.org/install.html)

## Runtime-test findings

The Ubuntu job can create honest software-hosted native resources:

* Weston's headless backend supplies a real Wayland compositor without X11 or
  XWayland.
* Mesa lavapipe supplies Vulkan 1.3+ through Ubuntu's packaged
  `/usr/share/vulkan/icd.d/lvp_icd.json`; `VK_DRIVER_FILES` selects that one
  driver explicitly.
* PulseAudio's null sink supplies a real Pulse protocol server and an advancing
  default output clock without claiming acoustic or route-switch evidence.

These services are sufficient in principle for Linux's existing system-cubeb
sink and production application playback scenario. Startup must be checked by
the actual Wayland socket and an exact Pulse socket connection, not an assumed
delay. Pulse readiness additionally requires the named null sink to exist and
be the default while the server remains alive. If a service or application
test fails, the job must fail and print its service logs; it must not turn the
test into an automatic skip.

Standard GitHub-hosted Windows runners do not contractually provide the audio
or GPU capabilities needed by the current default-device cubeb scenarios,
D3D11 presentation tests, or required D3D11VA test. Sunroom's current D3D11
domain requests a hardware device and has no WARP path. Hosted Windows CI must
therefore exclude tests carrying the existing `device` or `gpu` labels while
continuing to run dependency, media, QML, and other deterministic shared
boundaries. The `ffmpeg-first-frame` registration contains software/HDR cases
as well as its required D3D11VA cases, so its `gpu;hardware-decode` labels
exclude the whole executable; splitting a hosted software subset is deferred
instead of refactoring that large test as part of CI plumbing.

The Linux software environment is not evidence for native GPU selection,
VAAPI/DRM PRIME import, color-management-v1, HDR output, physical audio,
default-route migration, or acoustic A/V sync. The Windows hosted environment
is not evidence for D3D11VA, physical audio, Advanced Color, or HDR output.

## Design consequences

The Git repository root is now the project and CMake source root. The workflow
lives at `.github/workflows/ci.yml`; CMake uses `-S .`, and cache hashes name
`vcpkg.json`, `vcpkg-configuration.json`, `vcpkg-ports/**`, and
`cmake/vcpkg/**` directly.

Use two explicit jobs rather than a conditional platform matrix. Their
dependency ownership, runtime services, commands, and capability exclusions
are materially different; a matrix would hide those facts behind conditions.

Keep the workflow in one file and invoke the existing CMake/CTest boundaries
directly. Do not add wrapper frameworks, a custom CI image, reusable workflows,
CMake presets used only by CI, artifact publishing, or a test-selection script
until repetition or another consumer justifies them.

No architectural decision record is required. This work automates already
accepted platform, dependency, and testing decisions without changing product
runtime architecture.

## Implementation-time verification

Local Ubuntu 26.04 `apt-cache` metadata confirmed every named build/runtime
package on 2026-08-02, including Weston 14.0.2, PulseAudio 17, Mesa 26.0.3,
Qt 6.10.2, FFmpeg 8.0.1, and libplacebo 7.360.0. The packaged Weston 14.0.2
manual confirms `--backend=headless`, `--renderer`, `--no-config`, `--socket`,
`--idle-time=0`, and the headless `--width`/`--height` options. Official Git
refs mapped the reviewed checkout and cache action SHAs to their current `v4`
tags. These checks validate package/action inputs; they do not replace the
required first hosted run.
