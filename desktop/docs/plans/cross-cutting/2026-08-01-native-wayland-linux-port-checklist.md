# Native Wayland Linux port checklist

Status: Active

Upstream checkpoint: `5357d96` (`feat: add embedded subtitle playback`)

## Purpose and update rules

This is the persistent execution and evidence ledger for the
[native Wayland Linux port](2026-08-01-native-wayland-linux-port.md). The main
plan is the sole source of architecture, invariants, scope, and acceptance
decisions. This checklist gives stable IDs to work that spans build, graphics,
presentation, display transitions, media, audio, testing, and packaging.

Use these statuses:

* `Done`: the gate has linked durable evidence.
* `In progress`: implementation or validation is actively incomplete.
* `Pending`: work has not started or has no accepted evidence.
* `Needs native hardware`: WSL/CI cannot supply the required evidence.
* `Decision needed`: the plan intentionally leaves a choice to an evidence-
  producing spike.

Do not mark a gate `Done` from chat history or an unrecorded manual result.
Evidence belongs in a concise dated note under
`docs/plans/cross-cutting/linux-port-evidence/` and records commands, build
type, commit, dependency versions, compositor/kernel/driver/GPU/display/audio
environment where relevant, result, and known gaps. Do not check in bulky raw
logs. Link the note from the gate it closes.

Update this file in the same change that completes, invalidates, or materially
re-scopes a gate. Update root `PLAN.md` only when its corresponding high-level
roadmap state actually changes.

## Current baseline

| ID | Gate | Status | Evidence | Notes |
| --- | --- | --- | --- | --- |
| BASE-01 | Inspect Ubuntu 26.04 system packages and matching patched sources | Done | [Ubuntu platform baseline](../../research/2026-08-01-ubuntu-26-04-linux-platform-baseline.md) | Qt 6.10.2, FFmpeg 8.0.1, libplacebo 7.360.0, libass 0.17.4, cubeb distro snapshot |
| BASE-02 | Download matching Ubuntu Qt/FFmpeg/libplacebo/cubeb source packages | Done | [Ubuntu platform baseline](../../research/2026-08-01-ubuntu-26-04-linux-platform-baseline.md) | Packaging Git clones are unnecessary for installed-package inspection |
| BASE-03 | Record current WSL execution capability | Done | [Ubuntu platform baseline](../../research/2026-08-01-ubuntu-26-04-linux-platform-baseline.md) | WSLg Wayland; llvmpipe CPU Vulkan; no `/dev/dri`, `/dev/dxg`, or usable VAAPI |
| BASE-04 | Rebase plan and implementation onto current upstream and run independent correctness, delivery, and simplicity reviews | Done | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | Deeply reconciled through `5357d96`, including system libass and the shared subtitle pipeline; review findings resolved |
| BASE-05 | Require native Wayland while allowing simple unmanaged assumed-sRGB SDR when managed color is unavailable | Done | [ADR 0018](../../decisions/0018-support-unmanaged-srgb-wayland-sdr.md) | Managed gamma-2.2 SDR and extended-linear HDR have separate capability gates |

## 1. System build foundation

| ID | Gate | Status | Evidence | Blocker or notes |
| --- | --- | --- | --- | --- |
| BUILD-01 | Make Ubuntu system packages the sole initial Linux dependency path while retaining Windows vcpkg auto-discovery | Done | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | No provider abstraction; an explicit future Linux toolchain remains possible but unsupported |
| BUILD-02 | Discover Qt `>=6.10,<6.11`, matching Base/Declarative/Wayland private targets, Shader Tools, and the native Wayland plugin | In progress | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | Qt/private targets compile; installed-plugin runtime and mixed-family failure checks remain |
| BUILD-03 | Discover FFmpeg 8 libraries and assert expected ABI majors plus software, VAAPI, and DRM PRIME compile capabilities | Done | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | Runtime hardware remains optional capability |
| BUILD-04 | Discover libplacebo API 360 through pkg-config and assert Vulkan, shader compiler, built-in DOVI, and observed LCMS state | Done | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | glslang satisfies the shader-backend contract; ICC policy is tracked separately |
| BUILD-05 | Discover distro cubeb through its CMake target and system libass through the shared pkg-config wrapper | Done | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | Common cubeb ABI links without a sound server; real libass rendering uses the embedded font fixture |
| BUILD-06 | Generate only the required color-management-v1 client interfaces from system Wayland protocols | In progress | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | Qt's generator is proven in the contract test; move the same generation to the display adapter when it exists |
| BUILD-07 | Clear effective render-copy ICC handles while retaining source metadata and diagnostics | Pending | — | Required because system libplacebo enables LCMS |
| BUILD-08 | Add an ICC-tagged regression proving Linux retains the accepted no-source-ICC-transform policy | Pending | — | Must exercise the production render boundary |
| BUILD-09 | Complete the Windows-gated test migration inventory below; no Linux dependency behavior is silently skipped | In progress | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | Shared dependency and subtitle/media tests migrated; native GPU, audio-sink, and application scenarios await their implementations |
| BUILD-10 | Clean Linux Debug and Release configure/build with `BUILD_TESTING=ON` and `OFF` | Done | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | All four combinations pass on WSL |
| BUILD-11 | Pass all platform-neutral CTests and QML lint on Linux | Done | [System dependency foundation](linux-port-evidence/2026-08-01-system-dependency-foundation.md) | Twenty-two CTests and both QML lint targets pass, including subtitle state, FFmpeg subtitle decoding, and real libass rendering |
| BUILD-12 | Re-run the supported Windows build/tests after CMake and shared-policy changes | Pending | — | Protect the existing product path |

