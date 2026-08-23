# Release support and controlled runtime errors

Status: Complete

## Goal

Make the Windows Store package identifiable and supportable, add
direct About/Report actions, and replace plausible externally caused graphics
aborts with controlled user-visible failure states.

Grounding and rejected alternatives are recorded in
[the research note](../../research/2026-08-22-release-support-and-runtime-errors.md).
Current evidence is tracked in
[the checklist](2026-08-22-release-support-and-runtime-errors-checklist.md).

## Execution order

1. Freeze the scope and account for the complete working-tree diff.
2. Remove incidental formatting, speculative behavior, duplicate deployment,
   and documentation claims that the final artifacts cannot prove.
3. Finish only the three boundaries below: generated package notices,
   Help/About/reporting, and controlled presentation failures.
4. Run focused checks, then the full affected build/test/install/package path.
5. Build the generated package, obtain three independent final reviews, fix
   material findings, and repeat the affected evidence before handoff.

No commit or push is part of this plan unless it is requested after the final
tree and evidence are presented.

## Scope gate

Every current change belongs to one of these groups. A file is removed from the
change if it cannot satisfy the stated reason or if the same result is already
owned by Qt, vcpkg, CMake, Windows, or the existing player state model.

| Boundary | Files | Keep only for |
| --- | --- | --- |
| Release identity | `LICENSE`, `PRIVACY.md`, `README.md`, `vcpkg.json`, `CMakeLists.txt`, `cmake/SunPlayerWindowsThirdPartyInstall.cmake.in` | Project/build identity and installation of the project-owned legal/support inputs. |
| Generated dependency notices | `.github/workflows/ci.yml`, `packaging/windows/Generate-ThirdPartyNotices.ps1`, `packaging/windows/Package-WindowsStore.ps1`, `packaging/windows/Package.appxmanifest.in`, `packaging/windows/README.md`, `vcpkg-ports/libplacebo/portfile.cmake`, `vcpkg-ports/libplacebo/vcpkg.json` | Reading authoritative local dependency metadata/notices, requiring Windows 11 24H2 and the Store VCLibs framework, and generating the packaged notice text. No checked-in third-party license copies or second dependency deployment path. |
| Help and reporting | `src/app/AppShell.qml`, `src/app/pages/PlayerPage.qml`, `src/app/SupportController.*`, `src/app/SupportDiagnostics.*`, `src/presentation/QuickUiLayer.*` | Direct Report/About reachability, one platform-styled About dialog, and bounded structured diagnostics with no automatic upload or raw logs. |
| Controlled errors | `src/app/ApplicationError.*`, `src/app/PresentationWindow.*`, `src/app/main.cpp`, `src/presentation/RhiPresentationEngine.*`, `src/presentation/HdrCompositor.*`, `src/graphics/backends/MetalLibplaceboVideoTarget.mm`, `src/graphics/backends/VulkanLibplaceboVideoTarget.cpp`, `src/video/DiagnosticVideoProducer.cpp`, `src/video/LibplaceboDecodedVideoProducer.cpp`, `src/platform/linux/LinuxWaylandWindowContext.*` | Converting plausible external runtime failures into one stopped generation and an existing media or native application error boundary. Fatal exits remain for packaged-asset and impossible-state invariants. |
| Tests | `tests/CMakeLists.txt`, `tests/unit/app/tst_Support.cpp`, `tests/integration/ui/QmlShellTestTypes.h`, `tests/integration/ui/tst_AppShell.cpp` | Stable error metadata, report privacy/bounds, and direct UI routing. No test-only production abstractions. |
| Durable documentation | `docs/research/README.md`, the research/plan/checklist for this change, `docs/DEFERRED.md`, and the build/graphics/UI subsystem READMEs | Recording the implemented boundary, evidence, and genuine platform gaps without legal speculation or claims stronger than validation. |

