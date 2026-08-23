# Release support and runtime-error boundary

Status: Implemented and locally validated on Windows; native/manual platform
and Store-submission checks remain open.

## Question

What is the smallest coherent boundary for a Windows Store package that can
identify its packaged dependencies, provide useful support actions, and turn
plausible graphics failures into controlled user-visible states?

## Findings

### The final install tree is the package authority

SunPlayer's Windows Store payload is assembled by two existing owners:

1. vcpkg app-local deployment copies native media/rendering dependencies;
2. Qt deployment copies Qt and QML plugins before the Store wrapper packages
   the completed install tree.

A target link graph is not the package manifest: it misses deployment-selected
plugins and includes build-only inputs. Notice generation therefore scans the
final install tree after deployment and rejects a DLL/EXE without one metadata
owner.

Qt remains app-local; it cannot be replaced by an MSIX framework declaration.
The Visual C++ runtime is different: the manifest declares the Store-serviced
`Microsoft.VCLibs.140.00.UWPDesktop` framework, so Qt's otherwise dead
`vc_redist.x64.exe` payload is disabled. The package targets Windows 11 24H2
(build 26100) and newer. Windows also supplies D3D11, DXGI, WASAPI, Media
Foundation, Win32, and Schannel, so those are not copied or included in the
generated notices.
This follows Microsoft's
[Desktop Bridge runtime-package guidance](https://learn.microsoft.com/en-us/troubleshoot/developer/visualstudio/cpp/libraries/c-runtime-packages-desktop-bridge)
and Qt's documented
[Windows deployment behavior](https://doc.qt.io/qt-6/windows-deployment.html).

### Notices are generated from local build inputs

Third-party license text is not copied into the repository. Package generation
reads the copyright files installed by target-triplet vcpkg ports, the matching
Qt source `LICENSES` directories paired with Qt's local module SPDX metadata,
and the already-vendored Lucide notice. Generation is offline and fails when a
deployed runtime has no metadata owner or a required notice input is missing.

The original handwritten catalog described below was superseded by the
[notice-only generator](2026-08-22-derived-third-party-inventory.md).
Each installed vcpkg port is one component; each deployed Qt runtime is owned
by its exact module-SPDX path; and Lucide reads its colocated README/LICENSE.
Raw source locations are preserved without inferring a preferred upstream or
nested bundled-component graph. Libplacebo's combined installed copyright is
therefore the authority for incorporated fast_float and Vulkan-Headers sources.

The generated artifact contains component versions, source locations, and the
actual local notice text. Dependency SPDX files remain generation inputs rather
than copied package artifacts.

### Support is explicit and local

The existing player ellipsis menu directly exposes **Report a bug…** and
**About SunPlayer**. A normal platform-styled `ToolButton` exposes the same menu
when no media is active.

About is a small Qt Widgets dialog. It uses the platform widget style and does
not depend on SunPlayer's redirected QRhi/Qt Quick renderer, which is important
when presentation itself has failed. It shows version/build, source, generated
third-party notices, the packaged privacy policy, and a copy-diagnostics action.

Report a bug copies a detailed summary to the clipboard and asks the system
browser to open a bounded prefilled GitHub issue. It does not upload, attach, or
submit anything. Reports never include media paths/URLs/names, raw logs, local
log paths, account/host identifiers, or machine IDs. Fields come from an
explicit structured allowlist; values reject accidental path/URL/control data
but retain real Unicode and punctuation such as `scRGB / ...` and `PQ · ...`.

### Error ownership stays narrow

`ApplicationError` is one value plus a descriptor table for stable code,
subsystem, recoverability, and suggested actions. Existing low-level result
enums remain local.

Plausible QRhi/device/swapchain/resource/submission failures use bounded
recovery, then stop the failed presentation generation and publish one typed
error. The application destroys that generation outside its signal stack.
Retry creates a fresh generation. A native dialog offers the actions that can
repair or report total presentation failure: Retry where meaningful, Restart,
Report a bug, and Quit.

Media open/decode/render exhaustion remains owned by `MediaSession` and uses the
in-scene error page with Retry, Restart, Open another, Report a bug, and Quit.
A decoded libplacebo target/handoff failure is a media presentation failure; it
does not by itself prove that the UI/compositor device is unusable.

Packaged QML/shader absence, impossible native-handle/state ownership, and
synchronization overflow remain fatal programming/deployment invariants.

### Platform boundary

Qt Widgets is available on Windows, macOS, and Linux after `QApplication`
successfully creates a platform connection. A pre-application QPA failure or a
dead Wayland display cannot reliably show another in-process window and must
log/exit instead. Live-QPA Wayland/Vulkan prerequisite failures may use the
startup dialog. Native macOS/Wayland dialog and forced-driver-failure behavior
still require platform evidence.

The notice generator currently belongs to the Windows Store package.
Non-bundled Linux installs and the not-yet-defined macOS bundle do not claim a
copied dependency-notice set; About reports that notices are not
included in such a build.

## Review corrections

The post-implementation review rejected these intermediate designs:

* a host-specific `14.51` compiler-runtime gate and a dead packaged
  `vc_redist.x64.exe` installer;
* claims that Qt SBOM files were copied into the package;
* escalation of decoded-video interop failure to total presentation failure;
* suspending native-surface teardown until queued engine destruction;
* ASCII-only diagnostics that discarded production graphics strings;
* a playback-island-styled idle support button;
* presentation-level Open-another behavior that cannot repair a dead renderer.

## Current evidence and remaining release gates

The current RelWithDebInfo tree passes all 36 registered CTests and both QML
lint targets. A fresh install generated plain-text notices while resolving all
94 dependency runtimes, passed packaged-QML verification, and produced an
unsigned MSIX.

This is local construction evidence, not Store certification. Signed package
installation on a clean Windows 11 24H2 machine, the native Windows About and
report clipboard/browser smoke, Store submission, native macOS/Linux dialog and
failure-path checks, and physical driver/device-loss exhaustion remain release
gates.