## 2. Native Wayland Vulkan SDR

| ID | Gate | Status | Evidence | Blocker or notes |
| --- | --- | --- | --- | --- |
| SDR-01 | Select Wayland before `QGuiApplication`, require the native `wayland` QPA, and inventory optional managed-SDR and HDR capabilities | Pending | — | Fail XCB/XWayland; inability to declare managed SDR selects unmanaged assumed-sRGB SDR |
| SDR-02 | Add a typed window-scoped Vulkan context installed before native-surface creation | Pending | — | Context outlives recoverable device domains |
| SDR-03 | Prove final teardown: engine/domain, native `QWindow` surface, then `QVulkanInstance` | Decision needed | — | Default derived/base destruction order is insufficient |
| SDR-04 | Spike shared-device creation direction and record the accepted feature/extension/queue contract in an ADR | Decision needed | — | Try standard QRhi ownership first; use libplacebo ownership only if a measured feature requirement forces it |
| SDR-05 | Spike producer-to-fragment-sampler synchronization and record the accepted layout/barrier/semaphore contract in an ADR | Decision needed | — | Qt 6.10 external wait stage is too late for fragment sampling |
| SDR-06 | Implement the direct QRhi-owned RGBA16F libplacebo target with canonical accepted and aborted states | Pending | — | No normal CPU copy or `vkQueueWaitIdle` |
| SDR-07 | Play representative software-decoded SDR through FFmpeg, libplacebo, QRhi, and Qt Quick on unmanaged assumed-sRGB and managed gamma-2.2 Wayland surfaces | Pending | — | Unmanaged is the baseline; exercise managed SDR whenever it can be declared independently of HDR support |
| SDR-08 | Preserve the domain/media operation across resize, minimize/restore, scale/DPR, expose, and present-compatible surface recreation | Pending | — | Rebuild only surface-bound resources |
| SDR-09 | Publish physical device, queue, format/layout, generation, software path, copy count, and fallback diagnostics | Pending | — | Required before support claims |
| SDR-10 | Pass Vulkan standard and synchronization validation for normal, aborted, resize, surface-recreation, and teardown paths | Pending | — | llvmpipe/WSL proof first; native GPU proof later |
| SDR-11 | Pass focused video-only native-Wayland fullscreen and restoration transitions | Pending | — | Require asynchronous convergence, continued frames, and unchanged media/device generations; the audio-bearing application scenario follows in slice 3 |
| SDR-12 | Prove inability to declare managed gamma-2.2 SDR selects unmanaged assumed-sRGB with managed color and HDR diagnosed unavailable | Pending | — | Missing HDR-only capabilities retain managed SDR; no extra surface object or retry loop |
| SDR-13 | Classify compositor connection loss as a clear process-fatal result | Pending | — | No inert graphics retry loop |
| SDR-14 | Prove the final compositor emits piecewise sRGB for unmanaged SDR and `linear^(1/2.2)` for Qt-managed `gamma22` SDR | Pending | — | Analytic near-black, mid-gray, and endpoint checks must distinguish the curves and exponent direction |

## 3. Linux system-cubeb audio