The first cleanup pass specifically removes unrelated formatter-only hunks in
otherwise required files. It also rechecks that the package generator derives
notice text from dependency-owned local inputs, that Qt owns its deployment,
and that presentation recovery does not grow into a general error framework.

## Completion condition

The final Windows install contains SunPlayer's license/privacy statement plus
offline-generated third-party notices. Deployed runtimes without a metadata
owner and missing notice inputs fail installation. About and Report
are directly reachable with and without active media. Reports are useful,
bounded, and exclude user/media identifiers and raw logs. Classified graphics
failures stop cleanly, preserve native-surface lifetime ordering, and offer
appropriate recovery/support actions. Focused and full affected validation is
current, documentation matches the artifacts, and post-change review is clean.

## Invariants

* The completed clean install tree is the Windows package source of truth.
* Vcpkg, Qt deployment, Store frameworks, and the operating system retain
  ownership of the files they already deploy or provide; SunPlayer adds no
  second copy path.
* Third-party notice generation is offline and commits no new third-party
  license text to the repository.
* Every copied dependency DLL/EXE maps to one package-manager-owned component
  by relative metadata path; port-owned combined notices cover bundled sources
  without reconstructing a dependency graph.
* Reports use structured application-owned fields and never include raw media
  metadata, file locations, user/host identifiers, raw logs, or machine IDs.
* A presentation generation publishes one terminal error and schedules no more
  work. Native-surface teardown is handled while any engine still exists.
* Decoded-video failure remains a media-session error unless the shared
  renderer itself has independently failed.
* Missing packaged assets and impossible ownership/state remain fail-fast
  invariants.

## Implementation

### Release metadata and notices

* Install the GPL-3.0-or-later project license and factual privacy policy.
* Expose project version, overrideable build ID, and source/issue URLs.
* Let vcpkg and Qt deployment produce the final Windows runtime tree.
* Generate notices from local vcpkg/Qt/Lucide inputs after install.
* Require Windows 11 24H2 or newer, declare the Store-provided Visual C++
  Desktop framework, and disable Qt's VCRedist installer payload.
* Treat libplacebo as one vcpkg-owned component whose installed combined notice
  covers its incorporated sources; do not infer a nested component graph from
  unrelated SPDX resource records.

### Help and diagnostics

* Keep Report a bug and About as direct items in the existing ellipsis menu.
* Use a platform-styled idle `ToolButton`, not playback-island styling.
* Use one native Qt Widgets About dialog for version/build, source, notices,
  privacy, and copy-diagnostics.
* Copy the detailed report before opening a short prefilled GitHub issue.
* Preserve production Unicode/punctuation while rejecting accidental
  path/URL/control-shaped values.

### Errors

* Keep one typed `ApplicationError` record/descriptor table.
* Convert plausible non-device-loss QRhi allocation/handoff failures into
  existing local `Unavailable` results.
* Preserve bounded device/swapchain/frame recovery, then stop and publish once.
* Destroy failed engines outside their signal stack; retry with a fresh engine.
* Continue forwarding native-surface destruction to a suspended live engine.
* Keep media failure in `MediaSession` and total presentation failure in the
  native application dialog.

## Required evidence

* Error descriptors and real production diagnostic strings.
* QML reachability/action routing in idle, active, and media-error states,
  including Open another.
* Clean Windows install and generated notice inspection.
* Store package command.
* Affected CTest/QML lint/full build evidence outside the sandbox.
* Final diff/status review plus three independent review lenses.

Native macOS/Wayland dialogs, dead-display behavior, and real driver/device-loss
exhaustion remain explicit platform-validation gaps unless corresponding native
evidence is available in this change.

## Non-goals

Telemetry, automatic upload, raw-log attachment, crash-dump services, an issue
submission API, a generalized error framework, a custom dialog toolkit,
presentation-level Open another, bundled compiler-runtime installers or DLLs,
copied Qt SBOM artifacts, or package formats beyond the current Windows Store
boundary.
