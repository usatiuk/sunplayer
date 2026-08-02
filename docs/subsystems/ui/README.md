# User interface subsystem

## Status

The QML scene has a thin `AppShell` with a default `PlayerPage` and retained
`HdrLabPage`, rendered offscreen through `QQuickRenderControl`. The shell and
viewport boundary are production structure; HDR Lab remains developer tooling
and is reached from Player rather than occupying permanent playback chrome.

Player opens a local file and presents the movie across the full page. Windows
uses the production libplacebo video and default cubeb audio paths; Linux uses
the same software-decoded libplacebo path through Vulkan and the same cubeb
sink through the system-selected audio backend. A compact two-row transport
island appears on pointer activity,
keeps the timeline above its controls, and fades during uninterrupted playback.
The transport uses a small vendored Lucide 1.28.0 SVG subset inside fully
custom rounded buttons, avoiding font-dependent glyphs and platform-style
pressed backgrounds. The player and compositor background is pure black.
After the transport finishes its idle fade during uninterrupted playback, the
cursor hides over an available video frame and returns with the controls on
the next pointer movement. It remains visible when no frame is ready or while
the controls, menu, sliders, or statistics panel need interaction.
Its position/duration timeline performs seek through the session's
generation-scoped restart boundary and previews the selected time while the
scrubber is pressed. Session-lifetime volume and mute remain wired to the
audio-output boundary; track and subtitle controls remain absent until those
commands exist.

The native presentation window handles non-repeating `F11`/`Escape` and gated
Space play/pause because the redirected Quick window is not the active native
shortcut target. `F11` toggles Qt-managed fullscreen from either page. A
left-button
double-click on active Player video background does the same; one accepting
area behind each floating island prevents labels and dead panel space from
falling through to the video gesture while its child controls keep normal
input. `Escape` leaves fullscreen, except that QML publishes when an open menu
or file dialog owns the first Escape. QML retains the transport/cursor idle
policy, while the real presentation window applies native cursor visibility
and window-state changes.

Session readiness and video-frame availability are distinct UI facts. When
audio makes the session ready before the first video frame, Player keeps its
controls and viewport contract active while showing a preparing state. The
presentation engine can therefore select the first frame without QML claiming
that video is already drawable or creating a viewport/`hasFrame` dependency
cycle.

On native Wayland, an in-scene `ClientSideWindowChrome` is enabled only when
startup capability inventory finds no xdg-decoration manager. It overlays the
existing scene; it is not another window or presentation layer. Its black
titlebar follows the same pointer-activity fade as playback controls, while an
active video viewport remains full-window and does not jump as chrome fades.
Empty/non-player content requests a stable titlebar inset. Compositor-owned
move/resize begins through public `QWindow` operations. Chrome is absent in
fullscreen and its
resize zones are absent while maximized.

Window buttons ask the system icon theme for symbolic minimize, maximize,
restore, and close shapes, use the vendored Lucide glyphs as deterministic
fallbacks, and always apply Sunroom's light foreground tint. The desktop theme
does not choose color for the intentionally black application titlebar. Exact
desktop decoration styling, button ordering, and external client-side shadows
are not reconstructed. One muted, one-physical-pixel inner outline covers the
complete client area whenever application chrome is available. It remains
visible when the titlebar fades and when no media is active, and disappears in
fullscreen with the rest of the application chrome.

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
    windowchrome/
        optional in-scene native-window controls
