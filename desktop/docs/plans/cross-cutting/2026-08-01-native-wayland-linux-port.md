# Native Wayland Linux port

Status: Active

## Goal

Deliver Sunroom on Ubuntu 26.04 as a native Wayland video player using the
distribution's Qt 6.10, FFmpeg 8, libplacebo 7.360, libass,
Vulkan/VAAPI/DRM, and cubeb
packages while preserving the existing shared playback, rendering, audio,
color, recovery, and diagnostic behavior.

There are two explicit completion levels:

1. **Linux Wayland SDR:** the installed application opens local media, renders
   software-decoded video through the production FFmpeg/libplacebo/QRhi path,
   presents a compositor-managed gamma-2.2 SDR surface through Vulkan on native
   Wayland color-management-v1, and plays synchronized audio through system
   cubeb. There is no X11, XWayland, or legacy unmanaged-Wayland path.
2. **Linux Wayland HDR:** the application additionally declares and proves a
   managed extended-linear surface through color-management-v1, reacts to
   semantic preferred-target changes, and passes physical HDR-display
   validation. Hardware decode is a separate acceleration gate and must not
   be confused with HDR correctness.

The first usable milestone is SDR with software decode and real audio. HDR and
VAAPI follow only after the common Vulkan target is correct.

Execution status and evidence are tracked separately in the
[persistent Linux port checklist](2026-08-01-native-wayland-linux-port-checklist.md).
This document remains the source of architecture and acceptance decisions; the
checklist must not duplicate or silently change them.

## Grounded current state

* The shared media operation, playback controller, rendered-frame lifecycle,
  libplacebo renderer, QRhi compositor, redirected Qt Quick layer, cubeb sink,
  recovery policy, and diagnostic seams are implemented and covered on
  Windows.
* `PresentationWindow` is now the sole native toplevel, fullscreen, shortcut,
  and cursor authority. Its production fullscreen scenario covers F11,
  redirected background double-click, popup-owned Escape, Space play/pause,
  normal/maximized restoration, cursor hiding, and continued frame
  presentation, but remains registered only on Windows until Linux has a
  production graphics and audio path.
* `GraphicsBackendFactory` fails on non-Windows, the presentation runtime uses
  a null display provider there, and `MediaSession` creates no physical audio
  sink.
* The current CMake entry point requires vcpkg globally and pins Qt 6.11.1 and
  libplacebo 7.360.1. The libplacebo overlay deliberately rejects non-Windows
  targets and enables a D3D11/Shaderc feature set.
* The accepted graphics domain and video target/importer interfaces already
  model a Vulkan implementation. The port should fill those seams, not create
  a parallel Linux renderer or playback core.
* ADRs 0004, 0013, 0015, and 0016 already establish shared native-device
  ownership, system-managed calibration, native-Wayland-only scope, and
  semantic display-target reconciliation. ADR 0017 narrows Linux V1 further
  by requiring color-management-v1 instead of supporting unmanaged legacy
  Wayland.
* Ubuntu package/source inspection found a viable system stack and a narrow
  Qt Wayland feedback seam. The exact results and remaining hardware evidence
  limits are recorded in [the Ubuntu 26.04 platform baseline](../../research/2026-08-01-ubuntu-26-04-linux-platform-baseline.md).

## Fixed invariants and selected policy

### Platform and dependencies

* Ubuntu 26.04 is the initial reference and packaging target. Other Linux
  distributions are not claimed merely because they may configure.
* Linux V1 requires native Wayland color-management-v1 with the global,
  parametric-description feature, named sRGB primaries, `gamma22` and
  `ext_linear` transfer functions, perceptual rendering intent, and surface
  feedback. Treat this as a capability contract rather than a compositor
  brand or release-number list. Missing required protocol capability is an
  unsupported environment and fails clearly during startup.
* The initial Linux implementation uses only system packages. Windows retains
  its current vcpkg manifest and exact pins; Linux neither requires nor
  populates vcpkg. Do not add a project-specific dependency-provider switch:
  a future explicit vcpkg CMake toolchain may become an opt-in Linux build once
  its manifest and ports are made genuinely portable and validated.
* On Linux, accept Qt `>=6.10,<6.11` and require matching private Base,
  Declarative, and Wayland development packages. This narrow range is a
  deliberate compatibility contract for private QRhi and Wayland APIs.
