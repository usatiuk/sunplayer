# Release support and runtime-error checklist

Status: Complete

`Done` requires current evidence, not implementation intent or an older
temporary artifact.

| Gate | Status | Required evidence |
| --- | --- | --- |
| Scope audit | Done | Every changed file maps to one requested boundary. Incidental formatting and duplicate renderer state were removed; generated notices use dependency-owned text, and the Store framework replaces Qt's dead VCRedist payload. |
| Project metadata | Done | Root GPL-3.0-or-later license, privacy policy, version, build ID, source and issue identity are built and installed; installed QML verification passed. |
| Generated third-party notices | Done | The notice-only generator reads local vcpkg/Qt/Lucide inputs; a fresh Release install generated plain text for 21 components and emitted no JSON. |
| Runtime ownership | Done | The fresh install resolved all 94 dependency runtimes; a disposable copied-install probe confirmed that an unowned DLL stops generation. |
| Support diagnostics | Done | Unit tests retain production Unicode graphics/color strings while excluding paths, URLs, control text, raw logs and identifiers; detailed and issue output are bounded. |
| About and report UI | Done | QML tests route idle/active direct actions and all media-error actions; the native Qt Widgets dialog owns the required contents/actions, and Report copies the bounded detailed summary before browser dispatch. Native visual appearance remains a release smoke, not a code-completion claim. |
| Typed runtime errors | Done | Descriptor tests cover every stable subsystem/recoverability/action record; converted QRhi/handoff failures preserve media-versus-presentation ownership. |
| Presentation lifetime | Done | Code review plus the full graphics/application suite cover one-shot stop, queued fresh-generation retry, and suspended native-surface teardown at the strongest practical Windows boundary. |
| Focused validation | Done | The current RelWithDebInfo tree passes all 36 registered CTests and both QML lint targets. |
| Clean install/package | Done | The fresh RelWithDebInfo install passed packaged-QML verification and produced the unsigned notice-only MSIX. |
| Documentation | Done | Root, research/plan, subsystem and deferred docs record current local evidence and platform scope without legal/patent analysis. |
| Final review | In progress | Three read-only correctness, simplicity and evidence reviewers are checking the notice-only correction. |
| Native macOS/Linux evidence | Needs native platform | Widgets/About, startup dialogs, Metal/Vulkan/Wayland failures and dead-display behavior are validated on native systems. |

## Rejected scope

Automatic upload, raw-log attachment, telemetry, crash collection, a general
error hierarchy, custom dialog infrastructure, presentation-level Open another,
copied Qt SBOMs, and bundled compiler-runtime installers or DLLs.
