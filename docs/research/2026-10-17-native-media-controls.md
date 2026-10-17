# Native desktop media controls across platforms

Date: 2026-10-17. Research only; no implementation or architectural decision
is accepted by this note. One read-only research agent investigated each
platform, with shared playback/application inspection by the coordinating
agent. Findings are based on documentation and source inspection, not native
runtime experiments.

## Question and project baseline

How should SunPlayer expose playback to desktop media controls and hardware
media keys while retaining FFmpeg, cubeb, and its current rendering pipeline?

The intended platforms are native-Wayland Linux, Windows desktop, and macOS.
`MediaSession` already owns GUI-thread play/pause, absolute seek, play intent,
actual advancement, buffering, duration, position, media URL/display name,
volume, and mute. The application owns one presentation window. Its existing
`PlaybackPowerInhibitor` demonstrates a narrow native integration boundary;
power inhibition and media-control publication should remain separate concerns.

Important existing constraints:

* `playbackGeneration()` changes on seek, stream switching, and recovery. It
  identifies decoder work, not the lifetime of the open media item.
* `cancel()` clears media; `pause()` retains its position. There is no shared
  Stop operation with retain-item/restart-from-beginning semantics.
* Relative seek bursts currently coalesce in `src/app/pages/PlayerPage.qml`.
  Native commands should not require a QML page to exist or be visible.
* Public timeline values are integer milliseconds, with unknown duration
  represented by `-1`; native adapters must translate that boundary explicitly.
* Linux already requires and links QtDBus for the inhibition portal.

## Linux: MPRIS 2 on the session bus

Use QtDBus to publish the standard root and Player interfaces at
`/org/mpris/MediaPlayer2`, under an `org.mpris.MediaPlayer2.*` service name.
TrackList and Playlists are optional. Multiple running processes need distinct
instance names. Root identity should match the installed desktop entry
(`sunplayer` in the current AppImage packaging).

The protocol has several consequential requirements: Stop retains the item and
Play restarts it from the beginning; a current item has a D-Bus object-path
track ID; times use signed 64-bit microseconds; stale or invalid SetPosition
requests are ignored. Ordinary position advancement does not emit
PropertiesChanged; discontinuities use Seeked. Fixed-rate playback still
exposes rate limits of 1.0. These are adapter contracts, not reasons to change
the media clock.

