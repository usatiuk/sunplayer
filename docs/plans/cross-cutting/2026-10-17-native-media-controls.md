# Native desktop media controls

Date: 2026-10-17
Status: Implemented; focused Linux validation recorded below. Windows/macOS native validation remains open.

## Scope

Implement Linux MPRIS, Windows SMTC, and macOS Now Playing/Remote Command
Center together. Native controls call the same MediaSession actions as the UI.
There is no native-specific playback lifecycle. Do not gate implementation of
one backend on manual acceptance of another. Evidence and library comparisons
are in the [research note](../../research/2026-10-17-native-media-controls.md).

This proposal replaces the earlier, unapproved teardown-based Stop design.

## Delivered shared changes

Replace the existing private play-intent boolean with three values:

```cpp
enum class PlaybackIntent { Playing, Paused, Stopped };
```

Keep lifecycle State as Empty/Opening/Ready/Error. Keep the existing worker-facing
atomic play boolean, derived from that intent at the same thread boundary.
Do not add a parallel stopped boolean or another transport state machine.

The small public additions are:

```cpp
Q_INVOKABLE void stop();
Q_INVOKABLE void togglePlayback();
bool stopped() const;
std::uint64_t mediaItemId() const;
MediaClockSnapshot mediaClockSnapshot() const;

signals:
    void positionJumped(qlonglong positionMilliseconds);
```

* Stop uses existing pause-like clock/audio halt mechanics and records Stopped.
  Retain the frame, position, buffers, selections and media. Subsequent Play
  uses the existing restart-at-zero path. Pause after Stop is a no-op. Stop
  also records intent during Opening or EOF, where current pause() may reject
  the command; a newly created audio sink must receive false play intent.
* Existing Play/Pause availability remains as in the UI. Do not broaden initial
  Opening behavior to satisfy a native client; advertise the same availability.
  Toggle simply centralizes the existing Play/Pause choice and treats Stopped
  or EOF as Play. QML still owns control visibility and interaction feedback.
* Natural EOF, stream changes and asynchronous completions must preserve Stopped
  intent. Graphics recovery may finish paused/stopped, as it can after Pause.
  No Stop-specific recovery cancellation is needed. If Play from Stopped occurs
  during a pending seek/recovery, the existing deferred restart must target zero;
  the older saved recovery position must not overwrite the later Play request.
* Capture the existing nonzero decoder generation once at accepted new-media
  open into a retained item ID. Later decoder restarts do not replace that ID.
  Clear it on Close; zero explicitly represents no item. Retry follows the
  existing new-open path and may get a new ID. No separate counter or registry.
* The clock getter returns the existing clock snapshot, with the same bounded
  position policy as the public timeline. It adds no stored state, clock or DTO.
  Actual advancement is already available there and can differ from playing().
* The discontinuity signal reports a real logical position jump from any origin,
  including UI seeking/replay. Ordinary timeline ticks and same-position decoder
  restarts are not position jumps. This supplies MPRIS notifications without
  inferring seeks from decoder generations or polling heuristics.

Use existing seekToMilliseconds(), volume/mute setters and notifications. Make
existing seekable() agree with the existing seek command's acceptance predicate;
retain raw source seekability internally. No canSeek()/canControlPlayback()
wrappers. Keep QML's 180-ms seek preview/accumulation in QML. Native relative
requests validate their protocol input and call the existing absolute seek.
QML cancels a pending preview when a later native jump, Stop, Close, media
replacement or error supersedes it. No shared seek timer, preview model, request
identity or flush/cancel API.

## Native boundary

One build-selected object, constructed through one factory:

```cpp
class NativeMediaControls {
public:
    virtual ~NativeMediaControls() = default;
};

std::unique_ptr<NativeMediaControls>
createNativeMediaControls(MediaSession& session, QWindow& window);
```

PresentationWindow owns it. Dependencies are mandatory references. Observe
existing session signals, publish current facts, dispatch native commands onto
the session's Qt thread. Adapter-local caches/resources are sufficient; no
shared publication snapshot, controller, command bus, backend registry or
cross-platform scheduler.

Destroy native controls before the session/native window. Each implementation
owns and removes its native registrations; callback context must remain safe
for callbacks already in flight. Use native/Qt lifetime mechanisms and small
local ownership, not a generic callback framework. Windows attaches to the real
HWND, respects its destruction/recreation, and balances its own WinRT reference.
No adapter depends on graphics-device or decoder generations.

Only MPRIS SetPosition carries an authoritative item ID: validate it at execution.
Do not fabricate Windows/macOS item tags for events that do not contain one.
Generic Play/Pause acts on the current session. Recheck command availability
when dispatched rather than treating earlier native enablement as authority.

## Backend policy

| Platform | Integration and required differences |
| --- | --- |
| Linux | Existing QtDBus; required MPRIS root and Player interfaces, signed-microsecond validation, stable object-path item ID, property/discontinuity notifications, effective-volume property. CanControl stays constant true; item capabilities vary. |
| Windows | Existing SDK C++/WinRT; GetForWindow on the real HWND; manual metadata/status/timeline, native button/seek events, local periodic timeline refresh as recommended by Microsoft. |
| macOS | Public MediaPlayer shared information/remote-command centers; metadata, explicit playback state, elapsed time/rate, command handlers and seeking/skips. No artificial AVPlayer or private APIs. |

MPRIS has no buffering status. Keep Playing tied to active intent so PlayPause
matches the UI; correct the reported position on hold/resume and discontinuities.
Do not prescribe an extra correction timer/cadence or require a perfectly frozen
shell slider. Add bounded periodic correction only if real client behavior proves
it necessary. Other adapters map existing hold/advancement facts to native
status/rate. These observations need no new shared buffering state.

