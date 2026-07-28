# Testing subsystem

## Status

Sunroom has an initial CTest/Qt Test target for pure presentation-target policy.
It does not yet have a fixture corpus, render captures, or automated application
scenarios. The accepted testing direction is defined in
[../../TESTING.md](../../TESTING.md), and active bootstrap work is tracked in
[PLAN.md](PLAN.md).

Testing begins with the current presentation boundaries rather than waiting for
the whole player:

* Display-target and SDR-white policy.
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

Qt Quick Test may be added for isolated QML behavior when the player UI exists.
OpenEXR/OpenImageIO, a local process-control channel, and dedicated scenario
syntax remain candidates to adopt when a concrete test needs them.

## Test seams

Use narrow controlled seams only around nondeterministic or physical edges.
Early likely seams are:

* A pure presentation-target calculation separated from Qt/WinRT observation.
* Injectable `DisplayStateProvider` snapshots.
* Explicit graphics-device and display revisions on rendered surfaces.
* A test-only offscreen QRhi target with supported raw readback.

Later seams include a monotonic clock, audio sink, source-fault adapter, and
test-control endpoint. They should arrive with the subsystem behavior they
enable rather than as a speculative framework.

## Verification

Current verified coverage:

| Boundary | State |
| --- | --- |
| Configured Windows Debug build | Builds successfully |
| Focused automated tests | Presentation-target policy test implemented |
| Real QRhi capture | Not implemented |
| Recorded SDR/HDR runtime matrix | Not implemented |
| Media pipeline scenarios | Blocked on media pipeline implementation |
| Physical output measurement | Deferred |

Missing coverage must remain visible in this table or the active testing plan
until it is implemented or deliberately removed from scope.
