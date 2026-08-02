# Application shell

## Status

The application shell is a single-window Windows, Apple-Silicon macOS, and
native-Wayland presentation host with a thin QML `AppShell`, default Player
page, and retained HDR Lab. It establishes
startup, object ownership, native presentation events, redirected Qt Quick
input, the active video-viewport boundary, and asynchronous continuous local
audio/video playback with a position/duration seek timeline. It does not yet
provide drag-and-drop, persistent settings, general
structured errors, or a user-facing support-report interface. It now installs
the shared Qt category logger and bounded session-file sink.

Graphics details belong to
[../graphics/README.md](../graphics/README.md). The current diagnostic QML is
part of validating that subsystem rather than the accepted player interface.
The accepted page and viewport direction is documented in
[../ui/README.md](../ui/README.md).

## Accepted shell direction

Keep one native presentation window, one QML engine, and one redirected Qt
Quick scene. The application layer owns their lifetime and the top-level
connection to session services; page selection, viewport policy, and the
retained HDR Lab belong to the UI subsystem. Canonical playback/session state
does not live in the application window or QML page.

## Startup

Startup currently:

1. On Linux, fixes `QT_QPA_PLATFORM=wayland` before creating Qt application
   state. XCB and XWayland are not fallback paths.
2. Creates `QGuiApplication`, parses command-line options, and installs the
   application logger.
3. Asks `GraphicsBackendFactory` to configure Qt Quick for D3D11 on Windows,
   Metal on macOS, or Vulkan on Linux.
4. Installs one dark application palette used by the interface.
5. On Linux, inventories optional Wayland color/decorations capabilities and
   creates the window-scoped `QVulkanInstance`.
6. Constructs and shows one `PresentationWindow`, then enters the event loop.

One optional positional command-line path opens local media after construction.
`--playback-smoke` is a narrow noninteractive verification mode for that path:
it requires two distinct video content revisions to reach the swapchain plus
continued live Cubeb audio-master clock progress, then exits with a process
result. `--fullscreen-smoke` uses the same production boundary to verify
normal/fullscreen and maximized/fullscreen transitions, restoration, continued
video and audio-clock presentation, native keyboard routing, and idle cursor
hiding. These are test scenarios, not a general remote-control interface. The
fullscreen scenario is registered on Windows and run explicitly, non-gating,
on Linux. One clean direct run passes on macOS, but it is not registered while
live-desktop AppKit automation remains sensitive to concurrent input. There is no full
command-line model,
single-instance policy, recent-file state, settings store, or application
service container.

Application logging is installed after `QGuiApplication` construction and
command-line parsing, but before Qt Quick backend selection, QML, graphics,
media, or window construction. Info and higher records are mirrored to a
bounded temporary session file by default. `--debug-log`,
`--log-file <path>`, and `--no-log-file` control the sink; Qt logging rules
remain available. Detailed policy and the observability roadmap live in
[../diagnostics/README.md](../diagnostics/README.md).

## Window ownership

`PresentationWindow` is the current composition root. Its platform-neutral
state owns:

* `PresentationOutputState`.
* `PresentationSettings`.
* `DiagnosticVideoSource`.
* `MediaSession`.
* `ActiveVideoSource`.
* `VideoViewportState`.
* `RhiPresentationEngine`.

On Linux, `LinuxWaylandWindowContext` is an explicit longer-lived dependency.
It owns the `QVulkanInstance`, selects the initial SDR surface contract, and
creates the native Qt window before the graphics engine is built. Qt can
deliver exposure and resize events synchronously during both native creation
and destruction, so `PresentationWindow` has explicit `Initializing`,
`Active`, and `Releasing` phases. Only `Active` forwards those events to the
engine. Teardown first enters `Releasing`, destroys the engine/domain and
display state, explicitly destroys the native `QWindow` surface, then lets the
window context destroy the Vulkan instance.

On Windows and macOS, the graphics engine is created before display observation attaches
to the native window because attachment may create the platform window and
deliver synchronous events.

The window requests the factory-selected Direct3D, Metal, or Vulkan surface type,
starts at 1100 × 760 logical pixels, has a 760 × 560 minimum, and uses the
title `Sunroom`.

Qt remains the sole Wayland `wl_surface`, xdg-toplevel, Vulkan surface, and
managed-color surface owner. When the compositor does not advertise
xdg-decoration, a narrow `WindowChromeController` sets frameless mode before
native creation and exposes only public Qt move, resize, state, and close
operations to the in-scene QML chrome. It contains no Wayland, graphics, color,
or playback policy. Advertisement is treated as the ordinary server-
decoration happy path; the rare final-mode mismatch is documented rather than
mirrored through Qt-private state.

This direct ownership is intentionally simple for one window. Application-wide
services should only be introduced when a concrete subsystem needs shared
lifetime or coordination.

## Event routing

The native window translates:

* Exposure into first presentation and continuous frame handling.
* `UpdateRequest` into engine rendering.
* Resize and device-pixel-ratio changes into UI and viewport invalidation.
* Native surface destruction into swapchain teardown.

Mouse, wheel, and ordinary keyboard input are forwarded to the redirected
hidden `QQuickWindow`, including the complete native double-click event
sequence and its timestamp/device metadata. The visible native window handles
non-repeating F11/Escape plus gated Space play/pause itself; QML publishes only
whether a transient menu or dialog currently owns Escape. The hidden Quick
scene has the same logical size as the native window.

