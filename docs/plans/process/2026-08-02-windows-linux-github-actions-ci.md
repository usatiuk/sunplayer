# Windows and Linux GitHub Actions CI

Status: Active

## Goal

Add one understandable GitHub Actions workflow that continuously builds,
lints, and tests Sunroom's current Windows and native-Wayland Linux targets.
CI must fail on real regressions, expose capability gaps honestly, and remain
close to the documented local commands.

The supporting runner, cache, and hosted-capability evidence is recorded in
[the CI research note](../../research/2026-08-02-github-actions-platform-ci.md).

## Accepted design

### Workflow boundary

Create repository-root `.github/workflows/ci.yml` with two independent jobs.
The project has been flattened into the repository root, so commands use `.`
as the CMake source and cache hashes name the root dependency inputs directly:

```text
pull request / main push / manual dispatch
├── Linux · Ubuntu 26.04 system dependencies
└── Windows · Qt 6.11.1 + pinned vcpkg manifest
```

Do not encode these jobs as a matrix. Only configure/build/lint/test command
shape is shared; the useful details are platform-specific and clearer when
written directly.

The workflow will:

* request only `contents: read`;
* pin every referenced action to a reviewed full commit SHA with its release
  version in a comment;
* disable checkout credential persistence because later steps execute pull
  request code and need no authenticated Git operations;
* cancel superseded runs for the same workflow/ref;
* use explicit stable runner labels and bounded job timeouts;
* use Debug builds with `BUILD_TESTING=ON`;
* invoke `all_qmllint` and CTest directly; and
* avoid secrets, deployment, artifact publication, or repository writes.

### Linux job

Run the job in the official `ubuntu:26.04` container on the stable
`ubuntu-24.04` host. Install the same system development packages listed by the
build subsystem plus the runtime packages needed for a headless Wayland,
lavapipe Vulkan, and PulseAudio-null-sink test environment.

Configure and build using Ninja. Lint before running tests. For the test step:

1. select Bash explicitly for container run steps;
2. create a root-owned private `XDG_RUNTIME_DIR` for Weston and Qt;
3. start Weston with an explicit headless backend, pixman renderer, and socket;
4. poll that exact Wayland socket while checking that Weston remains alive;
5. create a separate runtime directory owned by the `pulse` user, then start
   PulseAudio in system mode with one anonymous local protocol socket and one
   null sink;
6. export that exact `PULSE_SERVER`, poll it through `pactl info`, and verify
   that the named null sink exists and is the default;
7. assert and select Ubuntu's packaged `lvp_icd.json`, Qt's Wayland QPA, and
   the explicit Weston socket; and
8. run all registered Linux CTests with failure output.

One trap installed before either daemon starts will preserve the original test
exit status, terminate and wait for both daemons, and print both service logs
on failure. It will not sleep for a guessed startup duration, silently retry
failed tests, or skip the device-backed scenarios.

### Windows job

Run on `windows-2022`, install Qt's exact public 6.11.1 MSVC 2022 package plus
`qtshadertools` with an exact `aqtinstall` version, and cache that immutable Qt
directory.

Use the runner's declared `C:\vcpkg` checkout and the repository's existing
manifest, baseline, overlay ports, and clang-cl triplet. Persist vcpkg's
filesystem binary cache through `actions/cache`; do not cache raw downloads,
the Sunroom build tree, or `vcpkg_installed`. Map the runner's
`VCPKG_INSTALLATION_ROOT` to the `VCPKG_ROOT` consumed by the project and pass
its toolchain plus the cached aqt Qt prefix explicitly to CMake after asserting
both paths. Enter the Visual Studio x64 developer environment for configure and
build, keeping MSVC as the Sunroom compiler while vcpkg's chainload file
selects clang-cl for dependencies.

Run `all_qmllint`, then CTest excluding tests labeled `device` or `gpu`.
Correct the labels on both application audio scenarios because they require a
live default device just as directly as the cubeb sink test. The GPU exclusion
is required because the production D3D11 domain requests hardware and has no
WARP path; do not add a CI-only graphics fallback or test abstraction. Record
that this also excludes the mixed software/hardware `ffmpeg-first-frame`
executable until its registration can be split for a real product reason.

## Implementation steps

1. [x] Add accurate `device` labels to the audio-first and fullscreen application
   tests.
2. [x] Add the two-job workflow with pinned checkout/cache actions, dependency
   provisioning, cache keys, build/lint commands, runtime-service probes, and
   explicit CTest selection.
