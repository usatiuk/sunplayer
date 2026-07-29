# Testing subsystem

## Status

Sunroom has CTest/Qt Test targets for pure presentation-target policy,
video-viewport state, rendered-video surface validity/reuse, and real
headless D3D11 QRhi and libplacebo producer/compositor capture. A non-presenting Qt Quick
component test covers the real QML shell's initial-property and viewport
publication contract. Dependency tests verify the pinned installed libplacebo
and FFmpeg configurations across the MSVC-to-clang-cl DLL boundary. A first
pinned, hashed lossless RGB fixture crosses real FFmpeg demux/decode,
libplacebo upload, and final QRhi composition. Sunroom does not yet have a
representative compressed-media corpus, golden-image suite, or automated
application scenarios. The accepted testing
direction is defined in
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
* The real pinned FFmpeg DLLs, software-frame ownership, first-frame decode,
  stream-default precedence, hardware-plane description, software upload,
  target-only rerender reuse, and final composition.
* A recorded Windows SDR/HDR manual matrix.

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
libplacebo renderer. The first-frame integration test uses
`QTEST_GUILESS_MAIN` for the same headless QRhi reason.

The QML shell component test uses `QTEST_MAIN` because Qt Quick Controls and
its hidden `QQuickWindow` scene require `QGuiApplication`. It never shows a
native window. Correct build-local DLL staging remains necessary because
loader failures occur before test code can run.

## Test seams

Use narrow controlled seams only around nondeterministic or physical edges.
Early likely seams are:

* A pure presentation-target calculation separated from Qt/WinRT observation.
* Injectable `DisplayStateProvider` snapshots.
* Explicit graphics-device and display revisions on rendered surfaces.
* Production source/producer/target contracts with readback enabled only for
  the real offscreen GPU test.

Later seams include a monotonic clock, audio sink, source-fault adapter, and
test-control endpoint. They should arrive with the subsystem behavior they
enable rather than as a speculative framework.

## Verification

Current verified coverage:

| Boundary | State |
| --- | --- |
| Configured Windows Debug build | Builds successfully |
| Focused automated tests | Nine CTest targets pass, covering presentation-target policy, video-viewport state, real QML shell publication, rendered-video and decoded-frame lifecycle, two dependency boundaries, and two real GPU paths |
| libplacebo dependency boundary | The real DLL loads; pinned version, installed D3D11/Shaderc/built-in-DOVI configuration, disabled Vulkan/OpenGL/external-libdovi features, runtime staging, and log lifecycle pass |
| FFmpeg dependency boundary | The three selected DLLs load; pinned major versions, D3D11VA, native H.264/HEVC decoders, disabled Vulkan/swscale configuration, and explicit runtime staging pass |
| Real QRhi/libplacebo capture | Factory-selected D3D11 domain; shared QRhi device and immediate context; persistent libplacebo renderer; fixed-size persistent software input; distinct per-input-frame and per-output-render transfer diagnostics; direct wrapped RGBA16F target; aligned pattern layout; sRGB normalization at 80, 100, and 203 nits; target-relative PQ normalization at 100 and 203 nits; minimum-target value/known-state contract; zero output copies; pixel-validated resize/rewrap and producer rebinding; final composition readbacks; and a non-gating 60-frame throughput probe pass |
| First decoded frame | Manifest-enforced SHA-256 for the pinned PPM fixture; caller-owned unique identities; real demux/decode; retained AVFrame and side-data lifetime; stream/frame metadata precedence; hardware-plane description and generation compatibility; RGB24 software upload; known pixel capture; final composition; one input upload reused across 203/100-nit target rerender; zero input download/GPU copy and zero output copy |
| Built application startup | Automated four-second GUI/device/swapchain liveness smoke passed; not yet a registered scenario |
| Recorded SDR/HDR runtime matrix | Not implemented |
| Representative compressed-media scenarios | Not implemented; the current lossless image fixture proves only the first-frame boundary |
| Fixed mastered PQ source across display targets | Not implemented |
| Physical output measurement | Deferred |

Missing coverage must remain visible in this table or the active testing plan
until it is implemented or deliberately removed from scope.

Focused tests are grouped by responsibility under `tests/unit/media/`,
`tests/unit/presentation/`, `tests/unit/ui/`, and `tests/unit/video/`. Future
actual-application scenarios
use sibling trees when their first concrete tests arrive; the first GPU
boundary test is under `tests/integration/presentation/`, and the libplacebo
binary-boundary test is under `tests/integration/video/`.