Sources: [MPRIS specification](https://specifications.freedesktop.org/mpris/latest/),
[Player interface](https://specifications.freedesktop.org/mpris/latest/Player_Interface.html),
[root interface](https://specifications.freedesktop.org/mpris/latest/Media_Player.html).

GNOME discovers these services and routes media-key actions through them;
selection among applications is desktop policy. This requires no Wayland
keyboard grab. Desktop presentation, including lock-screen visibility, is not
a uniform MPRIS guarantee. Qt adaptors fit the existing dependency; explicit
D-Bus property-change publication still needs verification.

Sources: [GNOME media-key controller](https://raw.githubusercontent.com/GNOME/gnome-settings-daemon/master/plugins/media-keys/mpris-controller.c),
[GNOME Shell consumer](https://raw.githubusercontent.com/GNOME/gnome-shell/main/js/ui/mpris.js),
[Qt adaptor API](https://doc.qt.io/qt-6/qdbusabstractadaptor.html).

Production reference: [VLC D-Bus module](https://raw.githubusercontent.com/videolan/vlc/master/modules/control/dbus/dbus.c)
uses per-instance naming, player observations, and coalesced changes. Its direct
libdbus machinery is not a reason to add libdbus code beside QtDBus.

## Windows: manual System Media Transport Controls

Use the documented desktop `ISystemMediaTransportControlsInterop::GetForWindow`
route with the real top-level presentation HWND. Manually publish playback
status, capabilities, timeline, and metadata; consume button and position
requests. Retain FFmpeg/cubeb. `GetForCurrentView` is not the desktop entry point.

ButtonPressed is delivered outside the UI thread, so dispatch to the Qt owner.
Publish start/end/position and valid seek bounds. Microsoft recommends timeline
updates approximately every five seconds while playing plus updates on seeks
and state changes. DisplayUpdater supplies explicit metadata updates/clearing;
use existing session facts instead of a second Windows media-file probe.

Sources: [desktop WinRT support](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/winrt-api-desktop-app-support),
[GetForWindow](https://learn.microsoft.com/en-us/windows/win32/api/systemmediatransportcontrolsinterop/nf-systemmediatransportcontrolsinterop-isystemmediatransportcontrolsinterop-getforwindow),
[manual SMTC integration](https://learn.microsoft.com/en-us/windows/apps/develop/media-playback/system-media-transport-controls),
[DisplayUpdater](https://learn.microsoft.com/en-us/uwp/api/windows.media.systemmediatransportcontrolsdisplayupdater).

The researched desktop route does not establish MSIX, backgroundMediaPlayback,
or an explicit AppUserModelID as prerequisites. Installed/unpackaged name and
icon attribution remain native validation items. Do not request
globalMediaControl: that concerns controlling other applications' sessions.
SMTC has no numeric application-volume setter; its SoundLevel is read-only.
Keep volume/mute in the existing audio boundary.

Source: [SoundLevel](https://learn.microsoft.com/en-us/uwp/api/windows.media.systemmediatransportcontrols.soundlevel).

Production comparison: [VLC SMTC plugin](https://github.com/spmn/vlc-win10smtc/blob/master/vlc-win10smtc/module.cpp)
demonstrates manual publication but uses an auxiliary MediaPlayer and shell
seek heuristics. Neither is automatically appropriate for SunPlayer.

## macOS: Now Playing and Remote Command Center

Use an Objective-C++ adapter over `MPNowPlayingInfoCenter.default()` and
`MPRemoteCommandCenter.shared()`. These public APIs publish metadata and receive
system commands independently of SunPlayer's decoder/audio implementation.
`MPNowPlayingSession` is shaped around AVPlayer instances; adding an artificial
AVPlayer for this integration would introduce unnecessary lifetime and state.

Explicitly update `playbackState` as playback begins or halts. Publish elapsed
seconds and playback rate; the system extrapolates, so per-frame position
updates are unnecessary. A held timeline needs rate zero. Keep user play intent
separate so a toggle during buffering still requests pause. Seek, pause, resume,
and interruption transitions need fresh timeline anchors.

Sources: [Now Playing information center](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfocenter),
[Remote Command Center](https://developer.apple.com/documentation/mediaplayer/mpremotecommandcenter),
[Now Playing Session](https://developer.apple.com/documentation/mediaplayer/mpnowplayingsession),
[playbackState](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfocenter/playbackstate),
[elapsed-time semantics](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfopropertyelapsedplaybacktime).

Suggested scope adds native seek-to-position and skip-back/forward commands
with ten-second preferred intervals, subject to seekability. Disable unsupported
track/repeat/shuffle controls. Keep paused media available; on close clear
nowPlayingInfo, mark stopped, and disable commands. Remove handler tokens on
teardown, and route callbacks safely to Qt's session owner.

Apple's official sample separates the platforms: its macOS behavior updates
playback state without AVAudioSession setup, and its macOS plist has no iOS
background-audio declaration. The macOS sample entitlements do not establish a
special media-controls permission. Do not import iOS background-mode rules or
add Accessibility/event taps/private MediaRemote APIs for ordinary controls.

Sources: [Apple sample](https://developer.apple.com/documentation/mediaplayer/becoming-a-now-playable-app),
[sample source archive inspected by the macOS agent](https://docs-assets.developer.apple.com/published/6eacaf8a4885/BecomingANowPlayableApp.zip).

Production reference: [IINA NowPlayingInfoManager](https://github.com/iina/iina/blob/develop/iina/NowPlayingInfoManager.swift)
uses these public APIs around libmpv. It includes an initial-paused arbitration
workaround; treat that as a native test case, not a workaround to copy without
reproduction. Silent video, mute, first-open-paused, competing media apps, and
bundle attribution need native verification. No primary requirement to produce
audible output was established by this research.

## Shared direction to evaluate

One application-owned bridge should translate canonical session observations
into three native implementations and route native commands to shared playback
actions. It should not own another player, clock, playlist, or recovery policy.
Use latest-value reconciliation. Keep native subscriptions and handles within
the adapter, and detach before session/window destruction. Queue callbacks to
the session thread, with QObject lifetime safety and media-replacement checks
where a request actually targets a particular item.

Suggested first user-visible scope: play/pause, timeline seeking where offered,
current display title, duration/position, and truthful command availability.
Leave Next/Previous disabled without a playlist; do not relabel them as ten-
second jumps. Artwork can follow once a real metadata/artwork source exists.
Use the platform's media-key routing first; do not add competing global hooks.

Resolve these behavior questions before implementation:

1. Define shared Stop semantics. MPRIS cannot simply omit Stop. Neither cancel
   nor pause is an accurate implementation. Avoid an adapter-only stopped flag
   that disagrees with the playback owner.
2. Give an open item a stable identity across seeks/recovery for MPRIS stale-
   request protection. A new open, including reopening the same path, can be a
   new item. Do not use paths as public object identifiers or decode generation
   as item identity.
3. Map buffering/seeking deliberately: keep play intent for command toggling,
   but prevent desktop progress extrapolation from advancing through a frozen
   clock. Native status/rate vocabularies differ. There is no need for a new
   cross-platform playback state machine merely to make observations atomic.
4. Decide whether repeated native relative seeks justify moving the existing
   burst policy out of QML. Preserve one meaningful command policy rather than
   copying page logic into each native adapter.
5. Define MPRIS volume/mute interaction: it exposes volume without a separate
   mute property. Avoid losing the stored nonmuted gain or accidental unmute.
6. Decide metadata privacy and idle registration policy. A display title can
   appear outside the app; raw file URLs need not be exported by default.
   Clear stale media on close/error; do not advertise HDR Lab as playback.

Integration failures need classification: absent desktop services or a lost
session bus are external failures that should leave playback usable and have
bounded recovery triggers. Invalid protocol construction or impossible native
ownership is a program defect. Record useful integration diagnostics without
putting media paths in support reports.

## Simplest complete candidate architecture

Follow-up design constraint: DRY, KISS, and YAGNI, while satisfying the actual
native contracts. This section refines the recommendations above; it remains a
research proposal, not an accepted implementation plan or a claim of runtime
correctness.

### Ownership and shape

Create one platform-selected `NativeMediaControls` object through one factory.
The application owns it within the existing single-window composition graph;
it holds a mandatory `MediaSession&`. Supply the real native window only to
the implementation that needs it. The object is destroyed before the native
window and session. A common destructor/lifetime interface is enough: do not
add a bridge, controller, service registry, command bus, and backend hierarchy
around this one boundary.

The selected implementation observes existing session signals, reads current
facts on the session thread, publishes native state, and invokes session
commands. Linux's two required D-Bus adaptors are protocol structure, not two
playback owners. Windows event subscriptions and macOS command targets are
owned resources with explicit removal. Cross-thread callbacks post to a
lifetime-bound Qt receiver; revocation and callback teardown must also protect
any native callback context, not merely its eventual queued Qt invocation.

An adapter may retain its last published values and native subscription state.
Those are caches/resources, never authoritative playback state. Add no shared
timer or snapshot generation: only the Windows adapter needs the documented
periodic timeline refresh. Reading current values is sufficient; do not enqueue
a backlog of old publication snapshots.

### Shared behavior belongs to the existing playback owner

* Add a real Stop command/state to MediaSession. Retain current item and selected
  tracks, halt progression, and have Play restart from the beginning. Stop is
  idempotent and works independently of seekability, including opening/seek
  cancellation. Model stopped intent explicitly in the existing transport
  representation instead of adding another loosely related boolean in an
  adapter. Resource release and stopped-frame presentation require a bounded
  implementation choice; do not pretend pause-at-zero alone provides stopped
  semantics.
* Share toggle and relative-seek policy through MediaSession commands when
  introducing the additional native consumers. Toggle uses user intent plus
  end/stopped behavior, not a desktop's temporarily displayed buffering state.
  Move the existing relative-seek accumulation policy out of QML rather than
  copying it; keep scrub-preview display state in QML. Absolute-seek and relative-
  seek entry points must resolve pending relative work consistently. Replacement
  and Stop cancel pending work so it cannot act on a later item.
* Keep the media URL as the existing source identity. Add just one opaque
  current-item serial at the accepted-new-open boundary for public protocol
  identity. Retain it across seek/recovery/stream switch and invalidate it on
  close. It identifies an open occurrence; no item registry, UUID framework,
  content hash, or new media object hierarchy is needed.
* Derive shared command availability from the same session rules used by the
  commands. Keep guards at execution time because native enablement is only
  an observation. No writable mirror of capabilities or transport state.

### Native differences stay at the protocol boundary

| Concern | Linux | Windows | macOS |
| --- | --- | --- | --- |
| Commands | Required Player methods | Enable supported SMTC requests | Register supported remote commands |
| Stop | Implement required behavior | Enable once shared behavior exists | Enable once shared behavior exists |
| Timeline | Microseconds; query + discontinuity signal | TimeSpan; bounded periodic refresh | Seconds + rate; extrapolated |
| Held clock with play intent | Paused publication while held; keep internal intent | Changing for transient work where appropriate | Paused state and zero rate while held |
| Volume | Required property mapped to effective gain | No equivalent writable numeric SMTC volume | No equivalent ordinary remote volume command |
| Item identity | Opaque object-path track ID | Existing item metadata; no invented generation | Existing item metadata; no invented generation |

The held-clock mapping above is a candidate to validate with real clients,
especially command behavior while buffering. Do not claim native arbitration
or shell UI behavior is guaranteed by the internal design.

MPRIS adapters must preserve protocol distinctions: SetPosition rejects stale
IDs and out-of-range values, whereas relative Seek has its specified end/start
behavior. Validate signed arithmetic and units before calling shared commands;
do not change protocol semantics merely to reuse the UI's clamping behavior.
Emit Seeked for real discontinuities from every command origin, including the
application UI, rather than only acknowledging D-Bus seek requests. If current
signals cannot distinguish discontinuities, add a semantic playback signal,
not a polling heuristic or another clock.

For MPRIS volume, publish effective gain (zero while muted). An explicit remote
volume write sets the existing bounded gain and clears mute; internal mute
toggles preserve the stored gain. This is a proposed deliberate user-action
policy, not a new independent volume store.

Implement mandatory fixed-rate/protocol fields honestly. Leave optional
playlists, loop/shuffle, fullscreen control, Raise/Quit, and URI opening out of
the first scope where the protocol permits, advertising their absence. Required
methods whose capability is false follow their specified behavior. Next and
Previous remain unavailable. Use existing title/duration; omit unknown metadata
and artwork. Empty/error clears publication; stopped/ended retains the current
item as appropriate. Native input never depends on the Player page's lifetime.

Completeness means all mandatory contracts for the selected APIs plus truthful
capabilities for optional features, not implementing every feature an OS offers.
Correctness is grounded in one playback owner, explicit native-resource
lifetimes, boundary validation, and observable conformance tests. Native desktop
acceptance remains necessary even with those invariants.

## Verification before claiming support

Shared behavior tests should cover commands through the public playback
boundary, stop/replay, buffering, seek and recovery without item replacement,
EOF, close/error clearing, and replacement while a command is pending. Exercise
real MPRIS interfaces on an isolated D-Bus session for signatures, notifications,
stale SetPosition, capabilities, and extreme integer inputs.

Native acceptance needs GNOME and KDE Wayland, Windows installed and unpackaged
builds, and a macOS application bundle. Check media keys while minimized,
simultaneous browser playback, shell seeking, rapid replacement, pause/buffering/
EOF, metadata clearing, fullscreen/native-window lifetime, and shutdown with
callbacks in flight. Desktop arbitration and UI layout remain OS behavior.

No builds or runtime tests were run: this investigation changes documentation
only. No implementation milestone, accepted subsystem contract, or ADR changes
as a consequence of this research.
