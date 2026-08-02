# Native Apple-Silicon macOS port checklist

Status: Active

## Purpose and update rules

This is the persistent execution ledger for the
[native macOS port](2026-08-02-native-macos-port.md). The main plan owns scope
and architecture. This file tracks implementation and evidence without adding
a second roadmap.

Statuses are `Pending`, `In progress`, `Needs native hardware`, and `Done`.
Keep IDs stable. Mark an item Done only after its acceptance condition passes
and the evidence column identifies the revision, environment, command or
scenario, result, and remaining limitation.

## Gates

| ID | Gate | Status | Acceptance and evidence |
| --- | --- | --- | --- |
| BUILD-1 | macOS dependency contract | Done | On Apple M2/macOS 26, ordinary CMake configures Qt 6.11.1 from the Online Installer and pinned manifest libraries through stock `arm64-osx`; Debug and Release source builds succeed. The macOS 13 archive/release claim is a packaging gate. |
| BUILD-2 | Clean Debug and Release builds | Done | Debug and clean Release source trees build with tests enabled. The final post-review Debug build and all 26 registered tests pass; packaging must still rebuild/verify dependency archives for the macOS 13 floor. |
| METAL-1 | QRhi Metal domain | Done | Production and GPU tests create QRhi Metal plus libplacebo Vulkan/MoltenVK on Apple M2 with explicit diagnostics and teardown. |
| SDR-1 | Managed SDR sRGB | Done | Metal compositor and real BT.709 captures pass exact/tolerant pixel assertions; production playback, resize, and teardown smokes pass on macOS 26. |
| EDR-1 | Extended-linear EDR | Needs native hardware | Mapping tests cover unknown absolute SDR white, scale `1.0`, and current/potential headroom. AppKit reported current `1.0`, potential `2.0`; this host did not expose current headroom above `1.0` for physical verification. |
| EDR-2 | Ordinary display transitions | Needs native hardware | `QWindow::screenChanged`, AppKit screen-parameter notification, expose reprobe, semantic rerender, and format reconciliation are implemented. Only one connected display was available; unlike-display movement and live HDR/SDR switching remain manual gates. |
| BRIDGE-1 | Metal/MoltenVK device identity | Done | Construction exports MoltenVK's Metal device and requires identity with QRhi's `MTLDevice`; production startup succeeds on Apple M2 and mismatch cannot form a valid domain. |
| BRIDGE-2 | Production video target | Done | Focused Metal capture and production smoke use the QRhi-owned RGBA16F `MTLTexture` directly in libplacebo. An exported timeline semaphore/`MTLSharedEvent` performs GPU-only handoff; the initial hold is deferred until a QRhi command buffer can consume it, and a regression replaces a provisioned target before that first submission. Diagnostics report zero output copies and CPU transfers. |
| APP-1 | Software-decoded application playback | Done | Production-path tests cover SDR/PQ/HLG/HDR10+/Dolby Vision, subtitles, seeking, pause/rerender contracts, and teardown. Bounded playback and a clean direct fullscreen smoke pass on macOS 26; fullscreen works interactively, and the native Open File sheet no longer creates a second window in user-confirmed playback. Physical EDR remains EDR-1/2. |
| AUDIO-1 | Cubeb system-default route | Needs native hardware | Cubeb dependency, sink lifecycle, and production A/V smoke pass with the `audiounit` backend and an advancing audio-master clock; user-confirmed playback is audible on the current default device. A live default-output change was not performed. |
| VT-1 | VideoToolbox capability | Done | The Metal domain publishes an active-generation FFmpeg VideoToolbox device; H.264 and Main10 HEVC tests select and diagnose it. |
| VT-2 | NV12/P010 native import | Done | H.264/NV12 and HEVC/P010 `CVPixelBuffer` planes import through CoreVideo Metal views, match software output within tolerance, report zero copies/transfers, and survive a three-frame producer run through GPU completion. |
| VT-3 | Bounded software fallback | Done | Existing shared hardware-decode and native-import failure scenarios preserve one position-aware software restart and terminal repetition behavior; macOS uses the same policy boundary. |
| TEST-1 | Shared and macOS tests | Done | Focused Metal, SDR/HDR, VideoToolbox, subtitle, audio, seek, application-smoke, QML-lint, teardown, and pre-submission target-resize checks pass. The final post-review Debug suite passes 26/26 in 25.43 seconds and leaves no Sunroom process running. |
| BUNDLE-1 | Local self-contained application | Pending | The `.app` launches outside the build tree with intended frameworks, plugins, shaders, dylibs, rpaths, and valid ad-hoc signature where required. |
| DOC-1 | Project truth synchronized | Done | Root/subsystem plans, READMEs, testing matrix, decision consequences, research, and deferred work describe the implemented port, verified behavior, native-hardware gaps, and separate packaging phase. |

## Capability claims

| Claim | Required gates | Status |
| --- | --- | --- |
| arm64 macOS development build | BUILD-1, BUILD-2 | Done |
| macOS SDR application playback | METAL-1, SDR-1, BRIDGE-1, BRIDGE-2, APP-1, AUDIO-1 | Needs native hardware |
| macOS EDR presentation | EDR-1, EDR-2 | Needs native hardware |
| VideoToolbox acceleration | VT-1, VT-2, VT-3 | Done |
| Self-contained local `.app` | TEST-1, BUNDLE-1, DOC-1 | Pending |
