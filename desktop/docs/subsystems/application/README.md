# Application shell

## Status

The current application shell is a single-window Windows HDR diagnostics
playground. It establishes startup, object ownership, native presentation
events, and redirected Qt Quick input. It does not yet provide player
navigation, file opening, persistent settings, fullscreen behavior, structured
errors, or general logging.

Graphics details belong to
[../graphics/README.md](../graphics/README.md). The current diagnostic QML is
part of validating that subsystem rather than the accepted player interface.

## Startup

`main.cpp` currently:

1. Creates `QGuiApplication`.
2. Selects D3D11 as the Qt Quick graphics API.
3. Installs one dark application palette used by the diagnostic interface.
4. Constructs and shows one `PresentationWindow`.
5. Enters the Qt event loop.

There is no command-line model, single-instance policy, recent-file state,
settings store, or application service container.

## Window ownership

`PresentationWindow` is the current composition root. It owns, in declaration
and destruction order:

* `PresentationOutputState`.
* `PresentationSettings`.
* `RhiPresentationEngine`.

The graphics engine is created before display observation attaches to the
native window because attachment may create the platform window and deliver
synchronous events.

The window requests a Direct3D surface, starts at 1100 × 760 logical pixels,
has a 760 × 560 minimum, and currently uses the diagnostic title
`Sunroom — RHI / HDR`.

This direct ownership is intentionally simple for one window. Application-wide
services should only be introduced when a concrete subsystem needs shared
lifetime or coordination.

## Event routing

The native window translates:

* Exposure into first-frame handling.
* `UpdateRequest` into engine rendering.
* Resize and device-pixel-ratio changes into UI and canvas invalidation.
* Native surface destruction into swapchain teardown.

Mouse, wheel, and keyboard input are forwarded to the redirected hidden
`QQuickWindow`. The hidden Quick scene has the same logical size as the native
window.

The current forwarding is incomplete for a player shell. Touch, tablet, input
methods, accessibility, drag-and-drop, and file-open events remain deferred.

## State and settings

`PresentationSettings` contains transient diagnostic controls:

* Procedural pattern peak.
* Automatic or manual target peak.
* Diagnostic tone mapping.
* Pattern animation.
* QML-computed canvas rectangle.

These values are not persisted and should not be grown into the final player
settings indiscriminately. Player preferences, current playback session state,
and short-lived diagnostic controls have different lifetimes and should remain
separate when they arrive.

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

Current automated coverage is absent. Application-shell verification currently
consists only of a successful configured build.

When features arrive, use:

* Qt Test or Qt Quick Test for focused command, model, accessibility, and QML
  behavior.
* Actual-application scenarios for startup, open, drag/drop, fullscreen,
  errors, and clean shutdown.
* Native platform tests where injected Qt events cannot prove the behavior.

See [../../TESTING.md](../../TESTING.md).