* Require FFmpeg 8's expected library majors, libplacebo API 360 with Vulkan
  and a supported shader compiler backend, system libass, Vulkan 1.2 or newer,
  Wayland protocol generation, libva, libdrm, and cubeb's exported CMake
  target.
* Keep dependency assertions platform-shaped. Linux must not inherit
  D3D11/Shaderc/no-Vulkan checks, and Windows must not become dependent on
  distro package discovery.
* Treat distro library features as capabilities, not renderer policy. In
  particular, preserve the current source-ICC behavior even though Ubuntu
  libplacebo enables LCMS: retain and diagnose the profile, but do not apply
  it until a separate accepted color-pipeline change says otherwise.

### Wayland and color management

* Select Wayland before constructing `QGuiApplication`, then fail fast unless
  Qt reports the `wayland` QPA. Do not recognize `xcb`, XWayland, or an
  automatic presentation fallback as supported.
* Qt owns the `wl_surface` and Vulkan `VkSurfaceKHR`. Exactly one component
  owns `wp_color_management_surface_v1`; a duplicate is a protocol error. Qt's
  Wayland integration is that owner. Sunroom selects the requested
  `QSurfaceFormat` color space and matching buffer encoding, but does not create
  a second color-management surface or take over `wl_surface.commit`.
* A narrow Linux display adapter may use Qt 6.10's private native
  `QWaylandWindow` interface to obtain the surface and follow its creation and
  destruction signals. The adapter binds capabilities, owns preferred-
  description parsing, and publishes semantic output state. It does not
  duplicate Qt's image-description creation or surface policy.
* Support two explicit, coupled presentation encodings:
  `ManagedGamma22Sdr` declares sRGB primaries plus `gamma22` and uses an SDR
  swapchain whose final compositor applies a power-2.2 OETF;
  `ManagedExtendedLinear` declares sRGB primaries plus `ext_linear` and uses
  the FP16 extended-linear swapchain with the 80-nit reference-white anchor.
  The existing piecewise sRGB OETF must not be emitted under a gamma-2.2
  declaration.
* Keep transfer selection at the existing final-compositor boundary. A small
  shared `PiecewiseSrgb`, `Gamma22`, or `ExtendedLinear` value is derived from
  the successfully created surface contract; Linux V1 uses the latter two and
  Windows retains its existing piecewise-sRGB fallback. This is not a second
  renderer or platform color-policy layer.
* Select the mode from the latest complete preferred target. Set
  `QColorSpace::SRgb` for gamma-2.2 SDR or `QColorSpace::SRgbLinear` for
  extended-linear HDR before creating the corresponding native surface, and
  couple that surface with the matching swapchain and compositor encoding.
  Qt applies its Wayland image description asynchronously. Do not gate the
  first buffer, add a declaration generation, or take surface ownership merely
  to perfect that transient ordering; reconcile the latest semantic mode at
  the next safe presentation boundary. Failure of an optional extended-linear
  attempt rolls back to a newly created managed gamma-2.2 SDR surface.
  Failure to establish the required managed SDR surface is fatal for Linux V1.
  Qt's unexposed internal description-failure path is treated as an upstream
  defect, not as a product state requiring duplicate machinery.
* Convert completed preferred descriptions into the existing semantic
  `DisplayState`. Protocol object identities and callbacks remain local to
  the adapter; equivalent values do not cause shared state churn. A surface
  recreation invalidates the old feedback object before binding the new one.
* Managed HDR remains linear sRGB with an 80-nit reference-white anchor;
  managed SDR is explicitly gamma 2.2. The semantic display target controls
  usable headroom. Do not claim full target-gamut propagation until the existing
  linear-sRGB surface contract and compositor behavior are extended and
  validated explicitly.

### Shared Vulkan domain

* Add one narrow window-scoped graphics context because the native window,
  `QVulkanInstance`, and `VkSurfaceKHR` have a different lifetime from a
  recoverable logical-device generation. The presentation window creates this
  context before its native handle and retains it across domain recovery. The
  existing engine/factory receives the context explicitly instead of reaching
  for hidden global Vulkan state.
* The context creates the Qt Vulkan instance with Qt's required instance
  extensions and API version 1.2 or newer, installs it on the `QWindow`, and
  observes native-surface creation/destruction. Device-domain creation is lazy
  until a valid Qt-created `VkSurfaceKHR` exists.
