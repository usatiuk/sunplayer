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
| BUILD-1 | macOS dependency contract | In progress | Qt 6.11.1 Online Installer plus pinned arm64 vcpkg libraries configure without Windows-only features. |
| BUILD-2 | Clean Debug and Release builds | Pending | Application and shared tests build from clean directories with tests on and off. |
| METAL-1 | QRhi Metal domain | Pending | Factory creates a valid Metal domain with explicit lifetime and diagnostics. |
| SDR-1 | Managed SDR sRGB | Pending | Deterministic patches, UI alpha, real SDR video, resize, and teardown pass without double encoding. |
| EDR-1 | Extended-linear EDR | Pending | QRhi EDR swapchain, relative headroom mapping, and deterministic values around `1.0` pass on capable hardware. |
| EDR-2 | Ordinary display transitions | Pending | Screen movement, HDR/SDR format change, paused rerender, fullscreen, expose/wake, and equal-state no-op behavior pass where available. |
| BRIDGE-1 | Metal/MoltenVK device identity | Pending | QRhi and libplacebo use the same `MTLDevice`; mismatch fails construction. |
| BRIDGE-2 | Production video target | Pending | Shared texture or diagnosed same-device GPU copy passes capture with correct ownership and no CPU roundtrip/per-frame queue idle. |
| APP-1 | Software-decoded application playback | Pending | SDR/PQ/HLG playback, subtitles, seek, pause/resume, fullscreen, and teardown pass through production code. |
| AUDIO-1 | Cubeb system-default route | Pending | Real audio-master playback works and follows a default-output change without application device policy. |
| VT-1 | VideoToolbox capability | Pending | FFmpeg negotiates the active-generation VideoToolbox device and reports it accurately. |
| VT-2 | NV12/P010 native import | Pending | Hardware frames remain alive through GPU use, output agrees with software decode, and diagnostics show native import/copies/synchronization. |
| VT-3 | Bounded software fallback | Pending | Unsupported or failed native import restarts once at the logical position and repetition becomes a visible error. |
| TEST-1 | Shared and macOS tests | Pending | Relevant CTests, QML lint, Metal/Vulkan validation, sustained playback, and lifecycle scenarios pass. |
| BUNDLE-1 | Local self-contained application | Pending | The `.app` launches outside the build tree with intended frameworks, plugins, shaders, dylibs, rpaths, and valid ad-hoc signature where required. |
| DOC-1 | Project truth synchronized | Pending | Root/subsystem plans, READMEs, testing matrix, decisions, and deferred work reflect the verified implementation. |

## Capability claims

| Claim | Required gates | Status |
| --- | --- | --- |
| arm64 macOS development build | BUILD-1, BUILD-2 | In progress |
| macOS SDR application playback | METAL-1, SDR-1, BRIDGE-1, BRIDGE-2, APP-1, AUDIO-1 | Pending |
| macOS EDR presentation | EDR-1, EDR-2 | Pending |
| VideoToolbox acceleration | VT-1, VT-2, VT-3 | Pending |
| Self-contained local `.app` | TEST-1, BUNDLE-1, DOC-1 | Pending |
