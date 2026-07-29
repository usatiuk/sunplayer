# User interface subsystem

## Status

The current QML scene has a thin `AppShell` containing the retained
`HdrLabPage`, rendered offscreen through `QQuickRenderControl`. The shell and
viewport boundary are production structure; HDR Lab remains developer tooling,
not the final player interface.

There is no `PlayerPage` or page selector yet. Both arrive with truthful
file-open/session states rather than controls disconnected from behavior.

## Page structure

Keep one native presentation window, one QML engine, one redirected Qt Quick
scene, and one final compositor.

The current and intended QML organization is:

```text
Main.qml
    AppShell.qml
    pages/
        HdrLabPage.qml
        PlayerPage.qml        # with the first real session slice
    components/
        shared controls and read-only diagnostics as they become concrete
```

`AppShell` currently owns the only active page and translates that page's
viewport into root logical coordinates. Once Player exists, the shell will own
simple top-level navigation and shared chrome. A small selector is sufficient;
a routing framework, page registry, or service container is not needed.

`QuickUiLayer` supplies application-owned objects as initial properties of
`Main.qml`; it does not install global QML context properties. The root is an
`AppShell`, and the shell passes each page its dependencies explicitly. The
C++ types are registered as named but uncreatable QML types, so tooling can
validate these contracts without transferring object construction or lifetime
to QML.

Pages publish domain commands and viewport geometry. They do not know QRhi,
libplacebo, native textures, graphics backends, or producer implementations.

## Session state and commands

Canonical playback/session state and commands live outside QML behind a narrow
UI-facing session contract. `PlayerPage` observes that state and issues
commands; it does not recreate canonical open, playback, track, timing, or
error state in page-local properties.

Define this seam with the first `PlayerPage` implementation from the states and
commands that slice actually needs. Expand it with working behavior rather than
adding disconnected controls.

## Video viewport

`VideoViewportState` is the application-owned boundary between QML page layout
and presentation. It contains:

* Rectangle in root logical coordinates.
* Visibility.

The active page publishes its page-local viewport through `AppShell`, which
maps it into root logical coordinates and updates the shared state. The
presentation engine converts that rectangle to physical pixels and places the
video below the redirected transparent Quick texture.

When the viewport is hidden or empty, the engine does not provision, render,
or prepare a video surface. The compositor binds its internal fallback texture
and uses zero video geometry, so UI-only pages do not require a fake surface.
Inactive pages cannot compete for the viewport.

## Player page

Introduce `PlayerPage` with the first real file-open/session slice:

* Empty.
* Opening.
* Error.
* Ready with a displayed frame.

Add play, seek, track, subtitle, and volume controls only when their underlying
commands and observable states exist.

## HDR Lab and diagnostics

The former diagnostic playground now lives in `HdrLabPage` without losing its
working controls. It remains a developer-facing way to inspect display state,
tone mapping, surface invalidation, and presentation behavior.

HDR Lab selects libplacebo by default. Its diagnostic renderer switch can
recreate the producer at a frame boundary and show the retained procedural QRhi
implementation for A/B inspection. Both implementations receive the same
pattern layout and nominal samples; renderer and tone-mapping differences may
remain. This control is explicitly not a playback fallback or a player
preference. The future Player page uses libplacebo and exposes an actionable
error if no supported target path is available.

Diagnostics label the input path separately from output interop. The current
libplacebo lab source reports one fixed-size software-frame CPU upload; the direct
D3D11 target reports zero output copies or CPU transfers. Future hardware-frame
import must therefore remain distinguishable rather than making every path
look “zero-copy.”

Once Player exists, it becomes the default page and HDR Lab remains reachable
through an explicit diagnostics entry or shortcut. Reusable read-only pipeline
diagnostics may also appear in a Player drawer or page, but HDR Lab controls do
not become ordinary player preferences.

## Verification

Focused Qt Test coverage verifies viewport geometry, visibility, notification,
and renderability. A non-presenting Qt Quick component test creates the real
QML shell through the same initial-property contract, resizes it, and verifies
that active-page geometry and visibility reach `VideoViewportState`. It also
verifies the diagnostic renderer switch's default and source binding. The real
D3D11 capture verifies that zero video geometry and the compositor's fallback
binding produce the normal background rather than sampling the retained video
surface. It also destroys a bound diagnostic producer, creates the other
implementation, rebinds the compositor, and captures the new result. The
packaged application also passes the noninteractive hidden startup smoke.

Extend Qt Quick component coverage when page selection or page commands create
new isolated behavior. Use actual-application scenarios for file open,
navigation, diagnostics access, and shutdown once those flows exist.