* Ordinary surface loss or recreation destroys surface feedback and swapchain
  resources, not the logical-device domain or media operation. Check that the
  existing physical device and queue family can present to the replacement
  surface before reuse. Enter bounded domain recovery only when that check
  fails or the device is actually lost.
* Each Linux `GraphicsDeviceDomain` generation owns one present-capable Vulkan
  logical device shared by QRhi and libplacebo, the shared graphics queue lock,
  renderer state, target/importer factories, and diagnostics. The exact
  creator/import direction is a blocking spike, not yet accepted architecture.
  Prefer the standard high-level direction first: let QRhi create and own the
  device/queue, then let libplacebo borrow those native handles. Prove that
  Qt's enabled features and extensions satisfy libplacebo before accepting it.
  A libplacebo-created device imported into QRhi remains the narrower fallback
  experiment only if the standard QRhi-owned path fails a measured requirement.
* Start with one graphics queue family and disable separate asynchronous
  compute and transfer families. The existing execution scope serializes CPU
  access to the shared queue across QRhi, libplacebo, and decoder work. It does
  not substitute for GPU dependency signaling.
* The direct-target goal is a QRhi-owned RGBA16F Vulkan image wrapped once by
  libplacebo with explicit image ownership, layout, and producer-to-fragment-
  sampler visibility. `pl_vulkan_release_ex`/`pl_vulkan_hold_ex` and
  `QRhiTexture::setNativeLayout()` are the relevant state boundaries, but the
  synchronization bridge is a blocking spike. Qt 6.10 assigns externally
  supplied queue waits to `COLOR_ATTACHMENT_OUTPUT`, which is too late for
  Sunroom's fragment-shader sampling and must not be assumed safe. Evaluate a
  same-queue bridge/barrier or another validation-proven mechanism. Normal
  rendering must not use `vkQueueWaitIdle` or a CPU pixel copy.
* Do not generalize `VideoTargetInterop` unless the synchronization experiment
  proves its producer/compositor transition points cannot express a safe
  handoff. Accepted and aborted frames both need a canonical, validated image
  state before the direct path can ship.
* Destruction order is mandatory. Device recovery releases imported frames and
  rendered producers, libplacebo's borrowed image/device wrappers, targets,
  compositor and Qt Quick resources, swapchain, then QRhi and its owned logical
  device while retaining the window context. If the fallback creator direction
  is selected, its final ADR must adjust the device-owner edge explicitly.
  `PresentationWindow` needs an explicit destructor contract:
  release the engine/domain, call a proven Qt native-window destruction
  boundary such as `QWindow::destroy()` while the instance is alive, and only
  then destroy the window context/instance. The default derived/base member
  order is insufficient. Stale frame generations remain rejected by existing
  contracts.

### Media and audio

* Prove software-decoded upload/render first. Reuse the current FFmpeg frame
  importer and libplacebo renderer; do not create a Linux-specific renderer.
* Ubuntu's LCMS-enabled libplacebo must not consume source ICC implicitly.
  Clear both ICC/profile handles from the render-copy passed to libplacebo
  while retaining the decoded frame metadata and diagnostics, and protect the
  shared current policy with an ICC-tagged regression.
* The first hardware route is FFmpeg VAAPI mapped/exported as DRM PRIME and
  imported by libplacebo on the same physical adapter. Prefer libplacebo's
  FFmpeg mapping helper over custom dma-buf machinery.
* Match the VAAPI DRM render node to the selected Vulkan physical device. An
  unprovable match, unsupported modifier/plane layout, or import failure is an
  unavailable hardware capability and triggers the existing one-shot software
  restart with diagnostics.
* A retained `AVFrame` owns exported DRM objects until GPU use completes.
  Validate NV12 and P010 on real Intel and AMD Mesa stacks and report any
  synchronization wait accurately. The direct path's acceptance condition is
  zero CPU pixel transfer and zero extra full-frame input copy; GPU conversion,
  tone mapping, scaling, and composition render passes are not copies.
* Reuse `CubebAudioSink`. Make COM setup and forced WASAPI initialization
  Windows-only; on Linux initialize system cubeb without naming a backend and
  retain the existing callback, buffer, clock, epoch, drain, volume, and
  device-recovery semantics.
* Validate cubeb against both a PulseAudio server and PipeWire-Pulse. Do not
  introduce a native PipeWire sink unless system cubeb fails a measured clock
  or recovery requirement.

