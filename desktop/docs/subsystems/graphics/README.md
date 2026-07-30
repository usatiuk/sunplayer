# Graphics and display subsystem

## Status

The repository contains a working Windows presentation prototype. It proves the
application-owned QRhi, redirected Qt Quick, final-compositor, extended-linear
presentation, display-observation, and recovery model.

The production decoded-frame/render boundary is wired into the Player and
capture-tested headlessly. Software `AVFrame` planes upload through
libplacebo; supported H.264 input decodes into a retained D3D11VA texture slice
on the same device and maps directly into libplacebo without an input copy.
HDR Lab still uses the analytic libplacebo producer by default, with the
retained procedural QRhi producer available only for diagnostic A/B
comparison. Continuous decoding, subtitles, and playback scheduling are not
integrated.

The currently accepted implementation is deliberately narrower than the
cross-platform target:

| Area | Current state |
| --- | --- |
| Operating system | Windows |
| Graphics domain | Factory-selected D3D11 implementation owning a video-capable native device, QRhi, same-device libplacebo/FFmpeg contexts, execution synchronization, and device generation |
| Presentation | Extended-linear sRGB/scRGB when supported, otherwise SDR |
| Qt Quick | Redirected into an application-owned full-window RGBA16F texture |
| Video | Shared QRhi diagnostic, analytic libplacebo, or FFmpeg-frame libplacebo producer → direct target → display-targeted RGBA16F surface |
| Display telemetry | Qt screen metrics, QRhi swapchain HDR information, and Windows Advanced Color |
| Rendering cadence | Demand-driven, continuous only while the pattern or UI animates |

The active work required to turn this foundation into a real player boundary is
tracked in [PLAN.md](PLAN.md). Known limitations are also collected in
[../../DEFERRED.md](../../DEFERRED.md).

## Responsibilities

The subsystem currently owns:

* Native presentation-window lifecycle.
* Factory-selected QRhi device-domain and presentation swapchain ownership.
* Backend graphics/decode execution synchronization and native frame import.
* Redirected Qt Quick rendering.
* The rendered-video surface, producer, and target-interop contracts plus the
  temporary diagnostic implementation.
* Final GPU composition and swapchain encoding.
* Display selection and HDR/SDR presentation-state observation.
* Render invalidation, resizing, surface loss, and bounded device recovery.
* Presentation diagnostics exposed to the HDR Lab page.

The future subsystem boundary also includes:

* A display-targeted video surface produced by libplacebo.
* Subtitle and diagnostic layers.
* Non-Windows native texture import hidden behind shared interfaces.
* Platform graphics and display adapters for macOS and Linux.

It does not own demuxing, decoding, media clocks, frame scheduling, audio, or
track selection.

## Current architecture

```text
PresentationWindow
    │ window, surface, input, and UpdateRequest events
    ├── VideoViewportState ← AppShell / active QML page
    ▼
RhiPresentationEngine
    ├── GraphicsDeviceDomain
    │       └── D3D11 backend
    │               ├── video-capable native device + execution guard
    │               ├── application-owned QRhi
    │               ├── same-device libplacebo GPU
    │               ├── same-device FFmpeg D3D11VA context
    │               └── native libplacebo frame importer
    ├── QRhi swapchain
    ├── QuickUiLayer
    │       └── QQuickRenderControl → full-window RGBA16F texture
    ├── PresentationOutputState
    │       ├── QScreen metrics
    │       ├── QRhiSwapChainHdrInfo
    │       └── WindowsDisplayStateProvider
    ├── DiagnosticVideoSource
    │       └── content state + cadence
    │               ├── DiagnosticVideoProducer
    │               │       └── QRhi pattern → QrhiVideoTarget
    │               └── LibplaceboDiagnosticVideoProducer
    │                       └── sRGB or BT.2020/PQ RGBA32F
    │                           → D3D11LibplaceboVideoTarget
    │                           → shared RGBA16F video surface
    └── HdrCompositor
            ├── display-targeted video surface or empty-layer binding
            ├── redirected Qt Quick texture
            └── layer composition + extended-linear or SDR encoding
```

QRhi, libplacebo rendering, and presentation still run on the GUI thread.
First-frame FFmpeg demux/decode runs on `MediaSession`'s worker. The D3D11
backend serializes that decoder's device callbacks with the engine's QRhi and
libplacebo resource/command phases and also enables native D3D11 multithread
protection. Device-independent source selection, geometry, and display policy
remain outside the native execution scope.

