# Testing strategy

## Status and intent

This document defines Sunroom's testing direction. It is an architectural
guide, not a requirement to build every layer before the corresponding player
feature exists.

Sunroom integrates timing, concurrency, color metadata, native GPU resources,
operating-system display state, and physical output. A large suite of tests
against mocked collaborators could be fast while proving little about those
boundaries. The default regression test should therefore exercise the highest
practical public boundary with real dependencies and representative data.

The practical boundary changes as the player grows. Today the repository has
Windows, Apple-Silicon macOS, and native-Wayland QRhi presentation paths plus
asynchronous synchronized A/V playback,
so useful tests cover display-target policy, resource-generation contracts,
real QRhi composition, bounded frame-mailbox backpressure, stop-aware packet
channels, timestamp-driven selection, active-source/session lifecycle, and
retained software/D3D11VA `AVFrame`s, single-pass A/V decode and resampling,
callback-safe PCM/metadata buffering, and real cubeb default-route lifecycle
boundaries on Windows, macOS, and Linux. A deterministic production-session scenario
now uses presented audio
as the video scheduler's clock. Embedded-subtitle coverage crosses the same
single FFmpeg operation, real libass and bitmap rendering, the QML track menu,
and final QRhi composition. A registered real-process scenario additionally
crosses the production executable, default audio device, QML viewport,
libplacebo render, and swapchain for an audio-first source. As buffering and
device recovery arrive, deterministic whole-pipeline scenarios should become
the bulk of behavioral coverage.

The Ubuntu system build now compiles that shared media, subtitle, playback,
and UI graph plus the native-Wayland Vulkan path. It passes 26 Linux CTests and
QML lint, including a device-backed system-cubeb lifecycle, real audio-first
application playback, FFmpeg/subtitle fixtures, system dependencies, exact
unmanaged-sRGB versus managed-gamma-2.2 surface encoding, version-2 managed
HDR10 capability/stable-mode selection and bounded rejection, and
application-chrome layout/state behavior. A WSLg production scenario exercises
the real Qt Wayland window, llvmpipe Vulkan device, libplacebo direct target,
redirected QML, swapchain, fullscreen/restoration, and teardown under Vulkan
validation.
The QML shell scenario also protects the full-root application outline's
media-independent visibility and DPR-derived thickness. The original missing-
outline regression is additionally covered by an interactive production WSLg
check because the component test cannot observe the redirected GPU texture.
One bounded run completes; two other attempts timed out on cursor-state
convergence, so this is narrow path evidence rather than complete WSLg
lifecycle acceptance.
WSLg's Pulse-compatible cubeb output and advancing application audio clock are
proven, and a user-confirmed real-file run is audible through WSLg. Native
PulseAudio/PipeWire-Pulse acoustic output and live default-route switching,
native GPU behavior, VAAPI import, live managed gamma-2.2 declaration, and HDR
display behavior remain unproven. The production Linux build compiles the
shared final BT.709-to-BT.2020/PQ shader. Its analytic GPU pixel oracle remains
inside the Windows-only real-compositor test and covers neutral reference white,
signed pre-matrix input/post-matrix clamping, and ordinary color. This is not
Linux HDR10 WSI or native-output evidence.

The Apple M2/macOS 26 lane exercises the real Metal QRhi device, same-device
libplacebo Vulkan/MoltenVK target, direct RGBA16F Metal texture import,
GPU-only handoff plus pre-submission target replacement, VideoToolbox
NV12/P010 plane import, AudioUnit output, subtitle composition, seeking, and
production application playback. Deterministic captures cover SDR
and the shared PQ, HLG, HDR10+, and Dolby Vision Profile 8.1 tone-mapping path.
The connected built-in display reported current EDR headroom `1.0` and
potential headroom `2.0`. A user-confirmed multi-display move and return now
preserve video, UI color, and EDR mapping; physical extended-output
measurement, live HDR/SDR switching, a broader display matrix, and live
default-audio-route movement remain native-hardware checks. Playback is
user-confirmed audible on the current default device. The
post-transition-fix and post-review Debug suite passes all 26 registered tests
in 24.55 seconds; every registered application scenario exits itself. Bundle
closure and macOS 13 dependency
compatibility remain packaging checks.