### Native window and display transitions

* `PresentationWindow` remains the sole fullscreen authority. Continue using
  Qt's asynchronous `showFullScreen()`/`showNormal()`/`showMaximized()` path;
  do not create a replacement Wayland toplevel or issue direct
  `xdg_toplevel.set_fullscreen` requests.
* Fullscreen, resize, scale/DPR change, minimize/restore, and ordinary native
  surface recreation preserve the media operation and logical-device
  generation when the replacement surface is present-compatible. Rebuild only
  surface-bound presentation and color-feedback resources.
* Windowed and fullscreen moves between outputs, compositor-selected
  fullscreen outputs, preferred-description changes, HDR enable/disable,
  hotplug, and reconnect reconcile from the latest complete semantic target.
  If target values change, rerender the retained frame—including a paused
  frame—at a safe boundary. Protocol identities never become shared topology
  revisions.
* A Wayland compositor connection loss is process-fatal with a clear diagnostic
  unless a future Qt-supported full reconnection path is separately proven. It
  is not ordinary graphics-device recovery.

## Non-goals

* X11, XCB, XWayland, GLX, or an OpenGL presentation fallback.
* Legacy or unmanaged Wayland compositors without the required
  color-management-v1 capabilities.
* A second Wayland toplevel or custom fullscreen protocol path.
* A second playback core, renderer, compositor, media probe, metadata policy
  engine, or Linux audio abstraction.
* Bundled Linux copies or source builds of Qt, FFmpeg, libplacebo, libass, or
  cubeb.
* FFmpeg Vulkan Video decode, NVIDIA-specific hardware decode, or custom
  dma-buf import in the first port.
* Applying source ICC profiles, full output-gamut propagation, absolute HLG
  monitoring, or broader HDR claims not already accepted by the shared color
  pipeline.
* Treating WSLg, lavapipe, a protocol advertisement, or successful FP16
  swapchain creation alone as proof of HDR support.

## Implementation slices

### 1. System build foundation

1. Restructure CMake so platform selection precedes dependency policy.
   Preserve Windows vcpkg auto-discovery; use system
   package/config/pkg-config targets on Linux. Do not add a provider enum or
   adapt the Windows-only overlay ports in this iteration.
2. Discover the accepted Vulkan, Wayland, VAAPI, and DRM build boundary up
   front. Link Qt private APIs and generated Wayland code only to the narrow
   Linux implementation or dependency-contract test that consumes them.
3. Replace global exact-version/feature tests with explicit Windows and Linux
   contracts. Test the actual linked runtime versions as well as configure-time
   discovery so mixed Qt or FFmpeg installations fail clearly.
4. Keep all platform-neutral sources and tests in the Linux build. Gate only
   native backends and tests that require unavailable hardware or OS APIs.
5. Add an install-tree dependency report recording resolved library paths,
   versions, libplacebo features, FFmpeg ABI/features, libass version, Qt
   plugin path, and cubeb target/backend diagnostics.
6. Add the explicit ICC-policy boundary and ICC-tagged regression so the
   distro's LCMS build cannot change cross-platform rendering behavior.

Exit: a clean Debug and Release Linux configure/build reaches every shared
test target using only Ubuntu packages. Launch still fails honestly until the
Vulkan backend exists.

### 2. Native Wayland Vulkan SDR vertical slice

1. Bind and inventory the required color-management-v1 capabilities after Qt
   selects the Wayland QPA. Create the window-scoped Vulkan context and request
   `QColorSpace::SRgb` before the SDR window's native handle exists; leave image
   description creation and attachment to Qt.
2. Make initial device-domain creation wait for a valid native surface, then
   preserve a compatible domain across ordinary surface recreation. Allow
   software devices only in explicit test configuration and never silently in
   a supported application run.
3. Spike both shared-device creation direction and the direct Vulkan
   libplacebo target behind the existing target contract. Prove extension and
   feature enablement, producer-to-fragment synchronization, layouts, create,
   render, compose, accepted/aborted submission, resize, surface recreation,
   and teardown under Vulkan standard and synchronization validation before
   recording the resulting ADR.
4. Route software-decoded SDR frames through the production media operation,
   libplacebo renderer, QRhi compositor, redirected Qt Quick layer, and the
   managed gamma-2.2 SDR swapchain. Add analytic near-black, mid-gray, and
   endpoint checks proving the final bytes use the declared power-2.2 transfer,
   not the existing piecewise sRGB OETF. Preserve target-only rerender and
   device-generation behavior.