### Component map

| Component | Current role |
| --- | --- |
| `PresentationWindow` | Owns presentation-facing state, forwards supported input to the redirected Quick window, and translates native window events into engine invalidation or teardown. |
| `VideoViewportState` | Carries the active QML page's root-logical video rectangle and visibility into presentation code without exposing page types. |
| `GraphicsBackendFactory` | Selects Qt Quick API, window surface type, and graphics-device implementation without exposing native types to application or presentation code. |
| `GraphicsDeviceDomain` | Owns the native device, QRhi, same-device libplacebo and FFmpeg hardware contexts, execution synchronization, device generation, backend/adapter diagnostics, target/importer selection, and native teardown. |
| `RhiPresentationEngine` | Owns the swapchain, render-pass lifecycle, frame scheduling, output verification, and device recovery around a graphics domain. |
| `QuickUiLayer` | Creates a `QQuickRenderControl` scene on the engine's QRhi and renders it into a transparent RGBA16F texture. |
| `RenderedVideoSource` | Device-independent content revision, cadence, invalidation, and producer-factory contract used by the engine. |
| `DiagnosticVideoSource` | Owns procedural-pattern controls, animation phase and cadence, and explicit QRhi or libplacebo diagnostic producer creation. |
| `RenderedVideoProducer` | Source-independent lifecycle, render, completion, and composition-texture contract used by the engine. |
| `VideoTargetInterop` | Owns the renderer-to-compositor texture boundary and reports output path, synchronization, copies, transfers, and fallback reason. |
| `DiagnosticVideoProducer` | Implements the producer contract with the temporary pattern pipeline and direct `QrhiVideoTarget`. |
| `LibplaceboDiagnosticVideoProducer` | Owns a persistent renderer and analytic RGBA32F upload texture; describes sRGB or BT.2020/PQ input and a linear BT.709 target to libplacebo. |
| `D3D11LibplaceboVideoTarget` | Wraps the QRhi-owned RGBA16F D3D11 texture as a `pl_tex` and brackets same-immediate-context work through QRhi external commands. |
| `D3D11LibplaceboFrameImporter` | Validates a retained D3D11VA texture/slice and maps NV12/P010/P012/P016 plane views into libplacebo without copying the frame. |
| `RenderedVideoSurfaceState` | Pure description and device/display/content reuse key for a completed display-targeted surface. It does not own a native texture. |
| `HdrCompositor` | Places an optional already processed video surface, converts and blends the flattened UI layer, applies presentation scaling, and encodes the swapchain. It owns a valid fallback binding for UI-only frames and has no source peak, transfer, metadata, or tone-mapping inputs. |
| `PresentationOutputState` | Combines screen metrics, operating-system display state, and the successfully created swapchain's properties into one QML-facing presentation snapshot. |
| `DisplayStateProvider` | Narrow platform adapter for dynamic display color information. The Windows implementation uses WinRT; other platforms currently return an invalid snapshot. |
| `PresentationSettings` | Holds the current target-peak policy. It is not the eventual persistent player-settings model. |

## Ownership and execution model

`RhiPresentationEngine` owns one factory-selected `GraphicsDeviceDomain` for
one `PresentationWindow`. The domain owns QRhi and its monotonic device
generation. Qt Quick adopts that QRhi with
`QQuickGraphicsDevice::fromRhi()`. The redirected Quick scene, diagnostic
producer, final compositor, and visible swapchain therefore use the same native
D3D11 device.

The domain creates that D3D11 device with video support before media can open,
then imports its device and immediate context into QRhi and gives the same
device to libplacebo and FFmpeg. An immutable hardware-decode capability can be
snapshotted by a worker request without exposing D3D11 types. FFmpeg references
the device through its own `AVBufferRef`, while a published hardware
`DecodedVideoFrame` keeps the decoder pool and texture-array slice alive.

The engine owns the final presentation loop rather than injecting rendering
into a Qt-owned onscreen scene graph. Qt Quick renders through
`QQuickRenderControl` into an application-provided texture, and the engine then
performs the visible swapchain pass.

The current destruction order is an invariant:

1. Destroy the compositor and swapchain resources that depend on the visible
   render-pass descriptor.
2. Destroy the active producer, its libplacebo renderer/input resources, and
   its surface resources.