| ID | Gate | Status | Evidence | Blocker or notes |
| --- | --- | --- | --- | --- |
| AUDIO-01 | Isolate Windows COM and forced-WASAPI setup from the shared cubeb sink | Pending | — | Keep callback and clock state machine shared |
| AUDIO-02 | Instantiate distro cubeb on Linux without naming a backend and report its selected backend ID | Pending | — | Preserve system-default-route policy |
| AUDIO-03 | Pass dependency and sink lifecycle tests on Linux | Pending | — | Open/start/pause/reset/drain/destruction and presented position |
| AUDIO-04 | Pass real application A/V playback and `application-playback` on native Wayland | Pending | — | Requires advancing current-generation cubeb audio clock |
| AUDIO-05 | Validate PulseAudio behavior including route loss/change and service interruption | Needs native hardware | — | Record server and device details |
| AUDIO-06 | Validate PipeWire-Pulse behavior including route loss/change and service interruption | Needs native hardware | — | No native PipeWire sink unless measured cubeb failure justifies it |
| AUDIO-07 | Validate Bluetooth reconnect and suspend/resume without stale clock progression | Needs native hardware | — | Bounded recovery only |
| AUDIO-08 | Register and pass the production audio-bearing `application-fullscreen` scenario on native Wayland | Pending | — | Require F11/double-click/Escape/Space, cursor state, continued frames, restoration, and unchanged media/device generations |

## 4. Managed output and display transitions

| ID | Gate | Status | Evidence | Blocker or notes |
| --- | --- | --- | --- | --- |
| DISP-01 | Inventory color-management-v1, parametric descriptions, named sRGB primaries, Qt 6.10's `gamma22`/`ext_linear` transfer functions, and surface feedback | Pending | — | Qt remains declaration owner; gate managed SDR and HDR from the capabilities each actually needs |
| DISP-02 | Parse completed preferred descriptions into semantic display state and ignore equivalent updates | Pending | — | Protocol identities remain adapter-local |
| DISP-03 | Keep Qt as the sole color-management surface and image-description owner | Pending | — | Use `QSurfaceFormat::setColorSpace`; the adapter observes only missing preferred-output information |
| DISP-04 | Implement coupled gamma-2.2 SDR → extended-linear FP16 surface recreation | Pending | — | Qt color-space request and buffer encoding change together; transient convergence is acceptable |
| DISP-05 | Implement bounded HDR rollback by recreating Qt's gamma-2.2 `SystemManaged` SDR surface | Pending | — | Suppress repeat HDR attempts for the same semantic target/device generation; retry only when either materially changes |
| DISP-06 | Rerender the retained current frame when effective target values change, including while paused | Pending | — | Reconcile at the existing safe frame boundary |
| DISP-07 | Validate windowed movement between SDR and HDR outputs | Needs native hardware | — | Include both transition directions and stable final diagnostics |
| DISP-08 | Validate fullscreen entry when the compositor selects/configures another output | Needs native hardware | — | Preserve media/device state and refresh feedback after convergence |
| DISP-09 | Validate fullscreen movement and normal/maximized restoration across SDR/HDR outputs | Needs native hardware | — | Include F11, Escape, double-click, cursor and transport behavior |
| DISP-10 | Validate HDR enable/disable, preferred-description change, hotplug, reconnect, scale/DPR, and surface recreation | Needs native hardware | — | No stale target or protocol lifetime |
| DISP-11 | Validate 80-nit reference white, headroom response, compositor mapping, and gamma-2.2 SDR rollback with a documented physical procedure | Needs native hardware | — | Record patterns, expected values, tolerances, compositor, driver, GPU, display, and measurement method |
| DISP-12 | Record tested compositor/GPU combinations and scope the HDR support statement to them | Needs native hardware | — | A second combination is required before a broad Ubuntu HDR statement |

## 5. VAAPI/DRM PRIME acceleration

| ID | Gate | Status | Evidence | Blocker or notes |
| --- | --- | --- | --- | --- |
| HW-01 | Match the selected Vulkan physical device to its DRM render node using stable device identity | Pending | — | Unprovable identity disables hardware decode |
| HW-02 | Create the FFmpeg VAAPI device on that exact render node | Pending | — | Do not let FFmpeg select an unrelated default adapter |
| HW-03 | Map retained VAAPI frames to DRM PRIME and import through libplacebo's FFmpeg helper | Pending | — | Retained `AVFrame` owns descriptors until GPU completion |
| HW-04 | Diagnose modifiers, planes, formats, waits, transfers, copies, and bounded software fallback | Pending | — | No duplicate dma-buf model yet |
| HW-05 | Pass NV12 and P010 import, seek/flush, device loss, teardown, mismatch, and forced-failure scenarios | Pending | — | Hardware path requires zero CPU pixel transfers |
| HW-06 | Validate Intel ANV/iHD on native Ubuntu hardware | Needs native hardware | — | Record kernel, Mesa, Vulkan, VA driver, GPU, and fixtures |
| HW-07 | Validate AMD RADV/radeonsi on native Ubuntu hardware | Needs native hardware | — | Record kernel, Mesa, Vulkan, VA driver, GPU, and fixtures |

