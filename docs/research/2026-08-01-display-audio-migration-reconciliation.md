# Display and audio migration: project reconciliation

## Question

Which recommendations from the broader
[production-player migration review](2026-08-01-video-audio-switch.md) fit
SunPlayer's current implementation and pinned dependencies, and which would add
or remove real correctness?

This note distinguishes current project behavior, exact pinned-source facts,
production precedent, accepted policy, and experiments. The larger review is
research input rather than project truth.

The immediate simplification described below was implemented on 2026-08-01.
The current-behavior sections preserve the pre-refactor evidence that motivated
the decision.

## Current project behavior

### Display

SunPlayer currently has one small native `DisplayState`, one calculated
`PresentationTarget`, and one `displayTargetRevision`. The revision is not a
platform topology model. It is copied into `RenderedVideoSurfaceState` as a
cache invalidator even though the same state already records the target's
reference white, minimum luminance, and peak headroom.

The Windows provider caches one HWND-bound WinRT `DisplayInformation`, listens
to `AdvancedColorInfoChanged`, and republishes the latest state. Qt
`screenChanged` plus a center-screen movement check currently cause swapchain
recreation based on native screen identity, even if the semantic presentation
target has not changed.

Relevant project sources:

* `src/platform/DisplayStateProvider.cpp`
* `src/presentation/PresentationOutputState.cpp`
* `src/presentation/PresentationTarget.cpp`
* `src/presentation/RhiPresentationEngine.cpp`
* `src/video/RenderedVideoSurface.*`

### Audio

SunPlayer already keeps the important small pieces:

* Playback generation for stale media.
* A cubeb output epoch distinct from playback generation.
* User play intent separate from the `Buffering` interruption.
* A fixed-capacity callback-to-control ledger mapping media frames and hold
  silence to cubeb output frames.
* One MTA control thread that serializes cubeb lifecycle and position calls.

The current sink enumerates the Windows multimedia default, passes its explicit
device ID, requests `CUBEB_STREAM_PREF_DISABLE_DEVICE_SWITCHING`, and applies a
project cubeb patch that converts WASAPI reconfiguration into an error. Device
failure is currently terminal at `MediaSession`; replacement is planned but
not implemented.

Relevant project sources:

* `src/audio/CubebAudioSink.*`
* `src/audio/AudioOutputLedger.*`
* `src/playback/MediaSession.*`
* `vcpkg-ports/cubeb/fail-disabled-device-reconfigure.patch`

## Pinned cubeb findings

