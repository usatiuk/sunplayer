# Application shell

## Status

The current application shell is a single-window Windows presentation host
with a thin QML `AppShell` and retained HDR Lab page. It establishes startup,
object ownership, native presentation events, redirected Qt Quick input, and
the active video-viewport boundary. It does not yet provide player navigation,
file opening, persistent settings, fullscreen behavior, structured errors, or
general logging.

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

`main.cpp` currently:

1. Creates `QGuiApplication`.
2. Asks `GraphicsBackendFactory` to configure Qt Quick for the selected
   backend; the current factory selects D3D11.
3. Installs one dark application palette used by the diagnostic interface.
4. Constructs and shows one `PresentationWindow`.
5. Enters the Qt event loop.

There is no command-line model, single-instance policy, recent-file state,
settings store, or application service container.

## Window ownership

`PresentationWindow` is the current composition root. It declares ownership in
this order, so destruction occurs in reverse:

* `PresentationOutputState`.
* `PresentationSettings`.
* `DiagnosticVideoSource`.
* `VideoViewportState`.
* `RhiPresentationEngine`.

The graphics engine is created before display observation attaches to the
native window because attachment may create the platform window and deliver
synchronous events.

The window requests the factory-selected surface type, currently Direct3D,
starts at 1100 × 760 logical pixels, has a 760 × 560 minimum, and currently
uses the diagnostic title `Sunroom — RHI / HDR`.

This direct ownership is intentionally simple for one window. Application-wide
services should only be introduced when a concrete subsystem needs shared
lifetime or coordination.

## Event routing

The native window translates:

* Exposure into first-frame handling.
* `UpdateRequest` into engine rendering.
* Resize and device-pixel-ratio changes into UI and viewport invalidation.
* Native surface destruction into swapchain teardown.

Mouse, wheel, and keyboard input are forwarded to the redirected hidden
`QQuickWindow`. The hidden Quick scene has the same logical size as the native
window.

The current forwarding is incomplete for a player shell. Touch, tablet, input
methods, accessibility, drag-and-drop, and file-open events remain deferred.

## State and settings

`PresentationSettings` contains presentation-facing controls:

* Automatic or manual target peak.

`VideoViewportState` separately holds the active page's video rectangle in root
logical coordinates and its visibility. `AppShell` publishes the current
`HdrLabPage` viewport; the presentation engine consumes it without knowing page
types or QML layout.

`DiagnosticVideoSource` separately owns the procedural pattern peak, diagnostic
tone-mapping switch, animation state and cadence, and content revision. It
creates a new producer whenever the graphics device is recreated, keeping
source-specific state out of the presentation engine.

None of these values are persisted. They should not be grown into the final
player settings indiscriminately: player preferences, playback-session state,
presentation policy, and short-lived diagnostic controls have different
lifetimes.

## Errors and recovery

The shell has no structured application error model. Graphics code logs
recoverable failures and performs bounded recovery, while deployment,
invariant, and exhausted recovery failures terminate through `qFatal`.

The player eventually needs:

* Structured subsystem errors.
* User-facing loading, fallback, recovery, and fatal states.
* Logs and diagnostics that remain available after a playback-session failure.
* Cancellation-aware shutdown that does not block on a source or device.

Those should be introduced with the first file-open and playback-session
vertical slices rather than as an empty global framework.

## Verification

Viewport state has a focused Qt Test target. A Qt Quick component target
verifies root initial-property handoff and active-page viewport publication.
The built application has completed an automated four-second hidden startup
liveness smoke without user interaction; this verifies packaged-QML loading,
startup, and graphics initialization, not complete UI behavior or visual
correctness.

When features arrive, use:

* Qt Test or Qt Quick Test for focused command, model, accessibility, and QML
  behavior.
* Actual-application scenarios for startup, open, drag/drop, fullscreen,
  errors, and clean shutdown.
* Native platform tests where injected Qt events cannot prove the behavior.

See [../../TESTING.md](../../TESTING.md).