5. Publish backend, physical-device, queue-family, target format/layout,
   software/hardware path, copy count, fallback reason, and validation state
   through existing diagnostics.
6. Exercise Qt-managed normal/fullscreen and maximized/fullscreen surface
   transitions at the native window/presentation boundary without an audio
   requirement. Wait for asynchronous Wayland convergence and prove continued
   frames plus unchanged graphics generation and media operation.

Exit: representative local SDR media plays video on the required
color-management-v1 gamma-2.2 SDR surface with software decode, zero target
copies, clean Vulkan validation, resize and seek, and no alternate surface
path. A transient frame during asynchronous description installation is
acceptable; stable presentation must match the declaration. This is not yet
the complete SDR milestone because audio is still absent.

### 3. Linux system-cubeb audio

1. Isolate the current Windows COM/WASAPI setup while leaving the shared cubeb
   stream state machine unchanged.
2. Instantiate `CubebAudioSink` on Linux and let cubeb select the default
   service. Record the selected backend ID diagnostically.
3. Validate open/play/pause/seek/drain, underrun recovery, mute/volume,
   video-only media, default-route changes/removal, suspend/resume, and
   bounded failure behavior.
4. Run the same scenarios on PulseAudio and PipeWire-Pulse, including a
   Bluetooth reconnect case on real hardware when available.
5. Register the existing audio-bearing production `application-fullscreen`
   scenario on Linux. Require F11, background double-click, blocked/unblocked
   Escape, Space, normal and maximized restoration, cursor hiding, a newly
   presented frame after every transition, and no graphics-generation or
   media-operation replacement.

Exit: installed native-Wayland SDR playback has synchronized real audio and
meets the first completion level.

### 4. Preferred-target observation and HDR acceptance

1. Extend the capability/lifetime adapter established by the SDR slice with
   preferred surface feedback and immutable description information.
2. Keep Qt as the only color-management surface owner. Request its standard
   gamma-2.2 SDR or extended-linear HDR color space before native-surface
   creation; do not duplicate its image-description implementation.
3. Implement semantic SDR/HDR transitions by recreating the native surface and
   swapchain as one coupled presentation mode while preserving the logical
   device and media operation whenever the replacement surface remains
   present-compatible. Allow asynchronous Qt/compositor convergence and let
   the latest semantic mode win.
4. Feed currently supported preferred luminance and HDR semantics into
   `PresentationOutputState`; reconcile at the existing safe frame boundary
   and rerender the retained frame when effective target values change. Parse,
   validate, and retain preferred primaries in adapter-local diagnostics until
   the shared linear-sRGB render contract is deliberately extended.
5. Validate the declared 80-nit anchor, one-times SDR target, HDR headroom
   response, compositor tone
   mapping, output moves, compositor-selected fullscreen output, HDR toggles,
   hotplug/reconnect, and paused-frame rerender on real SDR and HDR displays.
   Keep observed protocol identities out of shared state.

Exit: HDR is reported as supported only on tested protocol/compositor/display
combinations for which stable Qt declaration, FP16 presentation, semantic
target feedback, and visible/measurement behavior all pass.

### 5. VAAPI/DRM PRIME acceleration

1. Select the FFmpeg VAAPI device from the DRM render node corresponding to
   the active Vulkan physical device. Publish the matching evidence.
2. Map retained VAAPI frames to DRM PRIME and let libplacebo import their
   object/layer/plane/modifier descriptors. Do not add a second mapping model.
3. Exercise NV12 and P010 decode, seek/flush, decoder teardown before and after
   graphics loss, target rerender, adapter mismatch, modifier rejection, and
   the one-shot software restart.
4. Measure copies and waits. The accepted hardware path has no CPU pixel
   transfer; a necessary VA synchronization wait is diagnosed rather than
   described as fully asynchronous.

Exit: at least one Intel and one AMD Ubuntu/Mesa configuration complete
representative accelerated playback with correct software fallback. Other
devices remain software-decoded unless individually proven.

### 6. Reliability, packaging, and support evidence

1. Stress resize/minimize/expose cycles, repeated open/close, long playback,
   seeks, surface recreation, windowed/fullscreen output moves, hotplug,
   suspend/resume, audio migration, and bounded graphics recovery. Verify a
   compositor disconnect exits clearly rather than entering an inert retry.
