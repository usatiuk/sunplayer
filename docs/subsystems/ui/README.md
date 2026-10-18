# User interface subsystem

## Status

The QML scene has a thin `AppShell` with a default `PlayerPage` and retained
`HdrLabPage`, rendered offscreen through `QQuickRenderControl`. The shell and
viewport boundary are production structure; HDR Lab remains developer tooling
and is opened deliberately from About rather than occupying playback chrome.

Player opens a local file and presents the movie across the full page. Windows
uses D3D11/D3D11VA, macOS uses Metal/MoltenVK/VideoToolbox, and Linux uses the
software-decoded Vulkan path; all three share the production libplacebo video,
compositor, and default-route cubeb boundaries. A compact two-row transport
island appears on pointer activity,
keeps the timeline above its controls, and fades during uninterrupted playback.
The transport uses a small vendored Lucide 1.28.0 SVG subset inside fully
custom rounded buttons, avoiding font-dependent glyphs and platform-style
pressed backgrounds. The player and compositor background is pure black.
The idle overflow action is a square, icon-only standard `Button`: its
background remains present and its icon follows `palette.buttonText`, keeping
idle, pressed and keyboard-focused states readable in light and dark styles.
Custom transport and titlebar buttons expose `visualFocus` through their
existing hover highlight. HDR Lab's four switches set both `palette.text`
and `palette.windowText` for their intentionally dark surface; Fusion uses
the former, while Basic and native Windows/macOS styles use the latter.
These are shared control contracts, independent of Linux portal selection.
After the transport finishes its idle fade during uninterrupted playback, the
cursor hides over an available video frame and returns with the controls on
the next pointer movement. It remains visible when no frame is ready or while
the controls, menu, sliders, or playback-details panel need interaction.
The Linux CSD hover observer uses the same PlayerPage movement filter as the
video area, so repeated stationary hover updates do not reset the idle timer
in windowed or maximized playback. Fullscreen uses that same activity policy.
Its position/duration timeline performs seek through the session's
generation-scoped restart boundary and previews the selected time while the
scrubber is pressed. Persisted volume and session-lifetime mute remain wired to
the audio-output boundary. The overflow menu exposes checked video, audio, and
subtitle track lists backed by the session's observable selection state;
unsupported embedded streams are disabled. On Windows, the same menu has a
persisted checked action to blank every other display while Player is
fullscreen. It is hidden and its stored value remains unapplied on other
platforms for now.

The active and idle overflow menu also opens a window-modal native Qt Widgets
Settings dialog, matching the existing About/support surface. Its Playback tab
shares canonical volume and display-blanking owners. Its Subtitles tab exposes
authored/high-contrast/large-text presets plus unrestricted RGB colors,
component opacity, background and edge choices, 50–200% scale, bottom-to-top
position, brightness, overall opacity, and reset. The subtitle submenu opens that tab
directly. Every edit writes through immediately and updates the real active cue
without pausing playback; there is no Apply/Cancel draft state.

The native presentation window handles non-repeating `F11`/`Escape`, gated
Space play/pause, and unmodified Left/Right ten-second seeks (including native
key repeat) because the
redirected Quick window is not the active native shortcut target. Relative
seek requests reuse the Player page action used by the transport buttons and
accumulate into one target during a 180 ms trailing input window.
`F11` toggles Qt-managed fullscreen from either page. A
left-button
double-click on active Player video background does the same; one accepting
area behind each floating island prevents labels and dead panel space from
falling through to the video gesture while its child controls keep normal
input. The background area recognizes the double-click on its second press but
requests fullscreen from the matching release, so native fullscreen cannot
start before that release reaches the redirected Quick scene; cancellation
or session deactivation discards the pending request. `Escape` leaves
fullscreen, except that QML publishes when an open menu or file dialog owns the
first Escape. QML retains the transport/cursor idle policy, while the real
presentation window applies native cursor visibility and window-state changes.

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
Empty/non-player content requests a stable titlebar inset. Player navigation
and the playback-details panel reserve that same CSD height above their floating
content, including the panel's maximum height; fullscreen/native decorations
need no inset. The video viewport itself remains stable as controls fade.
Compositor-owned
move/resize begins through public `QWindow` operations. Chrome is absent in
fullscreen and its
resize zones are absent while maximized.

