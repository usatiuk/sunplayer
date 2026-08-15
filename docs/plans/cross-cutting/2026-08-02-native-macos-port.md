# Native Apple-Silicon macOS port

Status: Active

## Goal

Deliver a native arm64 macOS player using Qt 6.11.1 QRhi Metal for Qt Quick,
final composition, and presentation; FFmpeg and libplacebo for the existing
media and color pipeline; MoltenVK for libplacebo's Vulkan backend; cubeb for
the system-default audio route; and VideoToolbox for hardware decoding.

The first usable result must play local SDR and HDR media with software decode,
subtitles, synchronized default-route audio, seeking, fullscreen, and ordinary
display transitions. VideoToolbox acceleration and a self-contained local
application bundle complete the port without changing that shared behavior.

Progress and evidence are tracked in the
[persistent checklist](2026-08-02-native-macos-port-checklist.md).

## Accepted design

### Platform and dependencies

* Support Apple Silicon only, with a macOS 13 deployment floor. Initial claims
  are limited to the available macOS 26 host until other systems are tested.
* Use the Qt 6.11.1 Online Installer tree. Homebrew owns developer tools; the
  repository's pinned vcpkg manifest owns shipped libraries. Qt is not obtained
  through either package manager.
* Keep the existing shared playback, scheduling, subtitle, UI, diagnostics,
  fallback, and recovery code. Platform code is limited to native graphics,
  display observation, build integration, and the FFmpeg hardware capability.

### Presentation and color

* QRhi Metal owns Qt Quick, final composition, and the swapchain. Libplacebo
  continues to render video through Vulkan over MoltenVK.
* SDR output is compositor-encoded nonlinear sRGB in a surface accurately
  tagged as sRGB. EDR output is RGBA16F extended-linear sRGB through QRhi's
  `HDRExtendedSrgbLinear` swapchain path. ColorSync performs the final display
  profile conversion exactly once.
* Preserve the accepted 203-nit libplacebo coordinate system and existing tone
  mapping policy. On macOS, output value `1.0` is current SDR white and the
  active target peak is `203 * currentEDRHeadroom` in libplacebo's virtual
  units. Absolute SDR-white nits remain unknown because macOS exposes the
  relative EDR headroom, not a measured physical SDR-white value.
* SunPlayer owns HDR tone mapping. Leave `CAMetalLayer.edrMetadata` unset and do
  not add an application display-ICC transform or reopen the output-gamut
  design during this port.

### GPU interop

* MoltenVK/libplacebo and QRhi must use the same `MTLDevice`; enforce this as a
  construction invariant.
* Select one proven output bridge: prefer public shared Metal/Vulkan texture
  interop, otherwise use one explicit same-device GPU copy. Diagnose the path
  and copy count. CPU frame roundtrips, hidden staging, and per-frame queue-idle
  waits are not acceptable production behavior.
* Extend `VideoTargetInterop` only where required to protect the existing
  producer-write/compositor-read lifetime. Metal must not sample before
  libplacebo finishes writing, and libplacebo must not overwrite a texture
  still being sampled.

### Display, media, and audio

* Requery presentation state on normal Qt/macOS display-change, expose, and
  wake notifications. Reconcile the latest state at a render boundary; recreate
  only when the swapchain encoding changes and rerender a retained paused frame
  when mapping inputs change. Do not introduce polling or a display state
  machine for hypothetical event orderings.
* Establish software-decoded playback before adding VideoToolbox. The initial
  native import formats are NV12 and P010. Retain each cloned `AVFrame` and its
  native plane resources through actual GPU consumption.
* Reuse the existing bounded hardware-to-software restart. Do not silently
  download unsupported hardware frames on every frame.
* Let cubeb select its normal macOS backend and open the system-default device.
  Cubeb and macOS own ordinary route movement. SunPlayer replaces the stream only
  after an actual error or demonstrated loss of progress; it does not add a
  CoreAudio route watcher or device picker.

## Implementation slices

1. Port the vcpkg overlays and CMake dependency contract; pass shared tests in
   an arm64 macOS build.
2. Add the QRhi Metal domain and prove SDR patches, redirected Qt Quick,
   resizing, fullscreen, and clean teardown.
3. Add EDR selection, relative headroom mapping, and ordinary display-change
   reconciliation; prove deterministic SDR/EDR patches.