2. Choose and record the Ubuntu packaging format. If `.deb` is selected, use
   generated shared-library ABI dependencies plus explicit supported Qt 6.10
   family/Wayland-plugin constraints; do not leak private development packages
   into runtime dependencies.
3. Install against Ubuntu shared libraries and the matching Qt Wayland plugin.
   The package/startup check fails clearly when the supported Wayland runtime
   or dependency family is absent; it does not fall back to XCB.
4. Record the actual runtime dependency closure, distro FFmpeg configuration,
   licenses/notices, package metadata, and clean-machine installation steps.
5. Synchronize subsystem documentation, testing evidence, deferred hardware
   coverage, decisions that survive the spikes, and root plan progress.

Exit: a clean Ubuntu 26.04 machine can install and run the claimed SDR/HDR and
hardware subsets, with each claim backed by the corresponding evidence below.

## Validation matrix

### Repeatable development and CI checks

* Clean Debug and Release configure/build with `BUILD_TESTING` both enabled and
  disabled; no vcpkg variables are needed on Linux.
* Dependency-contract tests for Qt version/private-header consistency, FFmpeg
  major ABIs and VAAPI/DRM PRIME compile support, libplacebo API/features and
  ICC policy, Vulkan loader/version, protocol generation, and `cubeb::cubeb`.
* Every platform-neutral CTest plus focused Linux domain, display-adapter,
  target-state, audio-state, media fallback, and diagnostic tests.
* Headless/offscreen QML tests and a lavapipe direct-target capture where the
  software Vulkan implementation supports the required formats.
* Nested native-Wayland SDR smoke testing only with a compositor exposing the
  required color-management-v1 capabilities. Such results prove lifecycle and
  protocol behavior, not HDR or hardware decode.
* The production `application-fullscreen` scenario on native Wayland, including
  continued presentation and unchanged media/device generations through
  asynchronous normal/fullscreen and maximized/fullscreen transitions.
* Vulkan standard and synchronization validation with no relevant errors
  across normal render, aborted render, resize, surface recreation, and
  teardown.
* Install-tree smoke test that resolves only declared Ubuntu dependencies and
  refuses XCB, XWayland, and Wayland compositors missing the required color-
  management-v1 capability set.

Tests that exercise a claimed capability should fail when that capability is
present but broken. Generic runners may skip physical-device scenarios only
when the capability is genuinely absent, and the hardware lane must own the
missing evidence.

### Native hardware gates

* Intel ANV/iHD and AMD RADV/radeonsi lanes for Vulkan, VAAPI, DRM PRIME,
  NV12/P010, modifier handling, zero CPU transfers, and fallback injection.
* PulseAudio and PipeWire-Pulse lanes for clock progression, latency, drain,
  underrun, migration, route loss, reconnect, and suspend.
* A real color-management-v1 compositor and HDR display for gamma-2.2 SDR and
  FP16 extended-linear presentation, 80-nit
  reference white, highlight headroom, output moves, HDR toggles, and managed
  gamma-2.2 SDR rollback.
* Windowed and fullscreen transitions between SDR and HDR outputs, including
  compositor-selected fullscreen placement, semantic feedback convergence,
  paused retained-frame rerender, and preservation of playback/device state.
* At least one additional compositor/GPU combination before making a broad
  Ubuntu HDR statement; until then name the combinations actually tested.

## Failure scenarios and required behavior

| Failure | Required result |
| --- | --- |
| Qt starts under XCB/XWayland | Fail at startup with a direct native-Wayland requirement; do not create graphics state. |
| System dependency ABI or required feature mismatch | Fail configure or startup with the exact mismatched dependency/capability. |
| color-management-v1 or a required gamma-2.2/extended-linear capability is absent | Fail startup with the missing required capability; do not run an unmanaged Wayland surface. |
| Extended-linear surface or FP16 swapchain fails | Tear down the attempted HDR surface and recreate a color-management-v1 gamma-2.2 `SystemManaged` surface plus SDR swapchain. |
| Preferred display description changes | Publish the latest semantic target once complete and rerender the retained frame at a safe boundary. |
| Wayland surface is destroyed/recreated | Destroy feedback and swapchain state tied to the old surface; preserve the domain if it can present to the replacement, otherwise enter bounded domain recovery. |
| Fullscreen or output transition is asynchronous | Wait for Qt/compositor convergence, keep the latest semantic target, preserve playback/device state, and rerender after presentation resources settle. |
| Wayland compositor disconnects | Exit the process with a clear fatal diagnostic; do not loop graphics recovery. |
| Vulkan device is lost | Invalidate media graphics capability, tear down in contract order, and perform the existing bounded domain recovery. |
| Direct-target semaphore/layout transition fails | Abort the frame into its canonical state; fail the spike/support gate rather than hiding it behind a CPU-copy path. |
| VAAPI and Vulkan devices do not match | Disable hardware decode for that generation and perform the one-shot software restart. |
| DRM PRIME modifier/import is unsupported | Diagnose the descriptor/import reason and perform the bounded software restart. |
| cubeb device/route fails externally | Enter the existing canonical unavailable/recovery state and retry only on the bounded cubeb/device trigger. |