Window buttons ask the system icon theme for symbolic minimize, maximize,
restore, and close shapes, use the vendored Lucide glyphs as deterministic
fallbacks, and always apply SunPlayer's light foreground tint. The desktop theme
does not choose color for the intentionally black application titlebar. Exact
desktop decoration styling and button ordering are not reconstructed. Exterior
shadows belong entirely to the Linux Wayland integration: one passive SHM
subsurface below the opaque CSD body, hidden while fullscreen, maximized or
tiled. The QML scene, video viewport and input coordinates have no shadow
margins. Rounded corners remain deferred. One muted, one-physical-pixel inner
outline covers the complete client area only for floating CSD windows. It
remains visible when the titlebar fades and when no media is active, but is
hidden while maximized, tiled or fullscreen. Maximized and tiled windows retain
their titlebar and controls. Linux forwards the tiled state already sampled by
the shadow helper to the chrome controller; no second native observer is needed.
SSD appearance remains compositor-owned.

Signed AppImage releases expose one optional `applicationUpdate` capability on
WindowCommands. Player's existing empty state alone shows its update action,
Updating status and error. It is absent during opening/playback/error and from
HDR Lab; ordinary builds and other platforms have no updater capability. The
Linux owner handles subprocesses, staging and restart without graphics or media
integration. Media leaving Empty cancels an active update. The notice offers
Update and restart and Dismiss; dismissal lasts this launch and is disabled
while updating.

Only updater-enabled Linux AppImages add an Application settings tab with one
immediate “Check for updates on startup” checkbox. Linux `AppImageUpdateUi`
owns that page, the first-use Yes/No question and the `linux/appimage/checkOnStartup`
key in the existing application settings file. Missing means unanswered; no
check runs before consent. The question waits for Empty, closes if media opens,
and a dismissed question waits until next launch. Yes checks immediately and
once per future launch; No skips automatic checks. Enabling the checkbox checks
immediately. Checks remain asynchronous during playback; installation always
requires the empty-screen action. Other platforms and ordinary Linux builds
have no update preference tab or prompt.

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
active page's viewport into root logical coordinates. The About controller can
request HDR Lab, while HDR Lab has one return action; no permanent navigation
element reduces the movie viewport. A stable
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
behavior. It also exposes persisted volume, session-lifetime mute, and a
low-rate typed audio diagnostic view; mute preserves volume and audio-clock
progression. The runtime session remains the canonical source of truth while
the application boundary restores and records the volume preference.
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
another, close, mute, and volume. Relative taps update a visible pending target
immediately and dispatch one generation replacement after 180 ms of quiet;
pressing the slider cancels any pending relative burst and slider release
remains immediate. A new burst can supersede a slow active seek.
The active video viewport stays present on its black background while the old
decoder-backed frame is released and the replacement opens. Play/pause,
relative buttons, and the slider remain usable. Mute and volume remain visible
for a selected audio track even while the audio output is transiently rebuilt.
The viewport fills the page and the bottom-center transport island is pinned
only while paused, ended, seeking, actively manipulating a slider, or
menu-open. A
stationary pointer over the island does not pin it onscreen. Its non-live
timeline sends only interactive moves back to the session, so backend position
updates cannot create seek loops; while pressed, the current-time label follows
the selected slider position. Preparing and seeking use one subtle dark-backed
activity overlay with a visible spinner above readable status text.
Buffering keeps the frame, shows only a centered spinner, and does not summon
the transport. Explicit user play intent means Buffering still offers Pause
when the user reveals the controls, and a pause made during an interruption
cannot be mistaken for automatic playback.