MPRIS SetPosition ignores stale/out-of-range requests before lossy conversion.
Over-end relative Seek follows unavailable Next (no effect with CanGoNext false).
Rate/min/max remain 1; setting Rate zero invokes Pause as specified. Normal
position advancement does not emit PropertiesChanged. Optional playlists,
loop/shuffle, URI opening, fullscreen and Raise/Quit controls stay unsupported
with truthful declarations. Native Next/Previous remain unavailable.

Publish current title and known duration/position; omit unknown metadata, raw
paths/URLs and artwork. Keep paused/stopped media available; clear publication
on Close/Error. HDR Lab does not become a native media item. MPRIS volume writes
use existing bounded volume and clear mute; mute itself retains stored gain.
Desktop media-key arbitration remains OS policy, without competing global hooks.

Use QtDBus + C++/WinRT + Apple MediaPlayer. Evaluated libraries either require
patching protocol/error behavior (Amber MPRIS), a large toolkit dependency set
(Desktop App Toolkit), Rust/FFI plus contract fixes (Souvlaki), or do not cover
maintained three-platform support (libwinmedia). See research sources. No new
third-party runtime or playback engine is proposed.

Missing native service/bus is an observable external failure; preserve playback
and retry at an existing bounded lifecycle/session trigger where supported.
Validate actual QtDBus reconnect behavior during implementation rather than
inventing a bus-recovery service. Bad registration/signatures/ownership remain
program defects, not silent fake backends.

## Focused delivery evidence after signoff

* Shared behavior: Pause resumes in place; Stop holds and Play restarts at zero;
  Stop during Opening/EOF/recovery remains stopped; deferred recovery cannot
  overwrite a later Play-from-zero; track changes retain intent and item ID.
* Identity: accepted new open changes ID; seeks/recovery retain it; stale MPRIS
  SetPosition is ignored. Public seek capability agrees with command acceptance.
* Real isolated D-Bus calls: required members, types, raw bounds, notifications,
  fixed capabilities, buffering toggle behavior and metadata clearing.
* UI interaction: a pending relative preview cannot overwrite a later native
  command. No visual/lifecycle redesign or full-suite baseline before signoff.
* Affected builds/QML checks and native callback/window teardown checks where
  hosts are available. Report unavailable native evidence explicitly. Implement
  all three backends without waiting for another platform's manual acceptance.

Update current subsystem docs, PLAN and TESTING with actual delivered behavior.
Commit/push are separate actions; this signoff covers implementation only.

## Independent critique

Three read-only agents independently reviewed shared playback, native APIs and
lifetimes, and simplicity/evidence, each explicitly critiquing overengineering.
Two independently reassessed the reduced intent-based Stop design. Removed:
new lifecycle State::Stopped, decoder teardown/black viewport, Stop-specific
recovery cancellation, initial-Opening Play/Pause expansion, extra serial counter,
capability wrappers, relative-seek/preview API expansion, blanket event fencing,
and mandatory MPRIS correction cadence. Retained only actual protocol and native
resource-lifetime obligations. All three found no fundamental blocker to the
reduced design; focused correctness edges above remain implementation checks.

User signed off implementation of the reduced design. All three backends now
exist. The user subsequently authorized a local commit on main, with no push.

## Delivery evidence

Linux Debug player, media-session, QML shell and Linux native-controls targets
build successfully. Both QML lint targets pass. Final focused CTest run passes 4/4 in 33.52 seconds: media-session (10.89 s),
qml-shell Basic (11.16 s), qml-shell Fusion (11.09 s), and
linux-native-media-controls (0.39 s). This is affected-suite evidence, not a
claim that every repository test or native platform was exercised. New shared regressions cover Stop/replay,
initial opening, EOF, recovery and replay superseding a saved recovery target.
Two real FFmpeg/ControlledAudioSink rows exercise Stop during playback and before
sink creation, checking paused output and same-item replay from zero. Basic and
Fusion UI scenarios check pending-preview supersession by native jump/Stop.

The isolated D-Bus target additionally covers actual cross-item stale seeks and
controlled-audio hold/toggle/resume, consistent millisecond precision in MPRIS
microsecond fields, and registration/teardown. Tests use an isolated bus, never
the developer's live media service. Sandbox restrictions on D-Bus authentication
require the test process to run outside that filesystem sandbox.

Three independent read-only reviews covered core behavior, native protocol and
callback lifetimes, and simplicity/evidence. Fixed MPRIS reserved item-path prefix,
time precision and external registration failure classification; Windows now
recovers only explicit service/disconnection HRESULTs and catches native event
argument failures. Native review rechecked those fixes with no new blocker.
Added audio/hold coverage from review. Kept the signed-off hold/resume correction
policy instead of adding periodic correction to make every shell slider exact;
real GNOME/KDE long-hold rendering remains a deferred observation.

Windows/macOS sources use actual SDK APIs and received independent source review,
but SDK builds, shell/media-key behavior, app attribution, arbitration and native
surface/callback interaction remain unverified on this Linux host. No native
success or all-platform test claim is made. Subsystem docs, PLAN, TESTING,
DEFERRED and ADR 0035 describe the delivered implementation and these limits.


## Local AppImage refresh

Before the local commit, `packaging/appimage/build.sh` rebuilt
`out/appimage/SunPlayer-1.2.1-ubuntu26.04-x86_64.AppImage` and its SHA-256 file.
Docker packaging, isolated dependency/icon/QML checks and production playback
smoke passed; playback reported Pulse audio, position 1501 ms and 72066 presented
audio frames. The generated checksum verifies. The smoke has no session bus,
so MPRIS correctly reports unavailable without preventing playback; actual
MPRIS wire behavior is covered by the separate isolated-bus test. Generated
artifacts stay ignored/local. No push was authorized after the user's correction.