3. [x] Parse the workflow with available YAML tooling. Treat the first actual
   GitHub-hosted run as the platform-specific syntax and environment gate.
4. [x] Re-run the current Linux build, lint, and CTest suite locally to ensure the
   label and documentation changes do not regress the supported tree.
5. [x] Review the complete diff through correctness, CI failure/security, testing
   honesty, documentation, and simplicity lenses.
6. [x] Update project status and this plan with the validation actually achieved.

## Acceptance

The change is ready for a first hosted run when:

* the workflow is syntactically valid and all action references use full SHAs;
* Linux dependencies match the Ubuntu 26.04 system contract;
* Windows dependencies match Qt 6.11.1 and the existing vcpkg contract;
* both jobs build, run QML lint, and invoke CTest through documented commands;
* hosted-only capability exclusions are label-driven, minimal, and documented;
* service readiness is observable and bounded;
* caches contain no credentials, Sunroom build trees, or `vcpkg_installed`
  tree; only the exact public Qt install and vcpkg's ABI-addressed dependency
  binaries are persisted; and
* project documentation distinguishes local/hosted evidence from physical
  hardware coverage.

A successful actual GitHub-hosted run is required before the plan can be
marked Complete. Until the workflow is pushed and observed, local validation
is implementation evidence rather than hosted acceptance.

## Deliberately deferred

* macOS CI before the product has a macOS backend and dependency contract.
* Release/install/package jobs before packaging decisions exist.
* Scheduled stress, performance, validation-layer, or large-corpus jobs.
* A dedicated Windows GPU/audio runner and physical-lab orchestration.
* Splitting a hosted software subset from the mixed `ffmpeg-first-frame`
  hardware-required registration.
* A Linux VAAPI/DRM PRIME hardware runner, an HDR/display lab, and physical
  audio/default-route scenarios.
* D3D11VA, VAAPI/DRM PRIME, HDR, display-transition, route-switch, and
  acoustic-sync claims on generic hosted runners.
* Workflow factoring, custom images, dependency bots, and artifact retention
  until repetition or a concrete consumer requires them.

## Delivery evidence

Repository flattening is isolated in `137c64a`. The root workflow and corrected
test labels are implemented. PyYAML parses the workflow; all four action uses
are pinned to the reviewed 40-character checkout/cache SHAs; Bash parses every
Linux run script; and the cache hash inputs exist at their new root paths.

A fresh root Debug tree at `/tmp/sunroom-ci-linux-debug` configured successfully,
built all 237 Ninja edges, passed both `all_qmllint` targets, and passed all 26
registered CTests. Source inspection confirms both application scenarios now
carry `device`, while local CTest metadata confirms the audio-first label. The
hosted Windows expression `-LE "device|gpu"` selects the intended shared
deterministic subset (24 tests in the local Linux registration).

Independent correctness, CI security/failure, and simplicity reviews found no
flattening, build-path, cache-policy, label-selection, or overengineering defect.
Their accepted lifecycle and evidence findings were corrected with bounded daemon
shutdown, causal log tails, an explicit post-readiness Weston liveness check, and
this evidence update. `git diff --check` and documentation-link validation pass.

This WSLg host does not have the standalone Weston and PulseAudio packages, and
host packages were not installed for this change. Local CTest therefore used the
existing WSLg Wayland/Pulse route; it did not execute the workflow's controlled
service setup or any Windows job. The first actual GitHub-hosted Windows and Linux
execution remains pending and is required before this plan can become Complete.

The first pushed workflow was rejected during GitHub's static validation because
the `runner` context is unavailable in job-level `env`. The corrected workflow
uses the runtime-provided `RUNNER_TEMP` environment variable inside commands and
retains `${{ runner.temp }}` only in step-level action inputs, where that context
is valid. The next hosted run passed static validation and started both jobs.
Windows then exposed that released `aqtinstall` 3.3.0 predates Qt 6.11's changed
per-architecture repository layout. CI now pins the immutable upstream merge
commit containing the accepted layout fix. Build, lint, and CTest parallelism
initially followed each runner's available processor count. That Linux run built
and linted successfully, then exposed the missing Qt SVG runtime plugin and made
an already-documented timing-sensitive media-session checkpoint recur under broad
CTest contention. CI now installs `qt6-svg-plugins`, retains processor-wide
build/lint parallelism, and restores the established two-test CTest bound without
retries or skips. Another hosted rerun remains the validation boundary for these
corrections.