An optional scrollable upper-right playback-details panel groups media, video,
audio, subtitles, output, and performance. It shows the selected track labels;
source sample rate and subtitle text/bitmap kind where known; decoded
resolution, nominal frame rate, pixel format, bit depth, primaries, transfer,
and range; decode and fallback paths; the active presentation mode; and the
existing queue/frame, audio-clock, PCM-occupancy, and underrun counters.
Source dynamic range and presentation are deliberately separate: a Dolby
Vision, HDR10+, HDR10, HLG, or generic PQ source can truthfully report that it
is mapped to an SDR presentation, while SDR content can be shown through an
HDR or extended-range presentation surface. Incomplete evidence is labeled
`Unknown` or `PQ HDR`, not promoted to a branded format. Diagnostics remain
optional and do not occupy permanent space around the movie. Video/audio track
changes and subtitle changes all use their implemented session commands rather
than QML-owned playback state.

## Help, diagnostics, and error actions

`PlayerPage` keeps **Report a bug…** and **About SunPlayer** as direct items in
the existing overflow menu. A platform-styled `ToolButton` exposes the same
menu while no media is active, so support and packaged notices do not depend
on successful playback.

About is a separate Qt Widgets dialog rather than an in-scene Quick dialog. It
therefore follows the active platform widget style and remains independent of
the redirected QRhi composition path. It shows the application version/build
ID, source action, generated third-party notices, packaged privacy policy, and
Copy diagnostic information. Its **Open HDR Lab** action is the sole entry to
developer presentation controls. It does not introduce a second custom dialog
style beside the player scene.

`SupportController` receives the current application-owned session and output
state but does not own playback or recovery. It builds diagnostics from an
explicit allowlist of stable identifiers, booleans, numbers, and counters.
Reports exclude media names, paths and URLs, user/host identifiers, raw logs,
and arbitrary library errors. Structured backend fields preserve their real
Unicode and punctuation while rejecting accidental path/URL/control-shaped
values. **Report a bug…** copies the detailed summary, then asks the system
browser to open a bounded prefilled GitHub issue; it never uploads or submits
anything.

Media-open/decode exhaustion stays in the QML error page and offers Retry,
Restart, Open another, Report a bug, and Quit. Total presentation failure uses
the platform-styled fallback dialog because the failed QRhi pipeline cannot be
trusted to draw its own error UI.

## HDR Lab and diagnostics

The former diagnostic playground now lives in `HdrLabPage` without losing its
working controls. It remains a developer-facing way to inspect display state,
tone mapping, surface invalidation, and presentation behavior.

The page publishes the diagnostic source's fixed display geometry through the
same active-source contract as Player, allowing the presentation engine to
aspect-fit and provision its video surface without a page-specific path. Its
header, output, and control panels are opaque black over the diagnostic image.

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
SDR-white/display change rerender one unchanged PQ signal. A manual target peak
is applied only while the active video route is Diagnostics; Player always uses
the live platform target, even if the retained HDR Lab controls remain manual.

Diagnostics label the input path separately from output interop. The current
libplacebo lab source reports one fixed-size software-frame CPU upload; the
direct D3D11 and Metal targets report zero output copies or CPU transfers.
Hardware-frame imports remain separately diagnosed rather than making every
path look “zero-copy.”

Player is the default page and HDR Lab is reachable only through About.
Reusable read-only source, presentation, and pipeline diagnostics appear in
the optional Player details panel, but HDR Lab controls do not become ordinary
player preferences or production presentation state.

## Verification

The shell scenario runs with Basic and Fusion styles to exercise their distinct
switch label roles. It covers the idle overflow action's light palette and
Tab-reason focus, plus focus highlights on custom transport/window controls.
Native Windows/macOS rendering and full keyboard traversal still need desktop
validation. See the [control contrast research](../../research/2026-10-17-qt-control-contrast.md)
for the style contracts and rejected alternatives.