## Documentation and decision records

During implementation:

* update build, graphics, video-rendering, display/HDR, audio, playback, and
  testing subsystem documentation when each slice becomes current behavior;
* update `PLAN.md` only when a completion level or roadmap item actually
  changes status;
* record an ADR after the direct Vulkan spike fixes the shared-device creator,
  queue/semaphore handoff, and teardown contract;
* record an ADR after the Wayland spike fixes the division of ownership
  between Qt's surface declaration and Sunroom's feedback adapter;
* record unsupported device/compositor combinations and intentionally deferred
  work in `docs/DEFERRED.md`; and
* keep the Ubuntu platform research note as historical evidence rather than
  turning package inspection into accepted behavior prematurely.

## Useful commit boundaries

Prefer independently reviewable vertical slices:

1. `Use Ubuntu system dependencies on Linux`
2. `Add the native Wayland Vulkan domain`
3. `Enable cubeb audio on Linux`
4. `Integrate Wayland color management`
5. `Import VAAPI frames through DRM PRIME`
6. `Package and validate the Ubuntu port`

Each commit body must explain its ownership/lifetime contracts, rejected
alternatives, validation, known risks, and deferred work. Split a spike from
production integration when its result materially changes the intended
architecture; otherwise keep the proof and implementation together.

## Review disposition

Three independent review lenses examined behavior/correctness,
architecture/lifecycle, and delivery/testing risk, then repeated review after
substantive corrections. The plan incorporates their common findings:
system-dependency checks must be platform-shaped, modern managed Wayland must
be enforced, SDR/software playback precedes HDR and VAAPI, Vulkan device and
synchronization choices need blocking validation spikes, cubeb should retain
the shared sink, fullscreen/display transitions are product gates, system LCMS
must not change source-ICC policy, and hardware claims need native evidence.

The following review suggestions were deliberately not adopted:

* Bundling pinned Qt or libplacebo in the Linux artifact conflicts with the
  selected Ubuntu system-package policy and is unnecessary before the system
  stack fails a measured requirement.
* Creating a Sunroom color-management surface would duplicate Qt's owner
  and violate the protocol. Sunroom observes preferred feedback and otherwise
  uses Qt's standard surface-color path.

The earlier attempt to fix libplacebo-first device creation and direct QRhi
queue-submit semaphores in advance was rejected during review. QRhi ownership
is now the first experiment because it retains the standard Qt lifecycle; the
alternative remains eligible only if that path fails a measured libplacebo
requirement. No synchronization mechanism is accepted until it proves
producer-to-fragment visibility under validation.

## Open spike decisions and remaining evidence

* Shared-device creation direction and the producer-to-fragment synchronization
  mechanism are implementation blockers for the direct target. They must be
  proven against QRhi 6.10 and libplacebo 7.360 under synchronization
  validation before the Vulkan architecture ADR is accepted.
* Qt's surface declaration must be validated on claimed real compositors,
  including coupled surface recreation and rollback after a failed HDR-surface
  or FP16 path.
* Native Ubuntu hardware is required for HDR, VAAPI/DRM PRIME, route migration,
  Bluetooth, and suspend evidence; WSL cannot close those gates.
* The first distributable package needs a recorded license/dependency audit of
  Ubuntu's actual FFmpeg and codec closure.
* Broad target-gamut propagation, NVIDIA hardware decode, FFmpeg Vulkan Video,
  custom dma-buf import, and non-Ubuntu distributions remain deferred until a
  concrete product requirement or observed failure justifies them.
