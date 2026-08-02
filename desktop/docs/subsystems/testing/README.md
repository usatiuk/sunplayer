# Testing subsystem

## Status

Sunroom has CTest/Qt Test targets for pure presentation-target policy,
video-viewport state, rendered-video surface validity/reuse, and real
headless D3D11 QRhi and libplacebo producer/compositor capture. A non-presenting Qt Quick
component test covers the real QML shell's initial-property and viewport
publication and Player/HDR-Lab routing contract. Dependency tests verify the
pinned installed libplacebo and FFmpeg configurations across the
MSVC-to-clang-cl DLL boundary. Pinned, hashed lossless RGB, compressed BT.709
YUV, and Main10 HDR fixtures cross real FFmpeg demux/decode and libplacebo
upload/render. The HDR corpus covers static PQ, HLG, two-scene HDR10+, and
Dolby Vision Profile 8.1; the three-frame SDR fixtures also cross the
continuous packet/decode state machine, including D3D11VA surface retention.
Stop-aware queue tests cover hard
capacity, backpressure, and generation/cancellation wakeups. Session tests
cover real continuous playback, controlled timestamp advancement, play/pause,
drain/end, nonzero seek, paused/playing intent across seek, seek-to-end,
position-preserving fallback, cancellation, stale-generation rejection,
destruction, and presentation failure. A no-window application mode loads the
packaged QML module with its production type registrations. Sunroom has narrow
registered startup and playback scenarios on both current platforms plus a
registered Windows fullscreen scenario. Linux fullscreen is run explicitly
while its WSLg/compositor convergence remains unstable. There is not yet a
broad media corpus, golden-image suite, or general application-scenario suite.
The accepted testing direction is defined in
[../../TESTING.md](../../TESTING.md), and active bootstrap work is tracked in
[PLAN.md](PLAN.md).

Testing begins with the current presentation boundaries rather than waiting for
the whole player:

* Display-target and SDR-white policy.
* Active video-viewport geometry and visibility.
* Render-surface device/display-generation validity.
* A real D3D11 QRhi offscreen composition readback.
* The real pinned libplacebo DLL, installed feature configuration, shared
  D3D11 GPU/texture lifecycle, analytic SDR/PQ rendering, and final
  composition.
* The real pinned FFmpeg DLLs, software-frame ownership, complete
  send/receive/flush decoding, stream-default precedence, hardware-plane
  description, software upload, target-only rerender reuse, and final
  composition.
* A future recorded Windows SDR/HDR manual matrix.

As media subsystems arrive, coverage moves outward through real libplacebo,
FFmpeg, libass, scheduling, audio, and the application process.

## Responsibilities

The testing subsystem will own shared test infrastructure:

* CTest registration, labels, and test-environment setup.
* Qt Test support and reusable deterministic test helpers.
* Controlled clock, display, audio, source, and fault-injection adapters.
* Purpose-built media fixtures, manifests, hashes, and provenance.
* GPU capture, reference-image, and comparison contracts.
* Future actual-application scenario orchestration.
* Capability detection and explicit skipped-coverage reporting.
* Manual and automated hardware-lab matrices.

Each product subsystem remains responsible for defining its important
invariants, exposing sufficient observation, and adding scenarios with the
feature or bug they cover.

## Initial tooling

The first implementation should use:

* CTest as the build-system-level registry and runner.
* Qt Test for data-driven C++ policy and state tests.
* Build-local staging of Windows runtime DLLs so automated test startup cannot
  display missing-library dialogs.
* CTest labels to distinguish deterministic, GPU, platform, slow, and manual
  coverage as those classes appear.

Qt Quick Test may be added when page selection, commands, or other isolated QML
behavior exists to justify it. OpenEXR/OpenImageIO, a local process-control
channel, and dedicated scenario syntax remain candidates to adopt when a
concrete test needs them.