3. Disconnect and destroy `QuickUiLayer`, allowing it to invalidate its scene
   and release QRhi resources.
4. Before device teardown, capture the media position, supersede the media
   session's active decode generation, and clear its published frame. Software
   frame storage itself remains generation-independent. Destroy the
   graphics-device domain and QRhi while holding the shared execution scope.
5. Once the replacement domain publishes its decode capability, re-decode
   superseded media work. A retained worker reference may keep an old native
   allocation alive temporarily, but stale completion cannot be published or
   imported.

The Quick render target and diagnostic video producer intentionally survive
swapchain-only recreation. Resizing the video target changes its composition
texture revision, which forces the compositor to bind the current native
texture before sampling it. Both layers are destroyed when the QRhi device is
rebuilt.

## Frame lifecycle

For a visible, non-empty window, one engine frame proceeds as follows:

1. Reuse the graphics domain created before media open; lazily create the
   redirected Quick scene.
2. Recreate the swapchain if an output change requested it.
3. Create or resize the swapchain.
4. Ensure the Quick RGBA16F target matches the swapchain pixel size.
5. Render the Quick scene if it is dirty. `QQuickRenderControl` uses its own
   offscreen QRhi frame, which completes before the visible frame begins. Page
   route and viewport bindings are therefore coherent for this engine frame.
6. Refresh the concrete producer when the stable active-source router reports
   a configuration change.
7. Map the active page's logical video viewport to an aligned physical-pixel
   rectangle and aspect-fit a known decoded display ratio inside it.
8. Ask the active source to prepare its device-independent state. If the
   viewport is visible and intersects the output, ensure the video
   producer's RGBA16F target matches it, rebinding the compositor if the
   composition texture revision changed. Otherwise bind the compositor's
   empty video layer.
9. Begin the swapchain frame, retrying an out-of-date swapchain after an
   explicit resize.
10. Rerender the video surface when its device, display, content, or size state
    changed, then prepare the target for composition.
11. Sample the optional video layer and UI texture in the final swapchain pass.
12. End the frame and either commit or discard the pending video result.
13. Request another frame only if the visible active source or Qt Quick wants
    one.

An outstanding `QWindow::requestUpdate()` is represented by `m_framePending`.
Synchronous invalidation during rendering is coalesced into a later request
rather than recursively entering the renderer.

The first exposed frame is rendered directly. This avoids competing Qt timer
and DXGI update paths before the first swapchain presentation establishes
normal update delivery.

## Render invalidation

A frame is requested when:

* The Qt Quick scene requests rendering or changes.
* The window size or device-pixel ratio changes.
* The active video viewport geometry or visibility changes.
* Presentation policy or the active source changes.
* Display state or successfully created backend state changes.
* The window moves and its settled output needs verification.
* A swapchain or device recovery attempt is due.
* The procedural pattern is animated.

Display callbacks never mutate QRhi resources immediately. They mark
presentation state dirty and defer swapchain mutation to the next
engine-controlled render point.

## Surface, swapchain, and device recovery

The native surface and graphics device have different lifetimes:

* `SurfaceAboutToBeDestroyed` releases swapchain-dependent resources.
* A normal resize calls `createOrResize()` only when the surface pixel size
  changed, except when QRhi explicitly reports an out-of-date swapchain.
* A display-mode or output change schedules swapchain recreation.
* The video texture survives swapchain-only teardown because it depends on the
  QRhi device, not the swapchain render-pass descriptor.
* Device loss tears down compositor, swapchain, diagnostic producer, Quick,
  and QRhi resources in ownership order. It captures the current position,
  supersedes every active media pipeline, clears the published frame, then
  re-decodes against the replacement graphics capability. This also prevents a
  software pipeline's later seeks from retaining stale capability state.
* Device recovery is bounded to eight attempts, spaced 250 milliseconds apart.
* An otherwise unexpected QRhi frame error triggers one complete rebuild. A
  second consecutive frame error is fatal.
* Packaged-QML, shader, invariant, and non-device resource failures fail fast;
  they are programming or deployment failures rather than recoverable runtime
  states.

This is currently diagnostic-level recovery. Exhaustion still terminates the
application and is not yet surfaced as a player error state.

## Presentation and color model

### Sources of display state

Presentation state deliberately separates two kinds of truth:

* The active graphics path, reported after successful QRhi swapchain creation.
* Asynchronous operating-system and screen telemetry describing the selected
  display.