The project pins cubeb commit
[`ef47ae581df7c2f76058d554b3edde17f9ee7cba`](https://github.com/mozilla/cubeb/tree/ef47ae581df7c2f76058d554b3edde17f9ee7cba).
Inspection of the patched source used by the active vcpkg build confirms:

* A null output device follows the WASAPI default. Default-device changes and
  session disconnections signal the render thread's reconfigure event.
* Reconfiguration stops and closes the native clients, resolves the new
  default, rebuilds client and resampler state, and starts the native client on
  the same cubeb render thread.
* Closing the native client rolls `frames_written` into
  `total_frames_written`. `cubeb_stream_get_position()` reports that logical
  total minus current delay and clamps it against the prior position, so one
  cubeb stream has a monotonic logical position across migration.
* The WASAPI operations table does not implement
  `cubeb_stream_register_device_changed_callback`, and successful internal
  reconfiguration emits no distinct success state callback.
* The context collection callback reports default and device-state changes,
  but it is not a stream-migration completion event.

Therefore the broader report's proposal to start and re-anchor a new SunPlayer
epoch for every successful cubeb migration is not directly implementable on
this backend without another cubeb patch or inference from coarse events.

It is also not required for the simpler V1 contract. The logical position and
SunPlayer's existing media/hold history remain coherent within one cubeb stream.
Pending audio queued to the old endpoint may be skipped acoustically during a
route change, but subsequent media position and video selection converge on the
same logical stream. SunPlayer does not promise gapless or sample-perfect device
migration.

## Production precedent

The larger review's central simplification is supported by primary source:

* Microsoft recommends caching the HWND-bound
  [`DisplayInformation`](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.display.interop/nf-windows-graphics-display-interop-idisplayinformationstaticsinterop-getforwindow);
  it tracks window movement and provides fresh data for that window.
* mpv exposes the display Windows associates with its window and refreshes
  output-dependent state; it does not make display topology part of media
  identity.
* Firefox opens cubeb with a null device for the default route and keeps a
  compact
  [`FrameHistory`](https://searchfox.org/firefox-main/source/dom/media/AudioStream.cpp)
  so callback underrun frames do not advance media time.
* Chromium recreates its audio output on a device change and restores playback
  only when the prior state was playing. This supports keeping current user
  intent independent from output recovery.
* VLC and mpv translate native changes into local output reload/redraw work;
  they do not require the media pipeline to make every native event globally
  transactional.

These projects differ in backend ownership, so their exact recovery mechanics
are not copied. Their shared lesson is to keep native migration local and make
the player core own only media identity, clock meaning, and user intent.

## Accepted simplification

Strict identities remain only where they protect a real invariant:

```text
playbackGeneration       stale packet/frame/PCM rejection
graphicsDeviceGeneration native GPU-resource lifetime
audioOutputEpoch         one cubeb stream lifetime and clock mapping
```

Wayland preferred-description identity remains local protocol state, not a new
cross-platform generation.

Display state uses latest-value reconciliation:

```text
native hint
→ mark dirty or requery
→ normalize one semantic PresentationTarget
→ compare with the current value
→ rerender target-dependent video if needed
→ recreate presentation resources only if encoding or lifetime changed
```

Native display identity, raw capability values, provenance, and update reason
may remain diagnostics. They are not independent invalidation domains.

Normal default-audio migration is delegated to cubeb or the sound server. One
cubeb stream keeps one SunPlayer output epoch across that internal migration.
SunPlayer retains its media/hold ledger and current user intent. A cubeb error or
demonstrated prolonged no-progress condition may trigger one application-level
stream recreation; that recreation starts a new epoch, freezes at the last
confident media time, prerolls, and reanchors before advancing again.

The existing `Buffering` interruption remains. Replacing it with a generic
blocking-reasons bitset now would add abstraction because no second concurrent
timeline blocker exists in the implementation.

## Immediate refactor

1. Remove `displayTargetRevision` from the rendered-surface key and rely on the
   complete semantic target fields already present in the requested surface.
2. Stop recreating the swapchain merely because `QScreen` identity changed;
   keep recreation for a changed presentation encoding or resource lifetime.
3. Remove the planned cross-platform display identity/provenance/confidence and
   asynchronous query-generation graph. Keep raw platform facts diagnostic.
4. Open cubeb's default output with a null device, allow normal backend
   switching, and remove the fail-closed overlay patch and explicit endpoint
   enumeration.
5. Define the existing output epoch as one cubeb stream lifetime. Do not invent
   a hidden epoch for an internal backend reconfiguration that cubeb cannot
   report reliably.
6. Keep terminal stream error initially, then add one bounded stream-recreation
   path without reopening the media source as a separate coherent slice.

## Required evidence

Automated behavior should prove:

* Equal semantic display targets reuse a rendered video surface even after a
  native screen change.
* Reference-white/headroom changes invalidate and rerender a paused surface.
* Presentation-mode changes still request the required swapchain work.
* Hold silence does not advance media time.
* A cubeb stream reset advances the output epoch; an internal device revision
  does not silently invalidate the current media ledger.
* Pause intent survives future stream recovery.
* Stale observations from a replaced output epoch are ignored.

Real-device validation remains necessary for default-device switching,
Bluetooth disconnect/reconnect, sleep/wake, callback progress, and physical
A/V behavior. Add a no-progress watchdog only if these experiments show cubeb
can remain wedged without reporting an error.

## Resulting decision

The accepted policy is recorded in
[ADR 0016](../decisions/0016-reconcile-output-changes-semantically.md).