```

`VideoPage` defines the viewport rectangle and visibility contract shared by
Player and HDR Lab. `AppShell` owns the two-page route and translates the
active page's viewport into root logical coordinates. Player exposes HDR Lab
through its empty state and overflow menu, while HDR Lab has one return action;
no permanent navigation bar reduces the movie viewport. A stable
`ActiveVideoSource` delegates presentation to Player or HDR Lab and changes the
concrete producer only at a render boundary. No routing framework, page
registry, or service container is needed.

`QuickUiLayer` supplies application-owned objects as initial properties of
`Main.qml`; it does not install global QML context properties. The root is an
`AppShell`, and the shell passes each page its dependencies explicitly. The
C++ types are registered as named but uncreatable QML types, so tooling can
validate these contracts without transferring object construction or lifetime
to QML. The layer also supplies its validated redirected-render DPR as an
explicit root property so physical-pixel chrome geometry does not depend on
the offscreen Quick window's attached `Screen`.

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
behavior. It also exposes session-lifetime volume/mute plus a low-rate typed
audio diagnostic view; mute preserves volume and audio-clock progression.
Expand it with working behavior rather than adding disconnected controls.

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
* Ready with playing, paused, ended, or buffering-audio status; video may still
  be waiting for its first due frame.

The Ready state exposes play/pause or replay, ten-second relative seeks, open
another, close, mute, and volume. The active video viewport fills the page and
the bottom-center transport island is pinned only while paused, ended, seeking,
buffering, actively manipulating a slider, or menu-open. A stationary pointer
over the island does not pin it onscreen. Its non-live timeline sends only
interactive moves back to the session, so backend position updates cannot
create seek loops; while pressed, the current-time label follows the selected
slider position. Seeking has a distinct busy state and keeps the timeline
visible but disabled. Explicit user play intent means Buffering still offers
Pause and a pause made during an interruption cannot be mistaken for automatic
playback.

Queue/frame counters plus audio-clock, PCM-occupancy, and underrun information
live in an optional upper-right playback-statistics panel toggled from the
transport menu. Diagnostics no longer occupy permanent space around the movie.
Add track and subtitle controls only when their underlying commands and
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

Player is the default page and HDR Lab remains reachable through Player's
overflow menu or empty state. Reusable read-only pipeline diagnostics appear in
the optional Player statistics panel, but HDR Lab controls do not become
ordinary player preferences.

## Verification

Focused Qt Test coverage verifies viewport geometry, visibility, notification,
and renderability. A non-presenting Qt Quick component test creates the real
QML shell through the same initial-property contract, resizes it, and verifies
that active-page geometry and visibility reach `VideoViewportState`. It also
verifies Empty/Opening/Ready/Error visibility, cancel/retry/close and
play/pause command wiring, Player/HDR-Lab route and viewport selection, the
optional statistics panel, and the diagnostic renderer switch's default and
source binding. It explicitly covers `Ready` before `hasFrame`: playback chrome
and the viewport remain active while the preparing state is visible, then the
preparing state disappears after frame publication. It also verifies full-page
Player geometry, timeline formatting, scrub-preview time, relative and slider
seek commands, backend position updates, disabled seeking state, fullscreen
gesture dispatch, popup Escape priority state, island hit testing, and
native-cursor intent without launching a native dialog. The shell test also
checks disabled, normal, maximized, and fullscreen application-chrome state;
stable empty-page inset; full-root, media-independent outline visibility and
DPR-derived thickness; and the invariant that title fade never moves an active
video viewport. The real D3D11 capture
verifies that zero video geometry and the compositor's fallback
binding produce the normal background rather than sampling the retained video
surface. It also destroys a bound diagnostic producer, creates the other
implementation, rebinds the compositor, and captures the new result. The
registered audio-first application playback scenario crosses the production
FFmpeg, Cubeb, QML, QRhi, libplacebo, and swapchain paths and exits
noninteractively after observing real presentation plus continued clock
progress.

The Windows-registered fullscreen application scenario additionally crosses
the real window, QRhi swapchain, QML, media, and video-presentation boundaries
while driving native F11, Escape, Space, and redirected background double-click
input and checking normal/maximized restoration, cursor hiding, and one
advancing cubeb audio epoch. Linux runs it explicitly and non-gating while its
current WSLg buffer/configure failure remains unresolved.

Extend Qt Quick component coverage as commands gain behavior. Add
actual-application scenarios for file open, navigation, diagnostics access,
and shutdown when the private scenario-control boundary exists.