The final shader follows the successfully created swapchain format. It does not
switch encoding merely because operating-system HDR telemetry changed before
the swapchain was rebuilt.

`PresentationOutputState` combines:

* `QScreen` name, refresh rate, and window device-pixel ratio.
* `QRhiSwapChainHdrInfo` luminance behavior, SDR white, luminance limits, or
  current and potential component-value headroom.
* Windows `DisplayInformation::GetAdvancedColorInfo()` state: current HDR mode,
  SDR white, minimum luminance, and maximum luminance.

Windows display updates arrive through `AdvancedColorInfoChanged`. Moving the
window re-evaluates the output associated with the window center. Position
changes are debounced for 100 milliseconds before the engine verifies whether
the swapchain belongs to the same screen.

### Swapchain selection

The engine prefers `QRhiSwapChain::HDRExtendedSrgbLinear` whenever the
swapchain reports support. Otherwise it creates an SDR swapchain. Extended
linear sRGB is treated as the desktop HDR/extended-range working convention;
the operating system performs the final mapping to the physical display.

The current backend is fixed to D3D11:

* `GraphicsBackendFactory` selects Qt Quick D3D11 and the Direct3D window
  surface.
* Its D3D11 implementation creates the QRhi inside `GraphicsDeviceDomain`.
* Application, presentation, and video code consume the shared factory/domain
  contracts without D3D11 types.
* Other platform implementations remain unavailable.

The decision and fallback policy are recorded in
[ADR 0004](../../decisions/0004-cross-platform-graphics-domain-and-video-interop.md);
video-rendering responsibilities are described in
[../video-rendering/README.md](../video-rendering/README.md).

### Luminance convention

The diagnostic pattern and UI use SDR-white-relative values:

* A pattern value of `1.0` means SDR/reference white.
* In scene-referred scRGB, numerical `1.0` represents 80 nits.
* When the swapchain reports scene-referred behavior,
  `sdrScale = effective SDR white nits / 80`.
* With an absolute display maximum,
  `currentHeadroom = max(1, maximum nits / 80)`.
* The effective target expressed relative to SDR white is
  `max(1, currentHeadroom / sdrScale)`.
* Display-referred extended-linear output uses an SDR scale of `1.0`.
* SDR output is clamped to `[0, 1]` and encoded with the sRGB transfer
  function.

Values reported directly by Windows are preferred while Windows describes an
active HDR display. QRhi swapchain information provides the fallback and
describes non-Windows-style component headroom when absolute luminance is not
available.

### Rendered-video surface

`DiagnosticVideoSource` owns the pattern state, revision, animation cadence,
and HDR-Lab-only renderer selection. Both producers generate the same
grayscale, color-spectrum, stepped ramps, and separator bands. The QRhi
producer applies its temporary diagnostic tone mapper. The libplacebo producer
keeps a persistent 640×360 software-frame-style RGBA32F input and backing
buffer, uploads it as sRGB-encoded RGB for SDR or a fixed-reference
BT.2020/PQ signal with explicit HDR metadata, and applies libplacebo's real
color pipeline.
The upload is cached by the input-frame values it actually depends on, so a
target-only resize or display change reuses the source texture. The input size
is independent of the viewport; libplacebo performs scaling.
Both producers write one opaque, viewport-sized texture with the contract
recorded in
[ADR 0003](../../decisions/0003-display-targeted-video-surface.md):

* RGBA16F linear sRGB/BT.709 D65 with extended floating-point values.
  Transfer-source capability is enabled only by capture consumers such as the
  GPU integration test.
* Canonical top-left sampling coordinates.
* RGB `1.0` means the recorded SDR/reference-white luminance.
* Color processing and tone mapping for the effective display target are
  complete.

Platform display adapters observe native facts such as HDR enablement,
luminance capabilities, and system SDR white. Shared presentation policy turns
those facts into one display-relative target. The producer expresses available
headroom in libplacebo's fixed 203-nit coordinate system:
`max_luma = 203 * physicalPeak / referenceWhite`. The resulting linear samples
therefore use `1.0 = active reference white` directly; no custom post-map scale
is required. SDR diagnostic input remains relative, while the PQ diagnostic is
one fixed mastered signal independent of target changes. The compositor does
not know about libplacebo's coordinate system.