The central principle is:

> Optimize for meaningful behaviors that can be reproduced, not the number of
> tests or conformance to a fixed test pyramid.

## Coverage shape

Sunroom aims for complementary layers rather than a prescribed ratio:

```text
          Guided or automated physical display lab
        Real operating-system, GPU, device, and monitor tests
      Actual application scenarios through a private control channel
    Deterministic subsystem and whole-pipeline scenarios with real libraries
  Focused tests for dense, deterministic policy, state, math, and concurrency
```

Higher layers prove more of the deployed system but cost more and can have
noisier failure signals. Lower layers remain valuable where they provide a
stronger oracle, faster exploration, or deterministic coverage of many state
combinations.

### Hosted CI capability boundary

The root GitHub Actions workflow is configured as two direct platform jobs.
Linux uses Ubuntu 26.04 system dependencies plus headless native Wayland,
lavapipe Vulkan, and a PulseAudio null sink, and intends to run all registered
Linux CTests. That lane exercises production interfaces against controlled
software services without claiming native GPU, VAAPI/DRM PRIME import,
color-management-v1, HDR, physical display/audio, default-route migration, or
acoustic A/V synchronization.

Windows builds all production and test targets with exact Qt 6.11.1 and the
root vcpkg manifest, runs QML lint, and runs CTest with `-LE "device|gpu"`.
The exclusion reflects the production hardware-only D3D11 device and live
default-audio-device contracts; no WARP or CI-only product fallback exists.
It also excludes the complete mixed `ffmpeg-first-frame` registration,
including its software/HDR cases, until a useful hosted software subset is
split from the hardware-required executable.

This workflow is implemented and locally validated configuration. A first
successful GitHub-hosted run remains required before either lane is recorded
as passing hosted evidence.

Use the highest boundary that:

* Exercises the behavior that can realistically fail.
* Produces a deterministic and diagnostically useful result.
* Has acceptable execution cost for its intended cadence.

A real dependency is preferred when testing its integration contract. It is
not automatically useful when unrelated hardware or timing noise obscures the
assertion.

## General rules

* Use real FFmpeg, libplacebo, libass, queues, scheduling, and QRhi composition
  when the scenario is intended to validate their integration.
* Replace only the edge that needs control: for example the monotonic clock,
  physical audio sink, display provider, unreliable byte source, or physical
  monitor.
* Wait for explicit completion or state transitions. Do not make correctness
  depend on a fixed sleep.
* Assert what the player did through public state, structured diagnostics,
  captured output, and externally visible behavior.
* Keep queues, retries, timeouts, and resource pools bounded and observable so
  their invariants can be asserted.
* Record the exact backend, device, dependency versions, fixture identity, and
  relevant capability state for environment-dependent results.
* Treat a missing hardware capability as reported missing coverage, not a
  silent pass.
* Automated tests must not display modal application, loader, crash-reporting,
  or operating-system dialogs. Configure runtime dependencies and platform
  error handling so failures are captured by the runner and terminate with a
  useful nonzero result.
* Add a reproducible regression for a significant bug when practical. When it
  is not practical yet, document the coverage gap and the manual reproduction.

## Framework and tooling direction

### Adopt first

Use CTest as the common test registry and runner. CMake's `include(CTest)`
provides the `BUILD_TESTING` option, and test targets should only add their
dependencies when that option is enabled.

Use Qt Test for the initial C++ tests. It is already aligned with the Qt event
model and provides data-driven tests, signal observation, GUI event simulation,
and benchmarking without another test framework. `QSignalSpy` can wait for
observable Qt completion signals where a direct synchronous assertion is not
appropriate.

Sources:

* [Qt Test overview and CTest integration](https://doc.qt.io/qt-6/qtest-overview.html)
* [QSignalSpy](https://doc.qt.io/qt-6/qsignalspy.html)
* [CMake CTest module](https://cmake.org/cmake/help/latest/module/CTest.html)

### Add when a concrete need appears

Qt Quick Test is suitable for isolated QML models and components. It does not
replace tests of Sunroom's redirected Quick rendering, custom input forwarding,
or actual presentation path.

A future private application-control channel can use
`QLocalServer`/`QLocalSocket`, which map to named pipes on Windows and local
domain sockets on Unix. That is a plausible implementation, not yet an
interface decision. Build it when several whole-application scenarios need the
same orchestration rather than designing a protocol ahead of the player.

OpenEXR is a strong candidate for floating-point reference images because it
stores half- or full-float HDR channels and colorimetric metadata.
OpenImageIO's comparison tools are a candidate for standard error statistics,
thresholds, and difference images. Adopt them only when a real renderer corpus
justifies the dependency; an initial analytic QRhi smoke test may compare raw
readback values directly.

Sources:

* [Qt Quick Test](https://doc.qt.io/qt-6/qtquicktest-index.html)
* [QLocalServer](https://doc.qt.io/qt-6/qlocalserver.html) and
  [QLocalSocket](https://doc.qt.io/qt-6/qlocalsocket.html)
* [OpenEXR technical introduction](https://openexr.com/en/latest/TechnicalIntroduction.html)
* [OpenImageIO image comparison](https://openimageio.readthedocs.io/en/stable/imagebufalgo.html#image-comparison-and-statistics)

Do not select a scenario syntax, YAML library, property-testing framework,
native UI automation tool, or image-corpus dependency until a concrete scenario
shows that it improves clarity or coverage.

## Design for controllability and observation

Testability should be designed into subsystem boundaries without turning the
application into a test harness.

### Completion and state events

Long-running operations should have observable state transitions or completion
events. Likely examples include:

```text
source-opened
tracks-discovered
decoder-selected
frame-decoded
frame-rendered
frame-presented
seek-completed
buffering-started
buffering-ended
display-state-changed
device-recovered
error
```

Names and payloads should be defined with the owning subsystem. The important
contract is that tests can wait for the real operation rather than infer
completion from elapsed wall time.

Events that can be invalidated by seek, reopen, or device recreation should
include the relevant session, seek, device, frame, or audio-output epoch.
Display capability events normally converge by comparing the latest semantic
target; protocol-supplied asynchronous identities remain local to that
protocol.

### Structured diagnostics

User-facing diagnostics and test observation should come from the same
structured source where practical. As subsystems arrive, a snapshot should be
able to describe:

* Source, container, selected streams, and normalized timestamps.
* Effective color metadata and which source supplied each override.
* Decoder, hardware device, and decoded-frame storage.
* Graphics adapter, QRhi backend, libplacebo backend, and render-target
  description.
* Semantic display target supplied to libplacebo and its update reason.
* Known CPU and GPU copies and explicit fallback reasons.
* Queue depths, buffering state, clock position, and active generations.
* Presented, repeated, stale, and dropped frames.
* Audio underruns, device changes, and recovery state.

Fields should be added with the implementation that can report them accurately.
Unknown must be distinct from zero or none.

### Controlled edges

Narrow replaceable interfaces are appropriate for:

* Monotonic time and timer advancement.
* The audio device callback and presentation estimate.
* Platform display-state observation.
* Source reads, stalls, disconnections, and cancellation.
* Fault injection for allocation, decoder initialization, and device recovery.

These seams control nondeterminism. They should not replace FFmpeg,
libplacebo, libass, the real queue implementation, or the real compositor in a
test whose purpose is to validate those components.

### Future application-control channel

Once actual-application scenarios need interactive coordination, the test
runner should be able to launch the real binary with an isolated profile and
opt-in local control endpoint, conceptually:

```text
sunroom --isolated-profile=<temporary-directory>
        --test-control=<unique-local-name>
```

The channel should:

* Exist only when explicitly enabled.
* Be local to the machine and restricted to the current user where supported.
* Use framed, versioned, machine-readable messages.
* Drive user-level commands rather than private implementation calls.
* Stream structured events and diagnostics.
* Support clean termination and bounded timeouts.
* Avoid becoming a supported public remote-control API accidentally.

Early tests can call public subsystem APIs in-process. The out-of-process
channel is justified when it removes duplicated orchestration and proves
startup, shutdown, and application wiring that in-process tests cannot.

The current `--verify-initial-background`, `--playback-smoke`, and
`--fullscreen-smoke` modes are deliberately narrower than that future channel.
The Windows-only background probe sends native erase to the real unshown
presentation window with a contrasting memory DC and requires black output.
The other modes each open one positional file and observe production
application state directly. Playback smoke waits for distinct video
revisions plus live Cubeb-derived audio-master clock progress; fullscreen smoke
drives native F11, Escape, Space, and redirected background double-click input
while checking normal/maximized restoration, native cursor hiding, and continued
video and audio-clock presentation through both fullscreen transitions. Both
report a process result and exit. Keep these modes small until scenarios need
shared interactive command/event orchestration.

## Focused deterministic tests

Use focused tests where the oracle is stronger than an end-to-end result:

* Timestamp and time-base arithmetic.
* Clock anchoring and drift policy.
* Seek-generation invalidation.
* Bounded-queue cancellation and backpressure.
* Color-metadata precedence.
* Display-target and SDR-white calculations.
* Render-surface device/display-generation validity.
* Subtitle coordinate and avoidance geometry.
* Track-selection policy.
* Error and recovery state transitions.
* Cache interval merging and invalidation.

Avoid tests that merely assert one implementation object called another
implementation object's method.

## Deterministic pipeline scenarios

As features become available, these should exercise the real implementation
through a controlled outer boundary:

```text
pinned media fixture
→ actual source and FFmpeg demux/decode
→ actual queues and scheduler
→ actual libplacebo and libass
→ actual QRhi compositor
→ offscreen or controlled presentation target
```

Use a controlled clock or audio sink only when the scenario needs deterministic
advancement. A scenario should describe behavior such as opening, selecting,
playing, seeking, changing display state, capturing output, and checking
diagnostics rather than internal method sequences.

The first pipeline scenarios should grow with implementation milestones:

1. Procedural producer to explicit video surface to final QRhi compositor.
2. libplacebo-rendered known software frame to final composition.
3. First FFmpeg-decoded frame from a pinned lossless local image.
4. Complete three-frame compressed BT.709 YUV software/hardware drain.
5. Twelve-frame bounded queue, play/pause/replay, timestamp selection, due
   dropping, and end of stream through the real session.
6. Sparse-GOP/B-frame seek with decoded preroll and generation invalidation.
7. Display-target change with rerender.
8. Audio-backed playback and A/V synchronization, including seek, unequal
   stream duration, drain, and terminal output failure.
9. Audio-first actual-application playback through production FFmpeg, Cubeb,
   QML, QRhi, libplacebo, compositor, and swapchain boundaries.

## Media fixture corpus

The corpus should be small, purpose-built, pinned, and expanded alongside
features rather than assembled speculatively.

Each fixture should include:

* A stable filename and cryptographic hash.
* License and provenance.
* The generating script and pinned tool versions when generated.
* Expected stream, timing, color, audio, and subtitle properties.
* The behaviors and coverage tags it exists to exercise.

Tests should consume pinned fixtures. Binary video/audio inputs should not be
regenerated implicitly during tests. Regeneration should be an explicit
maintenance operation so upgrading the local FFmpeg executable cannot silently
change test inputs.

Initial additions should be narrowly tied to milestones:

* An analytically known linear color pattern for the video-surface boundary.
* A tiny lossless RGB image for the first real demux/decode/import boundary.
* One small SDR container for continuous integration. The current
  Matroska/FFV1 fixture covers deterministic compressed software YUV, limited
  range, BT.709 metadata, timestamps, and non-square pixels. Its twelve-frame
  playback variant crosses the three-frame mailbox capacity. A pinned
  Matroska/H.264 fixture covers three-frame D3D11VA decode and retention,
  retained NV12 import, complete
  BT.709 signal metadata plus PTS, duration, time-base, derived frame-rate, and
  SAR assertions, hardware/software output comparison, and zero-copy
  input/output diagnostics. A second pinned Matroska/H.264 fixture contains
  closed sparse GOPs and B-frames for dependency-safe keyframe seek,
  presentation-order preroll, and exact requested-frame publication.
  A two-frame Matroska/FFV1 fixture places its second intra frame at 3000
  seconds and catches narrowing of long absolute seek positions through the
  real demux/decode boundary without requiring a large media file.
* Six lossless Matroska/FFV1+FLAC fixtures cover the shared A/V operation,
  resampling, shared nonzero timing, flash/impulse markers, and production
  audio-master scheduling. They include equal three-second streams; short and
  high-frame-rate video tails after one second of audio; opposite audio/video
  start offsets including a packet-budget-exceeding late-audio case; and a
  midstream audio gap. Together they exercise terminal audio position, clean
  zero-output audio after a seek, clock handoff after drain, complete leading
  source silence, due-first-video scheduling, startup liveness, and midstream
  discontinuity handling.
* A deterministic Matroska/FFV1+DTS fixture reproduces millisecond container
  timestamp quantization around continuous 512-sample, 48 kHz audio frames.
  Full production decode verifies that cumulative source samples remain the
  canonical position inside a continuous region while genuine timestamp gaps
  and overlaps retain their existing fail-fast behavior.
* Four deterministic, four-frame Main10 HEVC elementary streams cover
  BT.2020 limited-range static HDR10/PQ with explicit mastering and content-
  light metadata, HLG, a two-scene HDR10+ sequence, and Dolby Vision Profile
  8.1 over an HDR10 base layer. Their manifests record exact hashes, pinned
  generator versions, source JSON, and analytical patch values. Raw HEVC is
  intentional: repeated generation is byte-stable, while existing Matroska
  fixtures already cover container routing and seek. Each HDR acceptance row
  performs one production FFmpeg demux/decode operation and renders the
  retained frames rather than probing or parsing the file a second time.
* Three subtitle Matroska fixtures cover native ASS and FFmpeg-converted
  SubRip, an embedded purpose-built test font, and byte-generated PGS
  compositions with multiple regions, authored colors and positions,
  replacement, open-ended display, and explicit clear. The same PGS stream is
  also muxed with Matroska zlib `ContentCompression`; decoding that checked-in
  container through the production operation guards the required
  `ffmpeg[zlib]` feature. The fixtures cross production demux/decode rather than
  a private subtitle parser.
* Subtitle failure coverage rejects a decoded PGS event at the production sink
  and proves that the subtitle worker drains while real audio and video still
  reach EOS. Renderer coverage separately proves generation-latched failure,
  next-generation recovery, and bitmap placement into a scaled, offset video
  viewport.
* Further external-subtitle, audio, corruption, range/bit-depth, dynamic-HDR
  profile, and unusual-format fixtures only as those features arrive.

FFmpeg FATE is valuable validation for the exact FFmpeg dependency build, but
it tests FFmpeg rather than Sunroom's integration. The same distinction applies
to libplacebo's upstream suite.

Sources:

* [FFmpeg Automated Testing Environment](https://ffmpeg.org/fate.html)
* [libplacebo upstream testing](https://github.com/haasn/libplacebo#testing)

## Render-output tests

The planned renderer provides two useful capture boundaries:

```text
FFmpeg → libplacebo → [display-targeted video capture]
                              ↓
                    QRhi final compositor
                              ↓
                       [composition capture]
                              ↓
                          swapchain
```

Video capture tests source metadata mapping, target description, tone mapping,
gamut mapping, scaling, native-frame import, and lifetime. Composition capture
adds geometry, letterboxing, subtitle/UI placement and luminance, alpha
composition, and application-owned output conversion.

QRhi supports asynchronous raw texture readback through
`QRhiResourceUpdateBatch::readBackTexture()`. Capture textures must be created
with `UsedAsTransferSource`; multisample targets cannot be read back directly.
Floating-point readback support must be capability-checked, especially on
OpenGL-class backends.

Sources:

* [QRhi texture readback](https://doc.qt.io/qt-6/qrhiresourceupdatebatch.html)
* [Qt's redirected QRhi example](https://doc.qt.io/qt-6/qtquick-rendercontrol-rendercontrol-rhi-example.html)

Every captured image needs an explicit contract:

* Pixel/channel format and byte layout.
* Primaries and white point.
* Transfer function.
* Reference-white and luminance scale.
* Scene- versus display-referred meaning.
* Alpha convention.
* Orientation and valid image region.
* Backend and render parameters.
* NaN and infinity policy.

Prefer independent analytic expectations for constructed patterns. For
goldens, compare with declared absolute and relative tolerances, mean and
maximum error, and the number of pixels exceeding meaningful limits. Add
percentile or perceptual metrics only when their color-space assumptions match
the capture contract.

Do not reduce HDR output to an 8-bit screenshot as the correctness oracle.
Exact equality is appropriate only when the producer and format make it a
stable promise.

## Real GPU and operating-system tests

Run representative pipeline scenarios on actual supported graphics
configurations. A redirected offscreen target proves real QRhi, shader, and
resource behavior, but it cannot prove swapchain creation, the desktop
compositor, native display events, or physical output.

Dedicated runs should enable the available graphics diagnostics:

* D3D11 debug layer and information queue.
* Vulkan standard validation.
* Vulkan synchronization validation.
* Vulkan GPU-assisted validation.
* Metal API and shader validation.
* AddressSanitizer and UndefinedBehaviorSanitizer where supported.
* ThreadSanitizer where it produces useful results with the dependency stack.

Standard, synchronization, and GPU-assisted Vulkan validation are distinct
capabilities and should be reported separately.

Sources:

* [Qt graphics debug-layer configuration](https://doc.qt.io/qt-6/qquickgraphicsconfiguration.html)
* [Khronos validation tooling](https://docs.vulkan.org/guide/latest/development_tools.html)

Native display tests eventually include moving a real window between unlike
displays, HDR toggles, SDR-white changes, brightness and profile changes,
hotplug, sleep/wake, mode changes, fullscreen transitions, and paused-frame
rerendering.

Simulation proves shared policy. It does not prove that a platform adapter
observes the real operating-system event or that the monitor emits the expected
light.

On the current macOS host, AppKit observation and the EDR-capable swapchain
selection path run against a real `NSScreen`, while pure tests cover relative
headroom mapping and equal-state reconciliation. A user-confirmed
multi-display move and return preserve video, UI color, and EDR mapping after
the Metal surface refresh. This physical event is not synthesized in CTest:
the current public boundary cannot select a second physical `NSScreen` or make
AppKit deliver its backing-property change, and a test-only signal or helper
would not prove that boundary. One clean direct fullscreen smoke passed, and
interactive fullscreen is user-confirmed working. Repeated fullscreen CTest
automation was sensitive to live desktop input and was not registered as a
deterministic test; broader display-transition evidence requires a controlled
or manually recorded run. Cross-platform source/router
coverage verifies that HDR Lab publishes its fixed input geometry so the
shared presentation engine can provision a surface, while component coverage
verifies its active viewport and opaque black diagnostic panels. These tests
do not replace physical-output validation.

The current WSLg lane is intentionally narrower. Its unmanaged assumed-sRGB
surface and software Vulkan device exercise production ownership, rendering,
synchronization, and teardown. A prior video-only fullscreen run returned
status zero, while two attempts timed out waiting for cursor-state convergence.
The current audio-bearing explicit run ended in an unresolved buffer/configure
protocol failure before its final assertion; broader transition acceptance
therefore remains open. These results do not satisfy a
real-GPU, color-management-v1, HDR, VAAPI, compositor-decoration, or physical-
output gate. WSLg's zero-size fullscreen configure warnings, xdg-surface
buffer/configure diagnostics, and broken-pipe messages are recorded as
environment output. Sunroom does not add a compositor-specific state machine to
suppress them; native compositor transition coverage remains open.

## Physical-output verification

Software capture cannot prove the final operating-system compositor, cable,
display, or acoustic output.

Begin with a guided, recorded manual matrix on available SDR and HDR displays.
Record operating system, GPU, driver, monitor, connection, display mode,
application diagnostics, test pattern, and result.

A later physical lab may add:

* At least one SDR and one HDR-capable display.
* Displays with materially different HDR behavior.
* Supported Windows, macOS, and native Wayland Linux hosts. Linux unmanaged
  assumed-sRGB SDR does not require color-management-v1; managed gamma-2.2 SDR
  and HDR measurement require their respective capability sets from ADR 0018.
  X11 and XWayland remain outside the support and test matrix.
* A colorimeter or spectroradiometer.
* Photodiode and audio-loopback equipment for end-to-end A/V sync.
* Repeatable ambient conditions.

Automate instruments only after the software capture contract and platform
pipelines are stable enough to make measurements actionable.

## Reliability and fault scenarios

Fault injection should exercise the real application and real cancellation,
queue, and recovery code. Controlled source edges may delay, short-read, stall,
disconnect, reject seeks, corrupt selected ranges, or resume.

Assertions should cover:

* UI responsiveness.
* Bounded cancellation and shutdown.
* Correct buffering transitions.
* Abandonment of old generations.
* Bounded queues and resource pools.
* No stale presentation after seek or reconnect.
* Explicit fallback and recovery reasons.

HTTP or in-memory byte-source faults do not prove behavior when a mounted SMB
or NFS filesystem blocks in the kernel. Real mounted-storage failure tests
belong on dedicated hosts when that source model is implemented.

Other later scenarios include audio removal, decoder rejection, surface-pool
exhaustion, allocation failure, graphics-device recreation, malformed
metadata, and close during open or seek.

## Performance and power

Record behavior, not only frames per second:

* Decoder and GPU backend.
* Known CPU/GPU copies.
* Decode, render, and presentation time.
* Frame-present jitter and drops.
* Audio underruns.
* CPU time and wakeups.
* Memory and texture-pool size.
* Renders per decoded or presented frame.
* Activity while paused, hidden, or minimized.
* Adapter selection and available platform energy metrics.

Shared CI should enforce only robust invariants such as bounded memory, no
unchanged paused rendering, no unexpected CPU frame transfer, and absence of
catastrophic timing regressions. Fine performance and power comparisons belong
on stable dedicated machines and should use that machine's historical
distribution.

## Default cadence

Cadence is based on cost and signal quality and may change as infrastructure
matures.

| Cadence | Default coverage |
| --- | --- |
| Every change | Supported builds; focused deterministic tests; available deterministic pipeline scenarios; software fallbacks; documentation/status checks |
| Scheduled or dedicated GPU hosts | Real GPU backends; hardware decode/import; graphics validation; larger corpus; seek/playback stress; source failures; packaged smoke tests |
| Physical lab | Actual HDR/EDR presentation; monitor changes; measurement; A/V output sync; power; driver/GPU matrix |
| Before release | Supported-platform matrix; long playback; suspend/resume; device and display changes; clean package install; configuration migration; exploratory real-media viewing |

A hardware test skipped because a runner lacks the capability must publish that
gap. It must not silently appear green.

## Tests to reject or redesign

Be suspicious of tests that:

* Mock FFmpeg to claim FFmpeg integration coverage.
* Mock libplacebo to claim video-rendering coverage.
* Sleep for a fixed duration and hope an operation completed.
* Assert private method-call sequences.
* Prove only that the application did not crash.
* Compare HDR correctness through an 8-bit screenshot.
* Require exact decoded pixels across unrelated hardware paths without a
  contract that promises them.
* Use a simulated display and claim native multi-monitor HDR support.
* Use an in-memory byte source and claim mounted-filesystem hangs are contained.
* Enforce fine performance thresholds on a noisy shared runner.
* Silently skip missing hardware, validation, or fixture coverage.

These are review heuristics, not automatic rejection rules. A narrow fake or
smoke assertion can still be useful when its claim and limitations are stated
accurately.