Focused Qt Test coverage verifies viewport geometry, visibility, notification,
and renderability. A non-presenting Qt Quick component test creates the real
QML shell through the same initial-property contract, resizes it, and verifies
that active-page geometry and visibility reach `VideoViewportState`. It also
verifies Empty/Opening/Ready/Error visibility, cancel/retry/close and
play/pause command wiring, About-only Player/HDR-Lab routing and viewport selection, the
optional details panel with selected video/audio/subtitle, source signal,
presentation, and performance text, plus the diagnostic renderer switch's
default and source binding. Source/router coverage verifies that the diagnostic route
retains its 16:9 input geometry, and the component test verifies the HDR Lab's
opaque black panel backgrounds. It explicitly covers `Ready` before `hasFrame`: playback chrome
and the viewport remain active while the preparing state is visible, then the
preparing state disappears after frame publication. It also verifies full-page
Player geometry, timeline formatting, scrub-preview time, relative and slider
seek commands, fixed-origin burst accumulation/cancellation and boundary
clamping, active-seek supersession, cleared-frame viewport with enabled
transport and selected-track volume controls, buffering feedback without
transport pinning, backend position
updates, fullscreen gesture release/cancellation ordering, popup Escape priority
state, island hit testing, and native-cursor intent without launching a native
dialog. The
window-command fake additionally proves the Windows-only menu action's
visibility and two-way checked-state binding; it does not claim physical monitor
coverage.
The shell test also
checks disabled, floating, maximized, tiled, and fullscreen application-chrome state;
stable empty-page inset; full-root, media-independent outline visibility and
DPR-derived thickness; and the invariant that title fade never moves an active
video viewport. The real D3D11 and Metal captures
verifies that zero video geometry and the compositor's fallback
binding produce the normal background rather than sampling the retained video
surface. It also destroys a bound diagnostic producer, creates the other
implementation, rebinds the compositor, and captures the new result. The
registered audio-first application playback scenario crosses the production
FFmpeg, Cubeb, QML, QRhi, libplacebo, and swapchain paths and exits
noninteractively after observing real presentation plus continued clock
progress.

The same shell test verifies direct Report/About menu routing in active and
idle states, absence of Player-page HDR Lab actions, the About controller's
diagnostic-route request, and all five media-error commands. Native dialog
pixels are not a test contract; platform dialogs and browser/clipboard dispatch
stay behind the controller/window boundaries.

The component test also asserts that macOS resolves `FileDialog.parentWindow`
to the supplied visible window-command host, while other platforms leave the
property unset for Qt Quick's automatic/native-fallback behavior. This protects
the redirected-scene boundary without opening a modal operating-system dialog
in automation. A user-confirmed run of the rebuilt application also opens the
native file sheet without materializing a second blank window.

The same component test verifies that the dialog sends its exact selected URL
to the application window command rather than bypassing it through
`MediaSession`. An application media-open notification returns HDR Lab to
Player, allowing startup, dialogs, native drops, and later platform activation
adapters to share one page-routing contract without moving route ownership into
C++.

The Windows-registered fullscreen application scenario additionally crosses
the real window, QRhi swapchain, QML, media, and video-presentation boundaries
while driving native F11, Escape, Space, and redirected background double-click
input and checking normal/maximized restoration, cursor hiding, and one
advancing cubeb audio epoch. On Windows it also enables other-display blanking
and checks the native companion count, target screens, fullscreen flags, focus
policy, and destruction on exit. A two-display Windows check is still required
for black pixel coverage, presentation-screen movement, and hotplug behavior.
Linux runs it explicitly and non-gating while its
current WSLg buffer/configure failure remains unresolved.

On Apple M2/macOS 26, the registered playback scenario crosses the production
Metal/MoltenVK window and AudioUnit clock. One clean direct fullscreen smoke
passes, and interactive fullscreen is user-confirmed working. Repeatable
live-desktop automation remains non-gating because concurrent input can affect
the harness.

Extend Qt Quick component coverage as commands gain behavior. Add
actual-application scenarios for file open, navigation, diagnostics access,
and shutdown when the private scenario-control boundary exists.

The Playback settings page includes a default-disabled “Prefer HDR10+ over Dolby
Vision” checkbox. It changes the compatible dual-format representation live,
including while paused, and persists through the existing application settings.

HDR reference white uses a 100–203-nit slider and numeric field with 100/203-nit
preset buttons, default 100. It adjusts PQ/HDR10+/Dolby Vision video on SDR and
HDR outputs through shared mapping. SDR input, HLG, UI, and subtitle brightness
are unchanged. Edits use the existing paused-frame update path.