The surface also preserves minimum target luminance as a value plus a known
state. The backend converts a positive physical minimum into the same virtual
coordinate system. Libplacebo treats numeric zero as unknown and otherwise
infers a linear-target contrast ratio, so the adapter uses
`PL_COLOR_HDR_BLACK` for an unknown or known-zero minimum.

The texture's BT.709 primaries define its extended-linear RGB coordinate basis,
not necessarily the physical target gamut. Sunroom does not yet propagate
actual display primaries into libplacebo's separate target-gamut metadata, so
the current target gamut is inferred as BT.709 and wide-gamut output is not yet
claimed.

The final compositor does not know the source peak, pattern phase, tone-map
setting, source transfer function, or HDR metadata. It places the video layer,
blends the UI in linear SDR-white-relative space, applies `sdrScale` once, and
encodes extended-linear or SDR output.

An invisible or empty active viewport disables video production and
composition for that frame. The compositor retains a valid internal texture
binding but uses zero video geometry, leaving only its background and the
redirected UI.

The FFmpeg importer replaces the analytic upload input, not this consumer
contract or the libplacebo renderer. Effective source metadata can
describe SDR, HDR10/PQ, HLG, dynamic HDR, different primaries, ranges, chroma
locations, and bit depths. Supported forms will be normalized and rendered
upstream into the same surface; unsupported or ambiguous forms must produce
observable fallbacks. No actual media format is decoded by the diagnostic
producer.

### Qt Quick layer

Qt Quick renders a transparent, full-window RGBA16F texture. The current final
shader treats its RGB as premultiplied sRGB-encoded UI:

1. Clamp alpha.
2. Recover straight encoded RGB when alpha is nonzero.
3. Decode sRGB to linear light.
4. Alpha-composite it over video in SDR-white-relative linear light.
5. Scale the complete composition into the active presentation convention.

Because Qt Quick flattens all UI into one texture before this conversion,
overlapping translucent QML content is not perfectly colorimetric. The current
interface is intentionally mostly opaque. This limitation must be measured
before investing in a more complex UI composition path.

## Input routing

`PresentationWindow` forwards mouse press, release, move, wheel, key press, and
key release events to the hidden `QQuickWindow`. The Quick root item is sized to
the logical presentation window and receives active focus.

Touch, tablet, input-method, accessibility, drag-and-drop, and richer pointer
semantics are not yet forwarded. Input routing will need a deliberate player
shell boundary rather than continued one-event-at-a-time growth.

## Diagnostics

The current HDR Lab page displays:

* Selected screen.
* Active QRhi backend and swapchain format.
* Device-pixel ratio and refresh rate.
* Scene-referred versus display-referred behavior.
* SDR white and UI scale.
* Absolute luminance range or current/potential headroom.
* Whether HDR or only extended-linear presentation is active.
* Active graphics adapter.
* Active video-surface producer and color/format summary.
* Video output-target path, synchronization mode, known GPU copies and CPU
  transfers per video render, and fallback reason.

It also exposes a manual reprobe and controls for source peak, target peak,
tone mapping, and pattern animation. These controls validate presentation
behavior; they are not the planned player settings surface.

The HDR pattern peak is defined relative to the fixed 203-nit HDR reference
white. The target peak is defined relative to the active platform reference
white. This lets a display-target-only change preserve one source signal and
exercise real display mapping.

Render timings, device-loss history, and decoded-frame import diagnostics do
not exist yet.

## Accepted invariants

The current implementation establishes these project rules:

* The application owns the final swapchain and composition pass.
* Qt Quick, the final compositor, and the diagnostic video producer share the
  current QRhi device.
* Qt Quick is an offscreen layer, not the owner of final presentation.
* Actual swapchain state controls final output encoding.
* Operating-system display telemetry is advisory input to target selection and
  invalidation.
* Color and luminance meaning must be explicit when a texture crosses a
  subsystem boundary.
* Source SDR/HDR/color-space differences terminate at the video producer; the
  final compositor consumes one display-targeted linear contract.
* Display-dependent video results must be rerenderable after display changes,
  including while playback is paused.
* Surface reuse requires matching device, display-target, content, and
  description state. Swapchain identity is not part of that key.
* Resource mutation occurs at engine-owned render points.
* Page layout crosses into presentation only through root-logical viewport
  geometry and visibility.
* UI-only frames do not require a fabricated rendered-video surface.
* Rendering remains demand-driven when no layer is changing.
* Platform-specific display observation stays behind
  `DisplayStateProvider`.