Pure policy tests use Qt Test's `QTEST_APPLESS_MAIN`. The QRhi integration test
uses `QTEST_GUILESS_MAIN`, not generic `QTEST_MAIN`: linking Qt GUI for QRhi
would make the generic macro instantiate `QGuiApplication` and load the GUI
platform stack, while the offscreen test needs only `QCoreApplication`
lifetime. Qt Test's public static `initMain()` hook applies the Windows
noninteractive error mode before application construction.

The libplacebo and FFmpeg dependency tests use `QTEST_APPLESS_MAIN`. They
exercise public dependency APIs rather than mocks, while remaining
intentionally below the GPU-rendering boundary. The QRhi integration test's
second case crosses that boundary with the production D3D11 domain and
libplacebo renderer. The FFmpeg video integration test uses
`QTEST_GUILESS_MAIN` for the same headless QRhi reason.

The media-session test uses `QTEST_GUILESS_MAIN` because worker completion is
delivered through queued Qt events and therefore needs `QCoreApplication`,
without loading a GUI platform plugin.

The FFmpeg video integration target retains its historical
`ffmpeg-first-frame` CTest name and is labeled `hardware-decode` in addition to
its FFmpeg/GPU/Windows labels. Direct manual execution reports an explicit Qt
Test skip when the machine lacks that capability. The registered CTest target
sets `SUNROOM_REQUIRE_D3D11VA=1`, so a missing capability fails instead of
silently appearing as covered.

The QML shell component test uses `QTEST_MAIN` because Qt Quick Controls and
its hidden `QQuickWindow` scene require `QGuiApplication`. It selects and
stages Qt's offscreen platform plugin before application construction and
selects the deterministic Basic Controls style. It adds the configured Qt QML
module root to its engine and CTest prepends Qt's configured binary directory
for QML-plugin dependencies; it never shows a native window. Correct
build-local DLL and platform-plugin staging remains necessary because loader
failures can occur before test code runs.

## Test seams

Use narrow controlled seams only around nondeterministic or physical edges.
Early likely seams are:

* A pure presentation-target calculation separated from Qt/WinRT observation.
* Injectable `DisplayStateProvider` snapshots.
* An explicit graphics-device generation and complete semantic target values
  on rendered surfaces.
* Production source/producer/target contracts with readback enabled only for
  the real offscreen GPU test.
* An injected pipeline operation for deterministic cancellation and
  stale-completion tests; real FFmpeg remains the default and covers success.
* A controlled `MediaClockSnapshot` passed through the production scheduler at
  the `prepareForPresentation` selector seam.

The controlled and physical audio sinks both produce the shared
`AudioPresentationSnapshot`. Production scheduling consumes it through the
clock-source-neutral frame selector; the controlled sink substitutes only the
physical device edge in deterministic session scenarios.
Later seams include a source-fault adapter and test-control endpoint. They
should arrive with the subsystem behavior they enable rather than as a
speculative framework.

## Verification

Current verified coverage:

| Boundary | State |
| --- | --- |
| Configured Windows Debug build | Builds successfully |
| Focused automated tests | Twenty-eight CTest targets cover presentation policy, UI/session lifecycle, bounded media/audio/subtitle queues and state, timing and generation behavior, dependency boundaries, real FFmpeg A/V/subtitle decode, libswresample/libass rendering, a silent real-WASAPI sink lifecycle, real GPU paths, and bounded application playback/fullscreen scenarios |
| Configured Ubuntu system build | The prior checkpoint covers Debug and Release with tests enabled and disabled. This change reruns Debug with tests and Release without tests using Qt 6.10.2 plus system media/native dependencies; the production player has native-Wayland Vulkan and system-cubeb paths |
| Linux test suite | All 26 registered CTests and QML lint pass under WSL, including the system-cubeb sink lifecycle, real audio-first application playback, platform-neutral subtitle/media behavior, system dependency contracts, managed/unmanaged Wayland SDR selection, application-chrome state, and packaged QML; final compositor pixel capture, native route-change/acoustic evidence, VAAPI import, native GPU, and HDR claims remain gated |
| WSLg production boundary | A prior installed Release video-only run completed fullscreen/restoration; two cursor-state timeouts kept acceptance open. The current audio-first playback run passes, while the current explicit audio-bearing fullscreen run ended in an unresolved buffer/configure protocol failure before its final assertion |
| Linux dependency boundary | Shared FFmpeg, libplacebo, cubeb, and libass tests use platform-shaped assertions; a Linux-only link test covers Vulkan, Qt private Wayland, generated color-management-v1 client code, Wayland client, VA-API DRM, and DRM without opening native resources |
| Audio callback boundary | Whole-block SPSC publication, sticky cancellation, bounded output-to-media mapping, hold silence, generation-safe drain, and the max-block/preroll deadlock regression pass deterministically |
| Real Windows audio boundary | The pinned cubeb WASAPI backend opens the default endpoint on a dedicated MTA thread and repeatedly passes silent start, presented-position observation, pause, reset, drain, and destruction |
| Real Linux audio boundary | Ubuntu system cubeb selects WSLg's Pulse-compatible server, opens the null/default route, and passes the same start, presented-position, pause, reset, drain, and destruction scenario; the real application advances its audio-master clock through that route, and user-confirmed real-file playback is audible |
| libplacebo dependency boundary | The real DLL loads; pinned version, installed D3D11/Shaderc/built-in-DOVI configuration, disabled Vulkan/OpenGL/external-libdovi features, runtime staging, and log lifecycle pass |
| FFmpeg dependency boundary | The four selected DLLs load; pinned major versions, D3D11VA, native H.264/HEVC decoders, libswresample, disabled Vulkan/swscale configuration, and explicit runtime staging pass |
| Real QRhi/libplacebo capture | Factory-selected D3D11 domain; shared QRhi device and immediate context; persistent libplacebo renderer; fixed-size persistent software input; distinct per-input-frame and per-output-render transfer diagnostics; direct wrapped RGBA16F target; aligned pattern layout; relative sRGB with an explicit 100-nit mastering maximum plus stale HDR10+/CIE-Y luminance normalized at 80, 100, and 203-nit output reference whites; an exact 203-nit analytic PQ patch at surface `1.0`; one fixed analytic 1000-nit PQ signal against one 600-nit target at those reference-white levels; source-upload reuse across target-only changes; no expansion while the source fits; compression into reduced headroom; exactly one final reference-white-to-scRGB scale; minimum-target value/known-state contract; zero output copies; pixel-validated resize/rewrap and producer rebinding; final composition readbacks; and a non-gating 60-frame throughput probe pass. This validates the SDR/static-PQ numerical bridge; the FFmpeg video row separately covers HLG and dynamic-HDR input behavior. Source ICC and emitted luminance remain unvalidated. |
| FFmpeg video pipeline | Manifest-enforced SHA-256 for pinned PPM, Matroska/FFV1, Matroska/H.264, and raw Main10 HEVC fixtures; caller-owned unique identities; complete software and D3D11VA drain; retained AVFrame and side-data lifetime; production regressions for missing-frame HDR10+ fallback plus real two-scene HDR10+ progression; static-PQ mastering/content-light retention and analytical patches; HLG target-response capture; parsed and mapped Dolby Vision Profile 8.1 RPU; explicit renderer policy diagnostics; hardware-plane description and generation compatibility; RGB24 and limited-range BT.709 YUV420P software upload; required D3D11VA NV12 direct import on the shared device; stale-generation import classified for software retry; exact YUV and tolerant linear-RGB capture; hardware/software differential capture; final RGB composition; zero hardware input transfer/copy and zero output copy/transfer; deterministic pre-publication fallback-policy test; real-container long-timeline seek beyond the 32-bit-microsecond boundary |
| Embedded subtitle pipeline | One production FFmpeg operation discovers and routes ASS, converted SubRip, and PGS tracks; an embedded deterministic font crosses attachment delivery into real libass rendering; PGS events preserve palette alpha/color, authored multi-region placement, replacement, open-ended display, and clear; the same bitmap regression runs through ordinary and Matroska-zlib-compressed containers; session tests cover Off/selection, current-position restart, seek, and stale-generation rejection; compositor and QML tests cover video-subtitle-UI order and the dynamic track menu |
| Initial media playback | Real off-thread local open plus controlled Opening/Ready/Error; a twelve-frame fixture that fills the three-frame mailbox and proves pause/backpressure, resume/refill, complete drain, replay, exact-zero/nonzero seek, paused/playing intent, seek-to-end, and maximum occupancy; a shared A/V operation with a presented-audio master, timeline-driven readiness, opposite stream-start offsets, pause, seek, generation replacement, clean zero-output audio intervals, drain with a terminal endpoint, no-audio fallback, a shorter-audio video tail, hidden sink failure, ordered output hold and Buffering, sustained clock-loss failure while output continues, and complete real-FFmpeg drain without any presentation consumer; nonseekable replay from a natural zero start; a sparse-GOP H.264/B-frame fixture that proves preceding-keyframe decode and requested-frame publication; exact stable-origin retention; duration-aware and PTS-lookahead preroll filtering; integer timestamp normalization; due-frame dropping; nonblocking cancellation/replacement; shutdown; nonfatal presentation failure; position-preserving one-shot hardware-import software restart; repeated-failure rejection; graphics recovery through the replacement capability for software-backed playback; and latest pending-seek preservation across recovery |
| Actual application playback | Registered CTest launches the built Player on a pinned audio-first FFV1+FLAC fixture and requires production FFmpeg decode, a valid and advancing current-generation default-device Cubeb clock, and two distinct QRhi/libplacebo video revisions reaching the swapchain; the QML component scenario separately protects viewport activation before `hasFrame` |
| Actual application fullscreen | The scenario drives native F11, Escape, Space, and redirected background double-click input through the real `PresentationWindow`, covering blocked Escape, pause/resume, normal/fullscreen/normal, and maximized/fullscreen/maximized while checking idle cursor hiding, continued video presentation, and one advancing cubeb audio-output epoch; it remains a registered but currently unrereun Windows gate and an explicit non-gating Linux run whose current attempt ended before the final assertion |
| Built application startup | The built Player executable also has a registered no-window check for packaged QML and production type registration. The startup and playback scenarios prove native-window, graphics-device, swapchain, playback, and automatic-shutdown paths; the changed fullscreen scenario retains the platform-specific gaps recorded above, and broader command/error/package scenarios remain missing |
| Recorded SDR/HDR runtime matrix | Not implemented |
| Representative compressed-media scenarios | Deterministic Matroska/FFV1 software YUV, Matroska/H.264 D3D11VA NV12, and software-decoded Main10 HEVC PQ/HLG/HDR10+/Dolby Vision fixtures implemented; P010/P012/P016 capture, broader profiles, and other hardware backends remain missing |
| Pinned FFmpeg-decoded mastered PQ source across display targets | Implemented with 50/203/400/1000-nit neutral patches against a six-times-reference-white target; analytic target compression remains covered separately |
| Physical output measurement | Deferred |

Missing coverage must remain visible in this table or the active testing plan
until it is implemented or deliberately removed from scope.

Focused tests are grouped by responsibility under `tests/unit/media/`,
`tests/unit/playback/`, `tests/unit/presentation/`, `tests/unit/ui/`, and
`tests/unit/video/`. The first actual-application scenario is a direct CTest
registration of the production executable because it needs no separate runner;
future scenario runners belong under an integration tree when shared control or
observation code becomes concrete. The first GPU boundary test is under
`tests/integration/presentation/`, and the libplacebo binary-boundary test is
under `tests/integration/video/`.