Absolute PQ is off by default and available only on supported Windows HDR
output. Enabling it disables the source reference-white controls. On other
Windows outputs its checkbox is greyed out, its saved preference is retained, and
adaptive rendering and reference-white controls remain active. Output changes
refresh these controls while Settings is open. Non-Windows platforms hide the
Windows-only checkbox and status row entirely. HLG remains unchanged.

Video details identify both Dolby Vision and HDR10+ on dual-format frames. Output
details refresh after successful presentation, including paused settings edits.

Subtitle brightness is a 0–100% slider with a percentage field, default 80%.
It changes the entire subtitle layer in linear light without editing its colors
or opacity. 100% matches display reference white on SDR and HDR. It remains
available with authored styling; style presets preserve it and Restore defaults
returns it to 80%. Changes apply while paused through the existing settings signal.

## Desktop media command interaction

The UI play/pause action calls the same MediaSession toggle as native toggle
requests. Relative-seek preview and its existing 180-ms accumulation stay in
QML. An external position jump or Stop, Close, replacement open or error clears
pending UI preview so it cannot overwrite a later native command. Stop adds no
new button, black viewport or teardown-driven UI state; its retained frame is
presented like a paused frame, and Play restarts the item at zero.

## Jellyfin browser

The empty Open video screen offers local-file opening and Jellyfin. During
playback, an upper-left Back action closes media and returns to the page that
opened it (Jellyfin retains its library/season view; local opening returns to
Open video). Direct launches without UI history default to Open video. This is
one QML return-page value, not a navigation stack or media-source abstraction.
The browser's Back/Forward follow visited browse history; Back at its beginning
returns to Open video and cancels a pending playback resolve. Forward from Open
video can return to the retained browser, without replaying closed media. Home has library tiles/tabs, Continue
Watching, and Recently Added shelves. Shared poster, shelf, detail-header and
episode-row components show artwork, descriptions and watched/progress state.
The password form clears its password after submit.
Browser selection supplies the ordinary player, with Resume/Play from start.
The browser owns no video viewport and blocks player keyboard shortcuts while
editing credentials. API/authorization logic stays in C++.
See [Jellyfin](../jellyfin/README.md) for exact scope and acceptance gaps.

The shell regression checks navigation from local/Jellyfin/direct launch and
actual overlay bounds under CSD, fullscreen and native decorations, while
verifying that active video still occupies its full viewport. Both Basic and
Fusion style runs cover the same behavior.

Jellyfin scrolling uses Qt's default Flickable/ListView/GridView gesture,
inertia and bounds behavior with standard attached scrollbars, including a
horizontal scrollbar for home shelves. Do not add custom wheel/velocity logic
or override those defaults without a demonstrated requirement. Descriptions
wrap fully within their scrollable page; no expansion state is maintained.


The Jellyfin title has horizontal breathing room beside an icon-only Back
control, and Home/library tabs share that header. The page uses the
same black background as Open video; illustrated
headers span the window width with square edges, while text/cards retain
24 px horizontal insets and 20 px before gallery cards. The toolbar arrow moves
one logical level up (episode list → seasons → TV library → Open video),
independently of same-level Home/library switches. Thumb buttons and platform
standard keys follow visit history. Home selections resolve the server-provided
ancestors so Up and playback return retain the actual media hierarchy.
Browser Backspace remains an editing key. See the
[header/navigation plan](../../plans/ui/2026-10-18-jellyfin-header-navigation.md).

Native controls retain the system/style palette and color scheme. Do not force
application or shell-wide foreground colors onto native button backgrounds.
Only text directly on the custom dark canvas is explicitly light, including
descriptive labels and the Remember me checkbox label. Jellyfin toolbar actions
use standard buttons with native state colors. Ordinary buttons and text fields
keep their native foreground/background pair.

About → Open logs opens the current session log's folder, rather than guessing
a host temp path for sandboxed processes. When session file initialization
failed or --no-log-file was supplied, this action is disabled with an
unavailable tooltip. Native folder-launch failure remains visible in About.