ADR 0004 adds these now-implemented ownership invariants:

* A factory-selected graphics domain owns the native device, QRhi, generation,
  diagnostics, target selection, and deterministic device teardown.
* The active video producer owns the target instance returned by the domain;
  the target owns its composition-visible resources and submission state.
* Qt Quick, the final compositor, and video rendering share that graphics
  domain when the backend supports direct interop.

## Source layout

The code layout follows current responsibilities without mirroring every class
into its own directory:

```text
src/
    app/            startup, native window, presentation settings, and QML
    graphics/       graphics factory, device-domain contract, and backends
        backends/
    media/          retained decoded frames and FFmpeg integration
        ffmpeg/
    playback/       media-session lifecycle and active request worker
    platform/       operating-system display observation
    presentation/   output policy, QRhi orchestration, layers, and compositor
        shaders/
    video/          rendered surfaces, producers, and target interop
```

Future audio, subtitle, and diagnostics source directories should be
added when those subsystems have concrete code. Cross-directory includes use
the responsibility-qualified path so dependencies remain visible.

## Verification

The configured Debug target builds successfully with Qt 6.11.1 and MSVC after
initializing the Visual Studio developer environment. Pure presentation-target,
viewport, rendered-surface reuse, and target-diagnostic policy have automated
coverage. A headless real D3D11 QRhi test creates the production graphics
domain, drives both diagnostic producers through the shared interface, renders
into RGBA16F, composes with the production final pass, and reads back both
boundaries. The QRhi case checks analytic extended values, orientation,
placement, SDR encoding, non-unity extended-linear scaling, post-submission
surface reuse, accepted submissions with committed and discarded rendered
states, target resize/revision-driven compositor rebinding, hidden-video
fallback, and premultiplied UI blending. The libplacebo case checks sRGB and
BT.2020/PQ input, pattern-layout correspondence, reference-white normalization
for SDR targets at 80, 100, and 203 nits, an exact 203-nit PQ patch at surface
`1.0`, and one fixed 1000-nit PQ signal against a 600-nit display target at
those same reference whites. It verifies unchanged surface-relative values
while the signal fits, highlight compression when available headroom falls
below the source, target minimum luminance, one explicit software input upload,
shared-target synchronization, zero output copies, exactly one final
presentation scale, pixel-validated texture rewrap after resize, and producer
destruction/rebinding. A sustained 60-frame probe
uses a fixed 640×360 input and a 1100×600 target; it reports local throughput
without imposing a machine-independent CI threshold.

A separate headless test opens manifest-hashed PPM and Matroska/FFV1 fixtures
through real FFmpeg demux/decode and retains immutable frames after decoder
teardown. The RGB case captures both video and composition output and verifies
copy diagnostics plus target-only rerender reuse. The compressed YUV case
verifies exact decoded plane samples, BT.709 limited-range conversion, timing,
and non-square-pixel metadata. A third pinned H.264 scenario decodes a real
D3D11VA NV12 frame on the graphics-domain device, imports the retained texture
slice directly, asserts zero input CPU transfers/GPU copies and zero output
copies/transfers, and compares captured output against software decode. CTest
requires D3D11VA for this target rather than treating a missing capability as
green coverage. The FFV1 and H.264 fixtures are also decoded as complete
three-frame streams through the bounded continuous path, including simultaneous
retention of three D3D11VA surfaces. A fixed mastered PQ fixture,
P010/P012/P016 capture,
renderer image corpus, cross-backend capture, and recorded runtime display
matrix do not exist yet.

The built GUI has also completed an automated four-second startup liveness
smoke with the configured Qt runtime. It created the normal application path
and remained alive until the harness terminated it. This is useful crash
coverage, not a visual assertion.

The project-wide testing approach is defined in
[../../TESTING.md](../../TESTING.md), with active graphics-related bootstrap
work in [../testing/PLAN.md](../testing/PLAN.md).

Before treating the presentation prototype as stable, manually verify:

* SDR display and SDR swapchain fallback.
* HDR enabled and disabled on an HDR-capable Windows display.
* Window movement between displays with different HDR state and scale.
* Device-pixel-ratio and resize behavior.
* Occlusion, minimize/restore, and native surface recreation.
* Static demand-driven rendering with animation disabled.
* Device-loss recovery where a reproducible test mechanism is available.
