# Testing subsystem

## Status

Sunroom has CTest/Qt Test targets for pure presentation-target policy,
video-viewport state, rendered-video surface validity/reuse, and a real
headless D3D11 QRhi producer/compositor capture. A non-presenting Qt Quick
component test covers the real QML shell's initial-property and viewport
publication contract. Sunroom does not yet have a media fixture corpus,
golden-image suite, or automated application scenarios. The accepted testing
direction is defined in
[../../TESTING.md](../../TESTING.md), and active bootstrap work is tracked in
[PLAN.md](PLAN.md).

Testing begins with the current presentation boundaries rather than waiting for
the whole player:

* Display-target and SDR-white policy.
* Active video-viewport geometry and visibility.
* Render-surface device/display-generation validity.
* A real D3D11 QRhi offscreen composition readback.
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
| Focused automated tests | Presentation-target policy, video-viewport state, real QML shell publication, rendered-video surface lifecycle, and target-path diagnostic policy tests pass |
| Real QRhi capture | Factory-selected D3D11 domain, shared source/producer contracts, direct RGBA16F target, resize/revision rebinding, hidden-video fallback, and final SDR/extended-linear composition readbacks pass |
| Built application startup | Automated four-second GUI/device/swapchain liveness smoke passed; not yet a registered scenario |
| Recorded SDR/HDR runtime matrix | Not implemented |
| Media pipeline scenarios | Blocked on media pipeline implementation |
| Physical output measurement | Deferred |

Missing coverage must remain visible in this table or the active testing plan
until it is implemented or deliberately removed from scope.

Focused tests are grouped by responsibility under `tests/unit/presentation/`
`tests/unit/ui/`, and `tests/unit/video/`. Future actual-application scenarios
use sibling trees when their first concrete tests arrive; the first GPU
boundary test is under `tests/integration/presentation/`.
