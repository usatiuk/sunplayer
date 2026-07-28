# Graphics and display subsystem

## Status

The repository contains a working Windows presentation prototype. It proves the
application-owned QRhi, redirected Qt Quick, final-compositor, extended-linear
presentation, display-observation, and recovery model.

It is not yet a media-backed video renderer. A temporary producer renders the
procedural HDR test pattern into the explicit display-targeted video surface
that libplacebo will later produce. FFmpeg, libplacebo, decoded frames,
subtitles, and playback scheduling are not integrated.

The currently accepted implementation is deliberately narrower than the
cross-platform target:

| Area | Current state |
| --- | --- |
| Operating system | Windows |
| QRhi backend | D3D11 |
| Presentation | Extended-linear sRGB/scRGB when supported, otherwise SDR |
| Qt Quick | Redirected into an application-owned full-window RGBA16F texture |
| Video | Procedural producer → canvas-sized display-targeted RGBA16F surface |
| Display telemetry | Qt screen metrics, QRhi swapchain HDR information, and Windows Advanced Color |
| Rendering cadence | Demand-driven, continuous only while the pattern or UI animates |

The active work required to turn this foundation into a real video boundary is
tracked in [PLAN.md](PLAN.md). Known limitations are also collected in
[../../DEFERRED.md](../../DEFERRED.md).

## Responsibilities

The subsystem currently owns:

* Native presentation-window lifecycle.
* QRhi device and swapchain ownership.
* Redirected Qt Quick rendering.
* The rendered-video surface contract and temporary diagnostic producer.
* Final GPU composition and swapchain encoding.
* Display selection and HDR/SDR presentation-state observation.
* Render invalidation, resizing, surface loss, and bounded device recovery.
* Presentation diagnostics exposed to the QML playground.

The future subsystem boundary also includes:

* A display-targeted video surface produced by libplacebo.
* Subtitle and diagnostic layers.
* Backend-specific native texture import hidden behind shared interfaces.
* Platform graphics and display adapters for macOS and Linux.

It does not own demuxing, decoding, media clocks, frame scheduling, audio, or
track selection.

## Current architecture

```text
PresentationWindow
    │ window, surface, input, and UpdateRequest events
    ▼
RhiPresentationEngine
    ├── application-owned D3D11 QRhi
    ├── QRhi swapchain
    ├── QuickUiLayer
    │       └── QQuickRenderControl → full-window RGBA16F texture
    ├── PresentationOutputState
    │       ├── QScreen metrics
    │       ├── QRhiSwapChainHdrInfo
    │       └── WindowsDisplayStateProvider
    ├── DiagnosticVideoProducer
    │       └── pattern + diagnostic tone map → RGBA16F video surface
    └── HdrCompositor
            ├── display-targeted video surface
            ├── redirected Qt Quick texture
            └── layer composition + extended-linear or SDR encoding
```

All current objects and GPU work run on the GUI thread. There is no render
thread or playback thread yet.

### Component map

| Component | Current role |
| --- | --- |
| `PresentationWindow` | Owns presentation-facing state, forwards supported input to the redirected Quick window, and translates native window events into engine invalidation or teardown. |
| `RhiPresentationEngine` | Owns the QRhi device, swapchain, render-pass lifecycle, frame scheduling, output verification, and device recovery. |
| `QuickUiLayer` | Creates a `QQuickRenderControl` scene on the engine's QRhi and renders it into a transparent RGBA16F texture. |
| `DiagnosticVideoProducer` | Owns the temporary pattern pipeline and canvas-sized RGBA16F render target. It represents the future libplacebo producer side of the video-surface boundary. |
| `RenderedVideoSurfaceState` | Pure description and device/display/content reuse key for a completed display-targeted surface. It does not own a native texture. |
| `HdrCompositor` | Places the already processed video surface, converts and blends the flattened UI layer, applies presentation scaling, and encodes the swapchain. It has no source peak, transfer, metadata, or tone-mapping inputs. |
| `PresentationOutputState` | Combines screen metrics, operating-system display state, and the successfully created swapchain's properties into one QML-facing presentation snapshot. |
| `DisplayStateProvider` | Narrow platform adapter for dynamic display color information. The Windows implementation uses WinRT; other platforms currently return an invalid snapshot. |
| `PresentationSettings` | Holds transient controls for the diagnostic HDR pattern. It is not the eventual persistent player-settings model. |

## Ownership and execution model

`RhiPresentationEngine` owns one QRhi for one `PresentationWindow`. Qt Quick
adopts that device with `QQuickGraphicsDevice::fromRhi()`. The redirected Quick
scene, final compositor, and visible swapchain therefore use the same native
D3D11 device selected by QRhi.

The engine owns the final presentation loop rather than injecting rendering
into a Qt-owned onscreen scene graph. Qt Quick renders through
`QQuickRenderControl` into an application-provided texture, and the engine then
performs the visible swapchain pass.

The current destruction order is an invariant:

1. Destroy the compositor and swapchain resources that depend on the visible
   render-pass descriptor.
2. Destroy the diagnostic producer and its surface resources.
3. Disconnect and destroy `QuickUiLayer`, allowing it to invalidate its scene
   and release QRhi resources.
4. Destroy the QRhi device.

The Quick render target and diagnostic video producer intentionally survive
swapchain-only recreation. The canvas texture wrapper is resized in place when
its integer pixel size changes; both layers are destroyed when the QRhi device
is rebuilt.

## Frame lifecycle

For a visible, non-empty window, one engine frame proceeds as follows:

1. Lazily create the QRhi device and redirected Quick scene.
2. Recreate the swapchain if an output change requested it.
3. Create or resize the swapchain.
4. Ensure the Quick RGBA16F target matches the swapchain pixel size.
5. Render the Quick scene if it is dirty. `QQuickRenderControl` uses its own
   offscreen QRhi frame, which completes before the visible frame begins.
6. Align the logical canvas to an integer physical-pixel rectangle and ensure
   the diagnostic RGBA16F video target matches it.
7. Begin the swapchain frame, retrying an out-of-date swapchain after an
   explicit resize.
8. Rerender the diagnostic surface when its device, display, content, or size
   state changed.
9. Sample the video and UI textures in the final swapchain pass.
10. End and present the frame.
11. Request another frame only if the pattern is animated or Qt Quick remains
   dirty.

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
* Diagnostic presentation settings change.
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
  and QRhi resources in ownership order.
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

* `main.cpp` selects Qt Quick's D3D11 graphics API.
* `PresentationWindow` requests a Direct3D surface.
* `RhiPresentationEngine` creates `QRhi::D3D11` on Windows and terminates on
  other platforms.

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

`DiagnosticVideoProducer` currently generates the grayscale, color-spectrum,
and stepped ramps and applies their optional diagnostic tone mapper. It writes
one opaque, canvas-sized texture with the contract recorded in
[ADR 0003](../../decisions/0003-display-targeted-video-surface.md):

* RGBA16F linear sRGB/BT.709 D65 with extended floating-point values.
  Transfer-source capability is enabled only by capture consumers such as the
  GPU integration test.
* Canonical top-left sampling coordinates.
* RGB `1.0` means the recorded SDR/reference-white luminance.
* Color processing and tone mapping for the effective display target are
  complete.

The final compositor does not know the source peak, pattern phase, tone-map
setting, source transfer function, or HDR metadata. It places the video layer,
blends the UI in linear SDR-white-relative space, applies `sdrScale` once, and
encodes extended-linear or SDR output.

The future FFmpeg/libplacebo path will replace the temporary producer, not this
consumer contract. Effective source metadata can describe SDR, HDR10/PQ, HLG,
dynamic HDR, different primaries, ranges, chroma locations, and bit depths.
Supported forms will be normalized and rendered upstream into the same surface;
unsupported or ambiguous forms must produce observable fallbacks. None of
those media formats are implemented by the current diagnostic producer.

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

The current QML playground displays:

* Selected screen.
* Active QRhi backend and swapchain format.
* Device-pixel ratio and refresh rate.
* Scene-referred versus display-referred behavior.
* SDR white and UI scale.
* Absolute luminance range or current/potential headroom.
* Whether HDR or only extended-linear presentation is active.
* Active video-surface producer and color/format summary.

It also exposes a manual reprobe and controls for source peak, target peak,
tone mapping, and pattern animation. These controls validate presentation
behavior; they are not the planned player settings surface.

Backend adapter identity, copy paths, render timings, device-loss history, and
video import diagnostics do not exist yet.

## Accepted invariants

The current implementation establishes these project rules:

* The application owns the final swapchain and composition pass.
* Qt Quick, the final compositor, and future video rendering share one graphics
  device for a presentation domain.
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
* Rendering remains demand-driven when no layer is changing.
* Platform-specific display observation stays behind
  `DisplayStateProvider`.

## Source layout

The code layout follows current responsibilities without mirroring every class
into its own directory:

```text
src/
    app/            startup, native window, diagnostic settings, and QML
    platform/       operating-system display observation
    presentation/   output policy, QRhi orchestration, layers, and compositor
        shaders/
```

Future media, playback, audio, subtitle, and diagnostics directories should be
added when those subsystems have concrete code. Cross-directory includes use
the responsibility-qualified path so dependencies remain visible.

## Verification

The configured Debug target builds successfully with Qt 6.11.1 and MSVC after
initializing the Visual Studio developer environment. Pure presentation-target
policy and rendered-surface validity/reuse rules have automated coverage. A
headless real D3D11 QRhi test renders the production diagnostic producer into
RGBA16F, composes it with the production final pass, reads back both boundaries,
and checks analytic extended values, orientation, placement, SDR encoding,
non-unity extended-linear scaling, post-submission surface reuse, and
premultiplied UI blending. A renderer image corpus, cross-backend capture, and
recorded runtime display matrix do not exist yet.

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