## 6. Reliability and Ubuntu delivery

| ID | Gate | Status | Evidence | Blocker or notes |
| --- | --- | --- | --- | --- |
| REL-01 | Stress repeated open/close, long playback, seek, fullscreen, minimize, surface recreation, output changes, suspend, and bounded recovery | Pending | — | Separate WSL/CI and native-hardware evidence |
| REL-02 | Choose and record the Ubuntu artifact/package format | Decision needed | — | `.deb` is the leading system-dependency option, not yet accepted |
| REL-03 | Separate build dependencies from runtime dependencies and generate shared-library ABI dependencies where the format supports it | Pending | — | Private Qt development packages must not leak into runtime metadata |
| REL-04 | Prove the installed artifact uses system Qt/FFmpeg/libplacebo/libass/cubeb and the matching Qt Wayland plugin across unmanaged SDR and capability-gated managed presentation | Pending | — | No bundled copies or XCB/XWayland fallback |
| REL-05 | Install through the selected package mechanism on a clean Ubuntu 26.04 machine | Pending | — | Launch and both SDR application scenarios must pass |
| REL-06 | Record distro FFmpeg configuration, runtime closure, licenses/notices, and source-offer obligations | Pending | — | Release gate |
| REL-07 | Synchronize subsystem docs, testing matrix, root roadmap, ADRs, and deferred gaps with delivered behavior | Pending | — | Do not update implementation truth early |

## Windows-gated test migration inventory

| Current test/boundary | Linux disposition | Status |
| --- | --- | --- |
| `sunroom_cubeb_dependency_tests` | Reuse with platform-shaped ABI/backend assertions | In progress: shared ABI test passes; selected Linux backend belongs to AUDIO-02 |
| `sunroom_cubeb_audio_sink_tests` | Reuse shared lifecycle/clock cases; isolate only native Windows control-thread setup | Pending |
| `sunroom_ffmpeg_dependency_tests` | Reuse with Linux FFmpeg ABI plus VAAPI/DRM PRIME assertions instead of D3D11/no-Vulkan assumptions | Done |
| `sunroom_libplacebo_dependency_tests` | Reuse with API 360, Vulkan, shader-backend, DOVI, LCMS-observation, and explicit ICC-policy assertions | In progress: dependency features pass; BUILD-07/08 own ICC policy |
| `sunroom_libass_dependency_tests` and platform-neutral subtitle/media tests | Reuse system libass plus the same embedded ASS/SubRip/PGS fixtures and session behavior | Done: system libass rendering and shared subtitle decode/state behavior pass; QRhi subtitle capture awaits the Linux graphics domain |
| D3D11 target/import/capture tests | Keep Windows-only and add paired Vulkan target/import/capture coverage through the same public behavior | Pending |
| `application-playback` | Register on Linux once Vulkan and cubeb production paths exist | Pending |
| `application-fullscreen` | Register on Linux and wait for asynchronous Wayland state/presentation convergence | Pending |

## Environment and claim ledger

| Environment or claim | Status | Required evidence |
| --- | --- | --- |
| WSL system-dependency build | Done | [Debug/Release, tests on/off, shared tests, and dependency versions](linux-port-evidence/2026-08-01-system-dependency-foundation.md) |
| WSLg native-Wayland lifecycle | Pending | Unmanaged assumed-sRGB SDR is sufficient; explicit software-Vulkan opt-in and no GPU/managed-color/HDR claim |
| Native-Wayland SDR video | Pending | Unmanaged assumed-sRGB baseline plus managed gamma-2.2 coverage where available, real GPU, software decode, direct target, validation, diagnostics |
| Linux cubeb audio | Pending | Real advancing audio plus PulseAudio and PipeWire-Pulse behavior |
| Linux fullscreen | Pending | Production scenario plus output/surface transitions without generation replacement |
| Complete Linux Wayland SDR | Pending | Unmanaged assumed-sRGB and available managed gamma-2.2 video, audio, fullscreen, clean install, native Wayland only |
| Managed Linux HDR | Needs native hardware | Coupled declaration/FP16 path, physical procedure, display transitions, gamma-2.2 SDR rollback |
| Intel VAAPI acceleration | Needs native hardware | Exact-device DRM PRIME import, NV12/P010, zero CPU transfer and no extra full-frame input copy, software fallback |
| AMD VAAPI acceleration | Needs native hardware | Exact-device DRM PRIME import, NV12/P010, zero CPU transfer and no extra full-frame input copy, software fallback |
| Ubuntu 26.04 distributable | Decision needed | Chosen format, clean install, ABI dependencies, license closure, runtime scenarios |
