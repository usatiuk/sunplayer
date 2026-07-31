# Application shell

## Status

The application shell is a single-window Windows presentation host with a thin
QML `AppShell`, default Player page, and retained HDR Lab. It establishes
startup, object ownership, native presentation events, redirected Qt Quick
input, the active video-viewport boundary, and asynchronous continuous local
audio/video playback with a position/duration seek timeline. It does not yet
provide drag-and-drop, persistent settings, fullscreen behavior, general
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

`main.cpp` currently:

1. Creates `QGuiApplication`.
2. Parses command-line options and installs the application logger.
3. Asks `GraphicsBackendFactory` to configure Qt Quick for the selected
   backend; the current factory selects D3D11.
4. Installs one dark application palette used by the diagnostic interface.
5. Constructs and shows one `PresentationWindow`.
6. Enters the Qt event loop.

One optional positional command-line path opens local media after construction.
`--playback-smoke` is a narrow noninteractive verification mode for that path:
it requires two distinct video content revisions to reach the swapchain plus
continued live Cubeb audio-master clock progress, then exits with a process
result. It is not a general remote-control interface. There is no full command-line model,
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

`PresentationWindow` is the current composition root. It declares ownership in
this order, so destruction occurs in reverse:

* `PresentationOutputState`.
* `PresentationSettings`.
* `DiagnosticVideoSource`.
* `MediaSession`.
* `ActiveVideoSource`.
* `VideoViewportState`.
* `RhiPresentationEngine`.

The graphics engine is created before display observation attaches to the
native window because attachment may create the platform window and deliver
synchronous events.

The window requests the factory-selected surface type, currently Direct3D,
starts at 1100 × 760 logical pixels, has a 760 × 560 minimum, and currently
uses the title `Sunroom`.

This direct ownership is intentionally simple for one window. Application-wide
services should only be introduced when a concrete subsystem needs shared
lifetime or coordination.

## Event routing

The native window translates:

* Exposure into first presentation and continuous frame handling.
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
audio-clock progress. It exits without user interaction and disables Windows
error dialogs. The scenario proves startup and initial playback wiring; broader command,
error, shutdown, and packaged-install scenarios remain future work.

When features arrive, use:

* Qt Test or Qt Quick Test for focused command, model, accessibility, and QML
  behavior.
* Actual-application scenarios for startup, open, drag/drop, fullscreen,
  errors, and clean shutdown.
* Native platform tests where injected Qt events cannot prove the behavior.

See [../../TESTING.md](../../TESTING.md).