4. Implement and capture-validate the MoltenVK/libplacebo output bridge, then
   play software-decoded SDR, PQ, and HLG fixtures through production code.
5. Validate subtitles, seeking, default-route cubeb audio, and application
   lifecycle behavior.
6. Add VideoToolbox capability and NV12/P010 import with lifetime-safe native
   resources and the existing bounded software fallback.
7. Build a self-contained local `.app`, run the relevant shared/native tests,
   update subsystem truth, and close the checklist with durable evidence.

Slices 1–6 are implemented on the available Apple M2/macOS 26 host. Slice 7
is deliberately split: non-packaging documentation, review, and regression
validation precede a separate packaging phase. That phase also owns rebuilding
the vcpkg dependency archives for the declared macOS 13 deployment floor and
testing their compatibility; the current cached archives were built against
the host SDK and do not establish that release claim.

## Validation and completion

* Build from a clean directory using the documented Qt/vcpkg configuration.
* Prove SDR encoding and transparent UI composition without double transfer or
  display-profile conversion.
* Prove EDR values below, at, and above `1.0`; keep UI/reference white at `1.0`
  while highlights fit the current headroom.
* Exercise representative SDR, PQ, HLG, HDR10+, and Dolby Vision fixtures,
  software decode, VideoToolbox NV12/P010, seeking, pause/rerender, resize,
  fullscreen, display movement, wake, and teardown.
* Change the system-default audio output during playback and verify that cubeb
  follows it or performs one bounded recovery.
* Launch the bundled application outside the build tree and verify its runtime
  dependency closure and diagnostics.

## Non-goals

No Intel build, notarized release, App Store work, custom macOS chrome or menu
redesign, Finder integration, device picker, application-managed display ICC
pipeline, P3 output redesign, AirPlay-specific policy, or compatibility
machinery for untested old macOS versions is part of this port. Missing EDR or
external-display hardware is recorded honestly rather than simulated.

## Grounding

This plan applies the existing graphics, color, display reconciliation, media,
audio, and testing contracts rather than replacing them. Important inputs are
ADRs 0002, 0004, 0008, 0012, 0013, 0014, and 0016; the graphics,
video-rendering, media, audio, build, application, and testing subsystem docs;
and the dated color, adaptive-HDR, display/audio reconciliation, build, and
testing research under `docs/research/`. Research remains evidence, while the
accepted ADRs and subsystem documentation remain project truth.

## Delivery evidence

The current host build creates a QRhi Metal domain and a libplacebo MoltenVK
domain on the same Apple M2 `MTLDevice`. Focused capture tests prove direct
RGBA16F target sharing, GPU-only shared-event synchronization, safe target
replacement before the first handoff is submitted, SDR composition, and
production tone mapping for SDR, PQ, HLG, HDR10+, and Dolby Vision Profile
8.1. VideoToolbox H.264/NV12 and Main10 HEVC/P010 frames import directly from
retained `CVPixelBuffer` plane views and agree with software decode within the
declared tolerances; a three-frame run exercises deferred native-surface
lifetime. Subtitle rendering, representative seek paths, AudioUnit default-
device playback, application playback smoke, QML lint, and the registered
Debug suite have also run on this host.

The built-in display reported current headroom `1.0` and potential headroom
`2.0`. This proves native observation and the capable-display selection path,
but not physical output above SDR white or movement between unlike displays.
Default-audio-route movement was not exercised. A clean direct fullscreen
smoke passed, while repeated live-desktop automation was sensitive to
interactive AppKit input and is not a registered CTest. The rebuilt application
is user-confirmed to open its native file sheet on the visible presentation
window without creating a second blank window. These remaining native
hardware checks and all packaging evidence stay visible in the checklist
instead of being inferred from code.

The final independent correctness review found one pre-submission target-
replacement lifetime gap: the initial Vulkan-to-Metal hold could be queued
before QRhi supplied a Metal command buffer to consume its wait. The target
now defers that hold until first command-buffer access, and a focused regression
creates and resizes the target before its first submission. The follow-up
correctness review accepted the fix. Platform/test and explicit anti-
overengineering reviews found no remaining blocker or high-severity issue; the
one shared exposure reprobe noted by the latter was narrowed to macOS.

The post-review Debug build succeeds and the complete registered suite passes
26/26 in 25.43 seconds. A process check before and after the run found no
pre-existing instance and no leaked SunPlayer process.
