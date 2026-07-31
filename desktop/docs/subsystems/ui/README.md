# User interface subsystem

## Status

The QML scene has a thin `AppShell` with a default `PlayerPage` and retained
`HdrLabPage`, rendered offscreen through `QQuickRenderControl`. The shell,
navigation, and viewport boundary are production structure; HDR Lab remains
developer tooling.

Player opens a local file, starts synchronized playback through the production
libplacebo video and default Windows audio paths, and exposes working play,
pause, and replay commands. A position/duration timeline performs seek through
the session's
generation-scoped restart boundary and reports truthful seeking state. It
reports decode path/fallback plus decoded, queued, selected, and dropped-frame
counts. Track, subtitle, and volume controls remain absent until those commands
exist.

Session readiness and video-frame availability are distinct UI facts. When
audio makes the session ready before the first video frame, Player keeps its
controls and viewport contract active while showing a preparing state. The
presentation engine can therefore select the first frame without QML claiming
that video is already drawable or creating a viewport/`hasFrame` dependency
cycle.

## Page structure

Keep one native presentation window, one QML engine, one redirected Qt Quick
scene, and one final compositor.

The current and intended QML organization is:

```text
Main.qml
    AppShell.qml
    pages/
        VideoPage.qml
        HdrLabPage.qml
        PlayerPage.qml
    components/
        shared controls and read-only diagnostics as they become concrete
```

`VideoPage` defines the viewport rectangle and visibility contract shared by
Player and HDR Lab. `AppShell` owns simple top-level navigation and translates
the active page's viewport into root logical coordinates. A stable
`ActiveVideoSource` delegates presentation to Player or HDR Lab and changes the
concrete producer only at a render boundary. No routing framework, page
registry, or service container is needed.

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

The current seam exposes `Empty`, `Opening`, `Ready`, and `Error`; playing,
paused, ended, seekable, and seeking state; integer-millisecond position and
duration; and open, cancel, retry, close, play/pause, replay, and seek
behavior. Expand it with working behavior rather than
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

`PlayerPage` implements the initial continuous-media session:

* Empty.
* Opening.
* Error.
* Ready with playing, paused, or ended media; video may still be waiting for
  its first due frame.

The Ready state exposes play/pause or replay, open another, and close. Queue
and frame counters provide lightweight pipeline observability. Its non-live
timeline sends only interactive moves back to the session, so backend position
updates cannot create seek loops. Seeking has a distinct busy state and keeps
the timeline visible but disabled. Add track, subtitle, and volume controls
only when their underlying commands and
observable states exist.

## HDR Lab and diagnostics

The former diagnostic playground now lives in `HdrLabPage` without losing its
working controls. It remains a developer-facing way to inspect display state,
tone mapping, surface invalidation, and presentation behavior.

HDR Lab selects libplacebo by default. Its diagnostic renderer switch can
recreate the producer at a frame boundary and show the retained procedural QRhi
implementation for A/B inspection. Both implementations receive the same
pattern layout and nominal samples; renderer and tone-mapping differences may
remain. This control is explicitly not a playback fallback or a player
preference. Player uses libplacebo and exposes an actionable session error if
no supported target path is available.

For HDR diagnostics, pattern peak is a fixed multiple of the 203-nit HDR
reference white and is labeled with its mastered nit value. Target peak remains
a multiple of the active platform reference white. This deliberately lets an
SDR-white/display change rerender one unchanged PQ signal.

Diagnostics label the input path separately from output interop. The current
libplacebo lab source reports one fixed-size software-frame CPU upload; the direct
D3D11 target reports zero output copies or CPU transfers. Future hardware-frame
import must therefore remain distinguishable rather than making every path
look “zero-copy.”

Player is the default page and HDR Lab remains reachable through the top-level
selector. Reusable read-only pipeline
diagnostics may also appear in a Player drawer or page, but HDR Lab controls do
not become ordinary player preferences.

## Verification

Focused Qt Test coverage verifies viewport geometry, visibility, notification,
and renderability. A non-presenting Qt Quick component test creates the real
QML shell through the same initial-property contract, resizes it, and verifies
that active-page geometry and visibility reach `VideoViewportState`. It also
verifies Empty/Opening/Ready/Error visibility, cancel/retry/close and
play/pause command wiring, Player/HDR-Lab route and viewport selection, and the
diagnostic renderer switch's default and source binding. It explicitly covers
`Ready` before `hasFrame`: playback chrome and the viewport remain active while
the preparing state is visible, then the preparing state disappears after
frame publication. It also verifies timeline
formatting, backend position updates, one user seek command, and disabled
seeking state without launching a native dialog. The real
D3D11 capture verifies that zero video geometry and the compositor's fallback
binding produce the normal background rather than sampling the retained video
surface. It also destroys a bound diagnostic producer, creates the other
implementation, rebinds the compositor, and captures the new result. The
registered audio-first application playback scenario crosses the production
FFmpeg, Cubeb, QML, QRhi, libplacebo, and swapchain paths and exits
noninteractively after observing real presentation plus continued clock
progress.

Extend Qt Quick component coverage as commands gain behavior. Add
actual-application scenarios for file open, navigation, diagnostics access,
and shutdown when the private scenario-control boundary exists.