Qt Quick dialogs normally infer their parent from that hidden window. On
macOS, the file dialog explicitly uses the visible `PresentationWindow` as its
native sheet parent so opening media cannot materialize the redirected window
as a second blank toplevel. Windows and Linux leave the property unset, keeping
Qt's normal `QQuickWindow` parent and non-native fallback behavior. The rebuilt
macOS application is user-confirmed to open the native file sheet without the
second window. Qt has fixed the missing redirected-window resolution upstream;
the pinned 6.11.1 build still needs the explicit binding. Recheck both native
and forced non-native behavior before removing it during a Qt upgrade; the
investigation is recorded under `docs/research/`.

`PresentationWindow` is the sole fullscreen and native-cursor authority. Its
QML-facing commands ask Qt to enter or leave compositor-managed fullscreen and
remember only whether the prior non-fullscreen state was maximized. QML still
decides when idle playback should hide the cursor, while the real window
applies and reapplies that state after fullscreen transitions. Leaving
fullscreen restores normal or maximized state without custom geometry, native
window-style edits, display-mode switching, or exclusive fullscreen. Existing
Qt resize, exposure, surface, and QRhi paths handle the asynchronous change.

The current forwarding is incomplete for a player shell. Touch, tablet, input
methods, accessibility, drag-and-drop, and file-open events remain deferred.

## State and settings

`PresentationSettings` contains presentation-facing controls:

* Automatic or manual target peak.

`VideoViewportState` separately holds the active page's video rectangle in root
logical coordinates and its visibility. `AppShell` publishes either the Player
or HDR Lab viewport; the presentation engine consumes it without knowing page
types or QML layout. The engine further aspect-fits a known decoded display
ratio inside that layout rectangle.

`DiagnosticVideoSource` separately owns the pattern peak, diagnostic
tone-mapping switch, animation state and cadence, content revision, and
HDR-Lab-only renderer selection. It creates a new producer when the graphics
device is recreated or that diagnostic selection changes, keeping
source-specific state and producer choice out of the presentation engine.

`MediaSession` owns file-open state and a decoded-frame source.
`ActiveVideoSource` is the stable engine-facing router between that source and
HDR Lab. QML selects only the semantic Player/Diagnostics route; it never sees
native textures, producers, or backends.

None of these values are persisted. They should not be grown into the final
player settings indiscriminately: player preferences, playback-session state,
presentation policy, and short-lived diagnostic controls have different
lifetimes.

## Errors and recovery

The shell has a narrow media-session error model. File-open, decoded-frame
import, and video-render failures become `MediaSession::Error` and hide the
Player viewport. Graphics code logs recoverable failures and performs bounded
recovery, while deployment, invariant, unsupported diagnostic-path, and
exhausted recovery failures may still terminate through `qFatal`.

The player still needs:

* Structured subsystem errors.
* Unified buffering, fallback, recovery, and fatal states beyond the initial
  continuous local-file pipeline.
* Logs and diagnostics that remain available after a playback-session failure.
* Stronger cancellation containment for uninterruptible sources and devices.

Introduce these with the playback and recovery slices that supply their real
state rather than as an empty global framework.

## Verification

Viewport, active-source routing, and media-session lifecycle have focused Qt
Test targets. A Qt Quick component target verifies root initial-property
handoff, all four initial Player states, cancel/retry/close command wiring,
Player/HDR-Lab route selection, and active-page viewport publication.
The current Player executable has a registered no-window mode that loads its
packaged QML module with production type registrations. A second registered,
bounded application scenario opens a real audio-first A/V fixture through the
production FFmpeg and Cubeb paths, shows the native presentation window, and
waits for two distinct video content revisions plus continued live Cubeb
audio-clock progress. A second bounded real-window scenario verifies native
keyboard/gesture routing, fullscreen state/restoration, cursor hiding, video
presentation after each transition, and an unchanged advancing cubeb audio
epoch. It is a registered Windows test; Windows has not been rerun after the
audio assertion was added. On Linux, a prior video-only WSLg run verified
continued presentation and teardown once, while two attempts timed out on
cursor convergence. The current audio-bearing explicit run ended in an
unresolved buffer/configure protocol failure before its final assertion. WSLg
is not treated as native-GPU, managed-color, HDR, VAAPI, native Linux acoustic-
output, or route-change evidence. The scenarios exercise startup, initial
playback wiring, and fullscreen;
broader command,
error, shutdown, and packaged-install scenarios remain future work.

On Apple M2/macOS 26, the registered playback scenario passes through the real
Metal/MoltenVK presentation path and AudioUnit-backed clock, then exits
automatically. Focused subtitle and seek tests cover the shared application
services. A clean direct fullscreen smoke passes and interactive fullscreen is
user-confirmed working. Unlike-display/EDR transitions remain native
validation gaps, while repeatable live-desktop fullscreen automation is a test
gap rather than a product failure; packaging is a later phase.

When features arrive, use:

* Qt Test or Qt Quick Test for focused command, model, accessibility, and QML
  behavior.
* Actual-application scenarios for startup, open, drag/drop, fullscreen,
  errors, and clean shutdown.
* Native platform tests where injected Qt events cannot prove the behavior.

See [../../TESTING.md](../../TESTING.md).
