# User interface subsystem

## Status

The current QML scene is an HDR diagnostics playground rendered offscreen
through `QQuickRenderControl`. It is useful project tooling, not the final
player interface.

The first visible player shell should arrive with truthful file-open/session
states rather than a screen of controls that are not connected to behavior.

## Page structure

Keep one native presentation window, one QML engine, one redirected Qt Quick
scene, and one final compositor.

The intended QML organization is:

```text
Main.qml / AppShell
    pages/
        PlayerPage.qml
        HdrLabPage.qml
    components/
        shared controls and read-only diagnostics
```

The shell owns simple top-level navigation and shared chrome. A small
top-level selector is sufficient for the known Player and HDR Lab pages; a
routing framework, page registry, or service container is not needed.

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

Replace the diagnostic `canvasRect` setting with a generic viewport contract
containing at least:

* Rectangle in root logical coordinates.
* Visibility.

The active page publishes its viewport through the shell. The presentation
engine continues to place the video layer below the redirected transparent
Quick texture. Inactive pages cannot compete for the viewport.

## Player page

Introduce `PlayerPage` with the first real file-open/session slice:

* Empty.
* Opening.
* Error.
* Ready with a displayed frame.

Add play, seek, track, subtitle, and volume controls only when their underlying
commands and observable states exist.

## HDR Lab and diagnostics

Move the current diagnostic playground into `HdrLabPage` without discarding
its working controls. It remains a developer-facing way to inspect display
state, tone mapping, surface invalidation, and presentation behavior.

Once Player exists, it becomes the default page and HDR Lab remains reachable
through an explicit diagnostics entry or shortcut. Reusable read-only pipeline
diagnostics may also appear in a Player drawer or page, but HDR Lab controls do
not become ordinary player preferences.

## Verification

Use Qt Quick component tests for page selection, active viewport mapping,
truthful session states, and emitted commands. Keep real QRhi capture tests at
the composition boundary and use actual-application scenarios for file open,
navigation, diagnostics access, and shutdown.
