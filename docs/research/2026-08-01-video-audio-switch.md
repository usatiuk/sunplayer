# Display and audio migration in production media stacks

Research snapshot: August 1, 2026.

Scope: Windows, macOS, and Wayland. X11 is intentionally unsupported and is not treated as a fallback target.

Evidence labels:

* **[Documented]** Platform or library contract.
* **[Source-confirmed]** Observed directly in current production source.
* **[Issue evidence]** Demonstrated bug, regression, or maintainer discussion.
* **[Inference]** Conclusion derived from the cited behavior.
* **[Recommendation]** Proposed SunPlayer policy.
* **[Experiment]** Requires validation on real systems.

## 1. Executive conclusion

The smallest sound SunPlayer architecture is:

```text
playback generation
graphics-device generation
current semantic DisplayTarget
audio-stream epoch
user playback intent
small audio frame-history map
```

It does not need separate display-selection, display-capability, provider, topology, or per-query generations.

### Main conclusions

**[Recommendation] Display migration can use the simple flow proposed in the question:**

```text
event or movement hint
→ mark display state dirty
→ requery at a render-safe boundary
→ compare semantic DisplayTarget
→ update presentation configuration if needed
→ rerender or recompose the paused frame
```

Platform APIs already resolve which display is relevant, though they do not always use identical rules while a window straddles monitors. Windows’ swapchain API uses greatest intersection, while Windows Advanced Color identifies the main display by the window center. Trying to create a perfectly atomic cross-API spanning-window state would therefore manufacture precision the platform itself does not provide.

**[Recommendation] Do not recreate the swapchain merely because the native output identity changed.** Recreate or reconfigure only when a semantic presentation property changes: swapchain format, surface color space, HDR/SDR presentation mode, or a native-resource lifetime. A reference-white or target-peak change normally requires a new tone-mapped frame, not a new swapchain.

**[Source-confirmed] Cubeb already performs substantial default-device migration internally.** The current WASAPI backend listens for default-endpoint and session-disconnection events, debounces endpoint changes, closes and rebuilds its internal clients on its render thread, and restarts the stream. The Rust CoreAudio backend similarly invokes asynchronous stream reinitialization for default-device changes.

**[Recommendation] SunPlayer should not immediately destroy its cubeb stream whenever it hears a device-change hint.** It should first treat the event as an audio-clock discontinuity, freeze the published media position, increment the audio epoch, and let cubeb’s backend recover. Application-level stream destruction is the fallback for a cubeb error, failed initialization, or a demonstrated no-progress condition.

**[Recommendation] Keep an explicit audio epoch above cubeb.** Cubeb’s WASAPI position implementation attempts to remain monotonic across internal reconfiguration by accumulating written frames and clamping against its previous result. Monotonicity is useful, but it does not prove that the returned number still has the same media-timeline meaning after latency changes, reinitialization, or injected hold silence.

**[Source-confirmed] A compact history of media frames versus hold-silence frames is justified.** Firefox keeps a frame history specifically because backend position can advance through underrun silence while media time must not. This is not evidence for a large recovery ledger; it is evidence for a small ordered mapping from output-frame ranges to media-frame progress.

**[Recommendation] User intent remains independent.** On recovery completion, resume only when `userWantsPlaying` is still true. Chromium uses essentially this rule: it records whether the stream was actually playing, recreates the output stream, and restarts only if it had been playing. A paused stream remains paused.

**[Recommendation] X11 should be explicitly unsupported.** Wayland without `color-management-v1` should fall back to SDR/sRGB presentation rather than falling through to an X11 implementation.

---

## 2. What mature players actually do

### Overview

| Project       | Display behavior                                                    | Audio behavior                                          | Architectural lesson                                                  |
| ------------- | ------------------------------------------------------------------- | ------------------------------------------------------- | --------------------------------------------------------------------- |
| mpv           | Native display selection, semantic requery, renderer redraw         | Backend requests generic AO reload                      | Small platform adapters and generic output restart                    |
| VLC           | Compact display format state; rebuild resources on semantic changes | Native backend requests generic audio-output restart    | Restart is local to audio output, not a global playback state machine |
| Chromium      | Maps current window to monitor when metadata is requested           | Recreates stream and restores only actual playing state | Intent preservation can be one boolean                                |
| Firefox       | Delegates endpoint recovery to cubeb                                | Maps cubeb position through frame history               | Backend migration and media-clock meaning are separate                |
| Qt Multimedia | Exposes changing default device and output list                     | Device selection is explicit at public API boundary     | Notification does not imply atomic system state                       |
| SDL 3         | Audio core owns logical/physical-device migration and conversion    | Large backend abstraction internal to SDL               | Useful if SDL is the audio policy owner; unnecessary on top of cubeb  |
| libplacebo    | Consumes target color/luminance state                               | No audio role                                           | It is a renderer, not a display-topology manager                      |

### mpv and libplacebo

**[Source-confirmed]** On Windows, mpv’s window code asks Windows which monitor is associated with the window, avoids publishing an update when the `HMONITOR` did not change, and then refreshes display-dependent information. It does not construct a hierarchy of topology, provider, selection, and capability revisions. The relevant source snapshot is `video/out/w32_common.c` at commit `1d15686142fd5d53c954aab7526cedab05ef9dc3`. Its public `display-names` documentation explicitly says that the first Windows display is the one selected by `MonitorFromWindow`.

**[Source-confirmed]** mpv 0.41 added and fixed more Wayland color-management behavior, including `color-management-v1` version 2 support, target-color-space handling, luminance conversion, and target colorspace on redrawn frames. These were renderer/window-backend fixes rather than additions of a global display transaction model.

**[Source-confirmed]** mpv audio backends can post `AO_EVENT_RELOAD`, described directly in source as a request for the player core to destroy and recreate the audio output. The core then rebuilds the output and applies its existing internal pause state to the new device. The recovery abstraction is therefore “audio output needs reload,” not a separate cross-platform endpoint migration state machine.

**[Source-confirmed]** libplacebo accepts source and target color information and performs rendering, scaling, tone mapping, and gamut mapping. It does not select native displays, observe OS topology, or own swapchains. Display observation belongs outside the libplacebo renderer.

**Takeaway:** mpv supports a relatively small core model: native output backends publish current facts and request redraw/reload; the player does not make all platform callbacks globally transactional.

### VLC

**[Source-confirmed]** VLC’s current Windows MMDevice backend listens for default render-device changes. When VLC follows the default device, the notification sets a flag and sends a generic `aout_RestartRequest`. Invalidated WASAPI operations likewise select the default device and restart. The native backend serializes acquisition on its own worker thread because Windows Core Audio’s threading contracts are difficult and callbacks may race.

**[Source-confirmed]** VLC’s D3D11 output keeps a compact `display_info_t`. It compares pixel format, luminance peak, color space, transfer function, primaries, range, and orientation, and rebuilds format resources only when that semantic value changes. The code even labels one peak calculation a “guestimate,” illustrating that elaborate provenance cannot make missing platform data precise.

**Takeaway:** VLC has significant backend-specific synchronization because it directly owns WASAPI. SunPlayer uses cubeb, so reproducing VLC’s MMDevice worker and endpoint-acquisition machinery above cubeb would duplicate the library.

### Chromium

**[Source-confirmed]** Chromium’s Windows HDR metadata helper caches output metadata by `HMONITOR` and resolves the current monitor from the window when queried. It refreshes the cache on display add/remove. Its conversion of `MaxFullFrameLuminance` into HDR content-light metadata is explicitly marked as a guess.

**[Source-confirmed]** Chromium’s audio `OutputController` closes and recreates a physical output stream on a device change. It remembers only whether its previous state was `kPlaying`, then restarts only in that case. The same component can use a fake stream where continued callbacks are required to keep video advancing, which is a browser policy choice rather than a requirement for all players.

**Takeaway:** preserving pause intent does not require a shared playback/recovery state machine. It requires that recovery completion consult current user intent rather than blindly restoring an old command.

### Firefox and cubeb

**[Source-confirmed]** Firefox obtains the cubeb output position and passes it through its `AudioClock`/`FrameHistory` mapping. Its callback records how many frames were serviced with media and how many were filled with zeros. This prevents injected underrun silence from incorrectly advancing media time.

**[Source-confirmed]** Firefox has also accumulated explicit native-lifetime precautions. One example releases locks before destroying a cubeb stream because historical WASAPI callbacks could arrive after stop. This complexity protects callback/native-object lifetime; it does not imply that display diagnostics require equivalent ordering.

**[Source-confirmed]** Current cubeb WASAPI and CoreAudio backends automatically reinitialize default-device streams. Explicitly selected devices use different policy, which is one reason cubeb has a “disable device switching” preference and an open issue separating explicit selection from following the system default.

**Takeaway:** for SunPlayer’s current “follow default” requirement, cubeb is the endpoint migration owner. SunPlayer owns the media-clock anchor and fallback if cubeb cannot recover.

### Qt Multimedia

**[Documented]** `QMediaDevices` monitors the available devices and system default and emits `audioOutputsChanged()` when either changes. `QAudioOutput` resolves a default-constructed device to the current system default when `setDevice()` is called. The public wrapper does not expose a transactional migration snapshot.

**[Source-confirmed]** The `QAudioOutput` implementation stores a concrete `QAudioDevice` and forwards changes to the platform output. Its constructor resolves the default immediately. There is no evidence in this wrapper for an automatic cross-platform “default changed, preserve media clock, reanchor” policy; that policy lives in player/backend layers. Source snapshot: `qt/qtmultimedia`, commit `6c4fe271aced360109765e1f89c1e3c6120e19ae`.

**Takeaway:** Qt’s device notifications may be useful diagnostics, but SunPlayer should avoid running a second default-device migration system beside cubeb.

### SDL as a contrast

**[Source-confirmed]** SDL 3 maintains physical and logical audio devices, conversion streams, default logical-device identities, zombie-device handling, and migration inside its own audio core. This is appropriate because SDL intends to be the application’s audio abstraction. It would be redundant architecture for SunPlayer, where cubeb already owns this layer. Source snapshot: commit `c15b6a14578bf6544cad834473d35bf2e38ff3fd`.

---

## 3. Cross-platform display migration comparison

| Concern             | Windows                                                              | macOS                                                                   | Wayland                                                    |
| ------------------- | -------------------------------------------------------------------- | ----------------------------------------------------------------------- | ---------------------------------------------------------- |
| Relevant display    | HWND-bound `DisplayInformation`; Advanced Color uses window center   | `NSWindow.screen`, effectively the screen containing most of the window | Compositor-selected preferred description for the surface  |
| Spanning rule       | DXGI swapchain: largest intersection; Advanced Color: center         | Most of window                                                          | No client-side output rule needed                          |
| Change signal       | `AdvancedColorInfoChanged`, window/screen movement, display topology | `didChangeScreen`, screen-parameters, profile notification              | `preferred_changed2`, output `image_description_changed`   |
| State retrieval     | Synchronous query of cached object/DXGI state                        | Synchronous properties on current `NSScreen`                            | Immutable description object, asynchronously becomes ready |
| Paused redraw       | Application responsibility                                           | Application or AppKit redisplay, depending on setup                     | Application must submit updated content if it adapts       |
| Final calibration   | DWM/Advanced Color                                                   | ColorSync/window server                                                 | Compositor                                                 |
| Missing HDR support | Conservative SDR                                                     | EDR headroom of 1.0                                                     | SDR/sRGB fallback                                          |

### Windows

#### Output association

**[Documented]** `IDXGISwapChain::GetContainingOutput` returns the output containing the majority of the window’s client area. `MonitorFromWindow` follows a greatest-intersection rule.

**[Documented]** Windows Advanced Color behaves differently while a window spans monitors: `AdvancedColorInfo` and `IDXGIOutput6` are abstracted to report the main display, defined as the display containing the window center. DWM handles conversion to the other displays.

**[Recommendation]** Do not add a custom greatest-intersection selector as the authoritative HDR target. Continue using the HWND-bound `DisplayInformation` for tone-mapping facts because it matches Windows’ Advanced Color abstraction. Use the swapchain’s current-output query only for swapchain support and diagnostics.

Largest intersection is not materially more correct for an ordinary windowed player because it can disagree with the display Windows uses for Advanced Color. It may be useful for UI labels, but not as a second competing tone-map authority.

#### Notifications and queries

**[Documented]** Microsoft explicitly recommends caching one `DisplayInformation` for the HWND. The object hooks the window’s movement and DPI messages, emits change events, and “always provides fresh data for your window.” This is direct evidence against rebuilding an asynchronous display-snapshot provider around it.

**[Recommendation]** The `AdvancedColorInfoChanged` callback should only mark the display target dirty. Query `GetAdvancedColorInfo()` on the render/control thread at the next safe boundary. `QWindow::screenChanged`, movement debounce, expose/activation after resume, and QRhi swapchain notifications should all feed the same dirty bit.

#### Swapchain recreation

**[Documented]** `CheckColorSpaceSupport` evaluates support against the swapchain’s current adapter output. Moving the window therefore changes the query result without requiring construction of another output-identity model.

**[Inference]** Merely moving a window between outputs does not require recreating a windowed swapchain. A device change requires recreation; a window size change requires resizing; a presentation format or color-space change may require QRhi-level recreation or `SetColorSpace1`. Output identity alone does not.

**[Recommendation]** Split the current “effective output changed” response:

```text
native output identity changed, same semantic target
    → diagnostics only

reference white / headroom changed
    → update libplacebo target and redraw

swapchain surface format / declared color space changed
    → reconfigure or recreate presentation resources

D3D device lost or adapter changed
    → advance graphics-device generation and rebuild GPU resources
```

If the QRhi D3D11 path can retain one FP16/scRGB swapchain across SDR and HDR modes, that is the simplest long-term Windows design. Confirm it experimentally before removing current recreation.

#### `IDXGIFactory1::IsCurrent`

**[Documented]** `IsCurrent()` reports only that an adapter may have appeared or disappeared and that cached adapter enumeration should be refreshed. It is not a general HDR, brightness, output, profile, or topology-change notification.

**[Recommendation]** Use `IsCurrent()` only inside code that actually caches a DXGI factory/output enumeration. Do not turn it into a global topology revision. With HWND-bound `DisplayInformation` and QRhi owning normal presentation, it can be deferred until adapter enumeration is needed.

#### Display identity

**[Recommendation]** Stable DisplayConfig identity is optional and local:

* Useful for diagnostics and correlating `adapterLuid + targetId` with an SDR-white fallback.
* Useful if QRhi does not expose the native current output needed by a backend.
* Not a cross-platform correctness identity.
* Not stable enough to outlive an arbitrary disconnect/reconnect without revalidation.

The semantic `DisplayTarget`, not the physical ID, determines whether a frame must be rerendered.

#### Reference white and peak

**[Documented]** Windows reports SDR white as a multiple of 80 nits. `DXGI_OUTPUT_DESC1::MaxLuminance` is a small-area peak; `MaxFullFrameLuminance` is the panel’s full-frame value. The values usually originate in EDID or an override.

**[Recommendation]** V1 needs:

```text
referenceWhiteNits
effectiveTargetPeakNits
presentation mode / surface encoding
optional minimum luminance
```

Use the reported current maximum as the initial tone-map ceiling when valid. Keep full-frame peak as a diagnostic or future APL-aware input, not as a second competing V1 target. A static renderer cannot fully exploit both small-window and full-frame limits without an explicit APL/display model.

Do not add “reported,” “potential,” “usable,” and “effective” peak as four independent correctness fields. Keep raw values in diagnostics and publish one effective value consumed by rendering.

**[Issue evidence]** Chromium’s current helper maps `MaxLuminance` to mastering maximum and labels its mapping of full-frame luminance to content-light fields as a guess. This is strong evidence that source provenance does not make the underlying display data exact.

#### Sleep, wake, HDR toggles, and profiles

**[Recommendation]** Treat wake, display hotplug, an HDR toggle, and a profile change as reasons to mark display state dirty and validate presentation resources. Do not add a global `Sleeping`, `Waking`, or `ReprobingDisplays` state unless a demonstrated platform bug requires it.

Windows Advanced Color and automatic color management should continue to own final calibration. SunPlayer’s responsibility is to declare the correct surface color space and choose a tone-map target; it should not reproduce the OS’s ICC/display pipeline.

### macOS

#### Display selection and notifications

**[Documented]** `NSWindow.screen` corresponds to the screen containing most of the window. `didChangeScreenNotification` is sent when portions of the window move onto or off a screen.

**[Documented]** When `displaysWhenScreenProfileChanges` is enabled, `didChangeScreenProfileNotification` is sent when most of the window moves to a differently profiled screen or when the current screen’s ColorSync profile changes.

**[Recommendation]** Observe:

```text
NSWindow.didChangeScreenNotification
NSWindow.didChangeScreenProfileNotification
NSApplication.didChangeScreenParametersNotification
```

All three should mark one `DisplayTarget` dirty. Query `window.screen` and its current EDR properties at the next render-safe boundary.

#### Current, potential, and reference headroom

**[Documented]**

* `maximumExtendedDynamicRangeColorComponentValue` is current available headroom and can change dynamically.
* `maximumPotentialExtendedDynamicRangeColorComponentValue` is the display’s potential and is fixed for a given `NSScreen` object.
* `maximumReferenceExtendedDynamicRangeColorComponentValue` is a special reference-rendering threshold and may be zero on ordinary displays.

**[Recommendation]**

* Use current headroom for the active tone-map target.
* Use potential headroom only for capability diagnostics or deciding that EDR is possible.
* Use reference headroom only for a future explicit reference-rendering mode.
* Do not combine the three into competing “peak” revisions.

Set `CAMetalLayer.wantsExtendedDynamicRangeContent` and the correct layer color space before relying on current EDR headroom. If current headroom initially reports `1.0`, render conservatively and requery after the layer has participated in an EDR frame.

#### Polling and paused frames

**[Documented]** Apple posts a screen-parameters notification when current EDR headroom changes. This supports event-driven requery rather than continuous polling.

**[Recommendation]** Do not poll current EDR headroom every frame by default. Requery on notifications, window movement, presentation reactivation, and a small number of settling retries.

A paused target-dependent frame must eventually be rerendered or re-tone-mapped when current EDR headroom changes. If SunPlayer later retains a truly scene-linear, display-independent intermediate and performs all target mapping in the final compositor, a cheaper recompose may replace the full libplacebo rerender.

#### ColorSync and external displays

**[Inference]** AirPlay, Sidecar, external display reconnects, profile changes, brightness changes, and sleep/wake can alter either `window.screen`, the `NSScreen` object, its profile, or current EDR headroom. The same requery flow covers them. There is no demonstrated need for separate AirPlay, Sidecar, and sleep state machines.

### Wayland

#### Surface state replaces display selection

**[Documented]** `color-management-v1` gives every surface a compositor-selected preferred image description. The preferred description can change as the surface moves, its role changes, or compositor policy changes. The protocol sends a 64-bit identity, and the client can fetch an immutable description when it does not already know that identity.

**[Recommendation]** Do not enumerate outputs and select one for color based on window geometry. Let the compositor choose the preferred description for the `wl_surface`.

Minimal state:

```text
current preferred-description identity
current ready semantic DisplayTarget
one pending description object, if any
```

When `preferred_changed2` arrives:

1. If the identity is already known, publish the corresponding target.
2. Otherwise request the new immutable description.
3. Publish only after its `ready` event.
4. Ignore or destroy it if a newer preferred identity has superseded it.
5. Retain the previous valid target until the new one is ready.

This is a genuine asynchronous stale-result boundary, but the protocol’s own identity is sufficient. SunPlayer does not need an additional provider generation, topology revision, and query revision.

#### Missing protocol support

**[Documented]** A surface without a color description is handled according to compositor policy; the protocol recommends treating such content as sRGB but does not guarantee identical behavior everywhere.

**[Recommendation]** On Wayland without usable `color-management-v1`:

```text
presentation = SDR
surface encoding = sRGB-compatible
headroom = 1.0
HDR output unavailable
```

Do not silently attempt HDR based on output EDID alone. Do not fall back to X11.

#### Deployment gaps

**[Source-confirmed]** mpv 0.41’s release includes multiple separate Wayland color-management fixes: protocol v2 support, luminance conversion, target colorspace, redrawn-frame handling, and HDR metadata corrections. This shows that protocol support is maturing but implementation details remain active and compositor-dependent.

---

## 4. Cross-platform audio migration comparison

| Platform/backend            | Default-device owner            | Typical migration                       | Position implication                                    | SunPlayer action                                     |
| --------------------------- | ------------------------------- | --------------------------------------- | ------------------------------------------------------- | -------------------------------------------------- |
| Windows/cubeb WASAPI        | Cubeb                           | Internal stop, close, setup, start      | Attempts monotonic position, latency/meaning may change | New epoch and anchor; app recreate only on failure |
| macOS/cubeb CoreAudio       | Cubeb                           | Asynchronous backend reinit             | No cross-device media-time guarantee                    | New epoch and anchor                               |
| PulseAudio / PipeWire-Pulse | Sound server, then cubeb client | Existing stream often moved server-side | Stream may continue, but route latency changes          | Detect discontinuity and reanchor if necessary     |
| Native PipeWire             | Application and graph           | Explicit node/graph policy              | More direct timing information, more policy             | Defer; not simpler for V1                          |
| Chromium native output      | Chromium audio service          | Close/recreate stream                   | Controller restores prior running state                 | Equivalent policy, but below player                |
| VLC native outputs          | VLC backend                     | Generic output restart request          | Player rebuilds audio output                            | More machinery because VLC owns native API         |

### Windows and cubeb WASAPI

**[Documented]** WASAPI disconnects a session for device removal, audio-service shutdown, endpoint format change, session changes, or exclusive-mode takeover. It closes the stream and invalidates its service interfaces. The client must release them and reopen.

**[Documented]** Default-device and session-disconnection notifications are asynchronous and can arrive in unpredictable order. Microsoft says a session-disconnection notification typically arrives before the default-device notification, but clients must not rely on that ordering.

**[Source-confirmed]** Cubeb WASAPI already accounts for this:

* Default endpoint notifications are debounced.
* Session disconnection sets a reconfiguration event.
* Reconfiguration occurs under backend synchronization.
* Existing clients are stopped and closed.
* The default endpoint and mix format are queried again.
* Clients and resampling state are rebuilt.
* The stream is restarted if it was active.

**[Source-confirmed]** Cubeb’s current implementation keeps a logical total of frames written across device reconfiguration and clamps the returned position against the previous result. This is an attempt to preserve monotonicity across migration.

**[Inference]** A monotonic cubeb frame count is still not a sufficient media clock because:

* The new endpoint can have different latency.
* The first new position can refer to a different native clock.
* Hold silence may have advanced cubeb frames but not media time.
* The stream may have spent an unknown period stopped or priming.
* A backend can preserve a logical count while changing the count’s physical presentation relationship.

**[Recommendation]** Keep a SunPlayer audio epoch and establish a new anchor after migration even when cubeb’s number did not reset.

#### Bluetooth

**[Issue evidence]** Firefox has had intermittent Windows Bluetooth disconnect failures where Windows changed to speakers but Firefox’s audio/video remained frozen until playback or the page was restarted. A 2026 report reproduced the issue in Firefox 146, with current Nightly/Beta later appearing fixed. An older open issue similarly reports no sound after disconnecting and reconnecting a Bluetooth speaker. These show that transparent backend recovery can occasionally wedge.

**[Recommendation]** This justifies a bounded fallback, not an always-parallel application migration system:

```text
cubeb change/reconfiguration hint
    → mark audio epoch discontinuous
    → wait for valid callbacks or cubeb error

valid callbacks return
    → reanchor and continue

cubeb reports error
    → destroy and recreate stream

demonstrated prolonged no-progress while user wants playback
    → one controlled recreate attempt
```

Avoid repeated immediate destroy/reopen loops. After one failed attempt, wait for a new device-list/default-device event or a slow bounded retry.

#### Format and sample-rate changes

**[Documented]** WASAPI reports endpoint format changes through session disconnection and requires reopening the native stream.

**[Source-confirmed]** Cubeb performs that native reconfiguration internally. SunPlayer does not need to rebuild libswresample merely because the hardware mix rate changed if it continues supplying the same cubeb stream format.

**[Issue evidence]** Firefox deliberately favors stable 44.1 or 48 kHz output because changing stream configuration can cause multi-second gaps on Bluetooth and HDMI devices.

**[Recommendation]** V1 should use a stable requested cubeb PCM format—preferably interleaved float at 48 kHz, with a conservative stable channel layout. Let libswresample convert decoded media into that format and let cubeb adapt from the requested format to the endpoint’s current mix format.

Rebuild SunPlayer’s swresampler only when SunPlayer’s requested stream format changes, not whenever the native endpoint changes internally.

### macOS and cubeb CoreAudio

**[Source-confirmed]** The current Rust CoreAudio backend listens for default input/output changes and device-state changes. For a stream following the default device, it invokes its device-changed callback and schedules asynchronous reinitialization. Explicitly selected devices follow different error behavior.

**[Inference]** CoreAudio reinitialization can change device latency, nominal sample rate, callback scheduling, or the native clock source. There is no public cubeb contract that promises a seamless media-timeline mapping across that change.

**[Recommendation]** Apply the same generic SunPlayer audio-epoch policy. Do not implement a macOS-specific AirPods state machine.

AirPods reconnects, Bluetooth profile changes, aggregate-device changes, and sleep/wake are backend-specific triggers for the same operation:

```text
old clock no longer trusted
→ freeze media time
→ wait for or initiate stream recovery
→ preroll
→ anchor new backend position to frozen media time
```

**[Issue evidence]** A Rust CoreAudio cubeb regression caused a crash when unplugging a non-default Bluetooth microphone during a WebRTC session. The failure occurred in device-switching code that was injecting silence. Although this was an input case, it demonstrates why device-reinitialization lifetimes belong in the backend and must be serialized.

### Linux through PulseAudio and PipeWire-Pulse

**[Documented]** Since PulseAudio 14, the server core normally moves existing streams when the default device changes. `module-switch-on-connect` no longer performs that move itself and is not loaded by default in some configurations because automatically making every new device the default can be too aggressive.

**[Documented]** PulseAudio can preserve active streams while profiles change when possible. Its default device also behaves partly as a fallback because stream-restore policy may remember a prior route.

**[Inference]** On PipeWire-Pulse, much of the same rerouting happens in the server. The cubeb stream may remain alive while its physical route and latency change. Therefore an endpoint change does not necessarily imply that SunPlayer should destroy its cubeb stream.

**[Recommendation]**

* Let Pulse/PipeWire-Pulse own normal sink movement.
* Treat cubeb error, callback interruption, or a material position/latency discontinuity as an audio-epoch boundary.
* Do not require the application to identify every Bluetooth profile transition.
* Do not adopt native PipeWire in V1 merely for migration.

Native PipeWire would give more direct graph and timing control, but it would also make SunPlayer responsible for node selection, graph negotiation, policy changes, reconnection, and clock interpretation. It is more control, not a simplification.

### A/V synchronization after migration

The required invariant is:

```text
For one audio epoch, every accepted backend position maps
monotonically to exactly one SunPlayer media time.
```

The invariant does not require backend position itself to start at zero.

A suitable anchor is:

```cpp
struct AudioClockAnchor {
    uint64_t epoch;
    uint64_t backendFrame;
    MediaTime mediaTime;
};
```

The published media time is:

```text
anchor.mediaTime
+ media frames presented since anchor.backendFrame
```

“Media frames presented” must come from the media/hold frame history, not directly from raw backend frames.

When an audio discontinuity begins:

1. Read and publish the last reliable media time.
2. Increment `audioStreamEpoch`.
3. Stop accepting position observations tagged with the prior epoch.
4. Keep `userWantsPlaying` unchanged.
5. Let cubeb recover, or rebuild the cubeb stream after an error.
6. Preroll enough PCM for the new stream.
7. Observe the new stream’s position/callback progression.
8. Create a new anchor mapping that position to the frozen media time.
9. Resume video scheduling only if the user still wants playback.

---

## 5. Known bugs and limitations in existing solutions

| Failure                                              | Evidence and root cause                                                                                                                                      | Mechanism that matters                                               |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------- |
| Cubeb WASAPI recovery race                           | A 2026 cubeb fix moved invalidated-device recovery onto the render thread because another thread could close/null clients while the render thread used them. | Serialize native object teardown; graphics/audio lifetime generation |
| Callback after stop                                  | Firefox historically received WASAPI callbacks after stream stop and had to avoid destroying under a lock.                                                   | Native callback lifetime discipline                                  |
| Bluetooth disconnect freezes playback                | Firefox reports show intermittent failure to rebind after Bluetooth output disappears.                                                                       | Error/no-progress fallback; one controlled stream rebuild            |
| Unexpected switch semantics for explicit devices     | Cubeb issue 694 separates “follow default” from explicitly selected devices.                                                                                 | Keep explicit device selection deferred; do not conflate policies    |
| Multi-second gaps after rate changes                 | Bluetooth and HDMI endpoints can take seconds to resynchronize after stream-rate changes.                                                                    | Stable requested PCM format                                          |
| Windows SDR washed out in HDR mode                   | mpv issue demonstrates wrong SDR/HDR target or colorspace behavior can persist.                                                                              | Semantic presentation target and colorspace update                   |
| Redrawn frames used wrong target colorspace          | mpv 0.41 includes fixes specifically for target colorspace on redrawn frames.                                                                                | Paused/manual redraw after target change                             |
| Display peak data is imperfect                       | Chromium labels part of its use of full-frame luminance a guess; Windows says data usually comes from EDID or overrides.                                     | Conservative fallback and useful diagnostics, not confidence graphs  |
| Windows notification ordering is unpredictable       | WASAPI default-device and disconnection callbacks are asynchronous and unordered.                                                                            | Events as hints; idempotent latest-state recovery                    |
| Wayland description may disappear or become obsolete | Protocol objects are immutable, may fail because an output disappeared, and are superseded by a new preferred identity.                                      | Use protocol identity and ready/lifetime rules                       |
| Pulse auto-switch can fight user policy              | `module-switch-on-connect` is considered too aggressive in some configurations.                                                                              | Let sound-server policy own default routing                          |
| Recovery completion can incorrectly resume           | Chromium deliberately restores only `kPlaying`, not merely “stream existed.”                                                                                 | Current user intent consulted at completion                          |

### Evidence not found

**[Inference]** I did not find strong production evidence that an ordinary desktop player needs all of the following simultaneously:

* A display-selection revision.
* A display-capability revision.
* A topology revision.
* A provider generation.
* Per-field query generations.
* Per-field confidence as a correctness mechanism.

Production code instead tends to requery, compare semantic state, and rebuild only the affected resources.

I also did not find convincing evidence that a player must poll macOS EDR headroom every frame, recreate a Windows swapchain whenever a window enters another monitor, or maintain a named global sleep/wake state.

---

## 6. Essential versus overengineered mechanisms

Classification:

1. Required for media correctness or native-resource safety.
2. Required to prevent persistently wrong output.
3. Helpful for transient polish.
4. Diagnostic sophistication.
5. Speculative or unnecessary complexity.

### Mechanism classification

| Mechanism                                     | Keep, simplify, defer, or remove                       | Why                                                                                                 | Production evidence                                                                                      |
| --------------------------------------------- | ------------------------------------------------------ | --------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| Playback generation                           | **Keep — class 1**                                     | Prevents stale decoded packets/frames after open, seek, fallback, or close                          | Existing SunPlayer invariant; standard player requirement                                                  |
| Graphics-device generation                    | **Keep — class 1**                                     | Prevents stale native/GPU resources crossing device loss or recreation                              | Cubeb’s analogous native-lifetime races show why lifetime boundaries must be strict.                     |
| Audio-stream epoch                            | **Keep, simplify — class 1**                           | Backend position cannot be trusted across endpoint/clock discontinuity without a new anchor         | Cubeb reinitializes internally; WASAPI position is logical/monotonic rather than a promised media clock. |
| Current semantic `DisplayTarget`              | **Keep — class 2**                                     | Rendering must eventually use correct reference white, headroom, and presentation mode              | VLC compares semantic display state; platform APIs provide current state.                                |
| Display-selection revision                    | **Remove**                                             | Native APIs already select the relevant target; output identity is not media identity               | Windows APIs even use different spanning rules.                                                          |
| Display-capability revision                   | **Collapse into `DisplayTarget`**                      | Semantic value comparison answers whether rendering changes                                         | VLC and Chromium use compact current values.                                                             |
| Provider generation                           | **Remove**                                             | Provider replacement is an implementation detail unless it owns resources visible to another thread | No production evidence found                                                                             |
| Topology revision                             | **Defer/local only**                                   | Useful only to invalidate cached enumeration; not needed by rendering                               | `IsCurrent()` only requests adapter re-enumeration.                                                      |
| Stable DisplayConfig/DXGI identity            | **Simplify**                                           | Useful for lookup/diagnostics, but not as a global generation                                       | Windows Advanced Color already follows the HWND.                                                         |
| Stale asynchronous-query rejection            | **Defer generally; keep for Wayland object readiness** | Windows/macOS queries are synchronous; Wayland already supplies identities                          | `preferred_changed2` and immutable descriptions.                                                         |
| Per-field provenance                          | **Simplify to aggregate source — class 4**             | Useful for bug reports; does not improve output by itself                                           | Peak data may still be an EDID guess.                                                                    |
| Per-field confidence                          | **Remove from V1 — class 4/5**                         | No stable objective confidence scale; rendering needs a selected fallback                           | Chromium’s explicit “guess” is handled as code policy, not confidence calculus.                          |
| Shared pause/buffering/recovery state machine | **Replace with intent plus blocking reasons**          | Avoids invalid state combinations and stale auto-resume                                             | Chromium remembers only whether it was playing.                                                          |
| Hold-silence ledger                           | **Keep as compact frame history — class 1**            | Required if backend position advances through silence while media time freezes                      | Firefox `FrameHistory`.                                                                                  |
| Paused-frame rerender                         | **Keep — class 2**                                     | Prevents a target-dependent paused image remaining persistently wrong                               | mpv fixes target colorspace on redrawn frames.                                                           |
| Explicit sleep/wake state                     | **Remove**                                             | Treat resume as display/audio dirty plus normal resource validation                                 | No production evidence for a global state                                                                |
| Coalescing/debounce                           | **Keep lightly — class 3**                             | Avoids bursts while preserving eventual convergence                                                 | Cubeb debounces endpoint changes; mpv uses semantic no-change checks.                                    |
| Multiple peak notions                         | **Simplify**                                           | Renderer needs one effective target; raw values belong in diagnostics                               | Windows distinguishes small-area and full-frame peak.                                                    |
| Explicit output-device selection              | **Defer**                                              | Different semantics and recovery policy; not required for V1                                        | Cubeb issue 694.                                                                                         |
| X11 backend/fallback                          | **Remove / unsupported**                               | Wayland is the intended Linux presentation architecture                                             | Project decision                                                                                         |

### Identities that can be collapsed

Use exactly these correctness identities:

```text
playbackGeneration
graphicsDeviceGeneration
audioStreamEpoch
```

`DisplayTarget` is a value, not an identity domain. Its semantic equality determines whether anything must change.

The Wayland preferred-description identity is platform protocol state, not a new global SunPlayer generation.

---

## 7. Simplest sound SunPlayer design

### Minimal shared state

```cpp
struct PlaybackState {
    uint64_t playbackGeneration;
    bool userWantsPlaying;
    BlockingReasons blockers;
};

struct GraphicsState {
    uint64_t graphicsDeviceGeneration;
    DisplayTarget displayTarget;
    bool displayTargetDirty;
};

struct AudioState {
    uint64_t streamEpoch;
    AudioSinkStatus sinkStatus;       // closed, priming, running
    AudioClockAnchor anchor;
    OutputFrameHistory frameHistory;
    std::optional<MediaTime> frozenMediaTime;
};
```

`BlockingReasons` can be a bitset:

```text
demux starvation
audio preroll
audio recovery
graphics recovery
```

Derived playback state:

```text
mayAdvanceTimeline =
    userWantsPlaying
    && no timeline-blocking reason
    && audio clock is anchored when audio exists
```

Do not encode the Cartesian product of pause, buffering, recovering, seeking, sleeping, display migration, and device migration as one enum.

### Minimal `DisplayTarget`

```cpp
struct DisplayTarget {
    PresentationMode mode;       // SDR, extended-linear, PQ10 if used
    float referenceWhiteNits;
    float effectivePeakNits;
    float minimumNits;           // optional/defaultable
    SurfaceColorEncoding encoding;
    TargetGamut gamut;           // only if renderer actually maps to it
};
```

Diagnostics may additionally retain:

```text
native display name/ID
reported max luminance
reported full-frame luminance
potential EDR headroom
data source: WinRT / DXGI / AppKit / Wayland / fallback
last update reason and timestamp
```

These diagnostic fields do not participate independently in invalidation.

### Minimal display adapter responsibilities

**Windows adapter**

* Own and cache the HWND-bound `DisplayInformation`.
* Register and unregister `AdvancedColorInfoChanged`.
* Accept Qt screen/move/presentation events as dirty hints.
* Query current advanced color information synchronously.
* Query swapchain color-space support when presentation configuration may change.
* Optionally expose native output identity for diagnostics.
* Report device-loss separately to graphics recovery.

**macOS adapter**

* Observe window screen/profile and application screen-parameter notifications.
* Query the current `NSScreen`.
* Configure the Metal layer’s EDR and color-space declaration.
* Publish current/reference/potential headroom, with only current used by normal rendering.

**Wayland adapter**

* Own `wp_color_management_surface_v1` and surface feedback.
* Track the compositor’s preferred identity.
* Resolve immutable descriptions.
* Publish the latest ready semantic target.
* Fall back to SDR when the protocol is unavailable or fails.

No adapter should expose a generalized provider/plugin interface.

### Minimal audio adapter responsibilities

The cubeb wrapper should:

* Open the system-default output.
* Register cubeb state and device-change callbacks.
* Feed preallocated PCM.
* Expose backend position and latency observations tagged with the current SunPlayer epoch.
* Report callback progress, error, and device-change hints.
* Serialize stream stop/destruction against callback lifetime.
* Perform one controlled application-level recreation when cubeb reports an unrecoverable error.

It should not:

* Implement its own WASAPI endpoint enumerator.
* Implement CoreAudio device listeners in parallel with cubeb.
* Move Pulse streams itself.
* Rebuild libswresample based on native endpoint mix-rate changes.
* Select explicit output devices in V1.

### Platform responsibility

| Concern                            | SunPlayer                                    | Library/backend                              | OS/compositor                          |
| ---------------------------------- | ------------------------------------------ | -------------------------------------------- | -------------------------------------- |
| Media open, seek, decode identity  | Playback generation and queue invalidation | FFmpeg decoding/demux                        | —                                      |
| Video tone-map target              | Choose semantic target and rerender        | libplacebo executes mapping                  | Supplies capabilities/headroom         |
| Swapchain/native-resource lifetime | Graphics generation and safe rebuild       | QRhi/backend creates resources               | Driver/DWM/window server presents      |
| Final display calibration          | Declare correct surface encoding           | QRhi/native API transports it                | DWM, ColorSync, Wayland compositor     |
| Active display selection           | Ask platform adapter for current target    | Native window/surface API                    | Chooses main/preferred output          |
| Display-event ordering             | Coalesce and requery latest                | Adapter translates callbacks                 | Emits non-transactional hints          |
| Default audio endpoint migration   | Preserve clock semantics and intent        | Cubeb reinitializes or sound server reroutes | WASAPI/CoreAudio/Pulse policy          |
| Audio native format adaptation     | Stable requested PCM format                | Cubeb backend resamples/negotiates           | Endpoint exposes mix format            |
| A/V clock                          | Epoch, anchor, frame-history mapping       | Cubeb supplies position/latency              | Native audio clock drives presentation |
| Underrun silence                   | Decide media versus hold frames            | Cubeb consumes PCM                           | Endpoint presents frames               |
| No output device                   | Freeze timeline, remain recoverable        | Cubeb reports failure/change                 | Device system later reports endpoint   |
| Explicit device selection          | Deferred                                   | Cubeb supports IDs/preferences               | System exposes devices                 |
| X11                                | Unsupported                                | —                                            | —                                      |

### Unavailable information

**[Recommendation] Display query failure**

1. Retain the last valid target for one bounded retry if the native display still appears present.
2. If the failure persists, fall back to SDR with no HDR headroom.
3. Do not invent a high target peak.
4. Record the failure source in diagnostics.
5. Rerender the paused frame when a valid target returns.

**[Recommendation] Audio device unavailable**

1. Freeze at the last reliable media time.
2. Set the audio-recovery blocking reason.
3. Keep decode queues bounded; do not accumulate unbounded decoded PCM.
4. Attempt creation on a cubeb device/default change notification.
5. Optionally perform a slow retry to survive missed platform events.
6. Resume only if `userWantsPlaying` is true.
7. Never spin a rapid reopen loop.

---

## 8. Suggested deletions or simplifications

### Delete

* Cross-platform display-selection revision.
* Separate display-capability revision.
* Provider generation.
* Global topology revision.
* Per-field revision numbers.
* Per-field confidence values.
* Named global sleep/wake recovery state.
* Mandatory async display-probe pipeline.
* Application-owned WASAPI/CoreAudio/Pulse endpoint migration.
* X11 backend and X11 fallback.

### Collapse

```text
reported peak
potential peak
usable peak
effective peak
```

into:

```text
effectivePeakNits           // consumed by renderer
raw reported values         // diagnostics only
```

Collapse:

```text
playing
paused
buffering
audio recovering
graphics recovering
device absent
```

into:

```text
userWantsPlaying
blockingReasons
local audio/graphics resource status
```

Collapse all display revisions into semantic equality of `DisplayTarget`.

### Retain

* Playback generation.
* Graphics-device generation.
* Audio-stream epoch.
* Current semantic display target.
* Paused-frame rerender/recompose.
* Compact media-versus-hold output history.
* Debounced movement hints and event coalescing.
* Strong stream/callback teardown ordering.
* Separate user intent.

### Defer

* Explicit audio-device selection.
* Native PipeWire.
* APL-aware use of full-frame display luminance.
* Per-monitor calibration overrides.
* Rich provenance/confidence UI.
* Continuous macOS EDR polling.
* DisplayConfig identity beyond diagnostics/fallback lookup.
* `IDXGIFactory1::IsCurrent` unless SunPlayer caches adapter enumeration.
* Platform-specific Bluetooth state machines.

### Suggested project policy text

For `AGENTS.md` or the project plan:

```text
Linux presentation targets Wayland only. X11 is unsupported; do not add
X11-specific backends, abstractions, fallbacks, or tests without an explicit
project-level decision to change this policy.
```

---

## 9. Event flows and pseudocode

### Display change

```cpp
void onNativeDisplayHint(DisplayHint hint)
{
    diagnostics.lastDisplayHint = hint;
    displayTargetDirty = true;
    requestRenderBoundaryWork();       // naturally coalesces repeated events
}

void processDisplayTargetIfDirty()
{
    if (!displayTargetDirty)
        return;

    displayTargetDirty = false;

    QueryResult result = platformDisplay.queryCurrentTarget(window);

    if (!result) {
        scheduleBoundedDisplayRetry();
        if (displayQueryHasPersistentlyFailed())
            publishDisplayTarget(conservativeSdrTarget());
        return;
    }

    DisplayTarget next = normalizeDisplayTarget(result);

    if (next == graphics.displayTarget)
        return;

    DisplayTarget previous = graphics.displayTarget;
    graphics.displayTarget = next;

    bool presentationChanged =
        previous.mode != next.mode ||
        previous.encoding != next.encoding;

    bool renderingChanged =
        previous.referenceWhiteNits != next.referenceWhiteNits ||
        previous.effectivePeakNits != next.effectivePeakNits ||
        previous.minimumNits != next.minimumNits ||
        previous.gamut != next.gamut;

    if (presentationChanged &&
        !presentationBackend.canReconfigureInPlace(previous, next)) {
        recreateSwapchain();
    } else if (presentationChanged) {
        presentationBackend.applyTarget(next);
    }

    if (renderingChanged || presentationChanged)
        requestCurrentFrameRedraw();
}
```

### Wayland preferred description

```cpp
void onPreferredChanged(uint64_t identity)
{
    currentPreferredIdentity = identity;

    if (auto known = targetCache.find(identity)) {
        publishDisplayTarget(*known);
        return;
    }

    requestPreferredDescription(identity);
}

void onDescriptionReady(uint64_t requestedIdentity,
                        NativeDescription description)
{
    if (requestedIdentity != currentPreferredIdentity)
        return; // superseded

    DisplayTarget target = convert(description);
    targetCache.insert(requestedIdentity, target);
    publishDisplayTarget(target);
}
```

The protocol identity replaces a custom asynchronous query revision.

### Audio device discontinuity

```cpp
void onCubebDeviceChangeHint()
{
    beginAudioDiscontinuity(AudioReason::DeviceChanged);
    // Do not immediately destroy: cubeb may already be reinitializing.
}

void onCubebStateError()
{
    beginAudioDiscontinuity(AudioReason::BackendError);
    recreateDefaultCubebStream();
}

void beginAudioDiscontinuity(AudioReason reason)
{
    if (!audio.frozenMediaTime)
        audio.frozenMediaTime = currentReliableAudioMediaTime();

    ++audio.streamEpoch;
    audio.sinkStatus = AudioSinkStatus::Priming;
    playback.blockers.add(Blocker::AudioRecovery);

    audio.anchor = {};
    audio.frameHistory.clear();

    diagnostics.lastAudioRecoveryReason = reason;
}
```

### Callback after migration

```cpp
long dataCallback(float *output, long requestedFrames)
{
    uint64_t callbackEpoch = audio.streamEpoch;

    OutputChunk chunk = pcmQueue.read(requestedFrames);

    if (chunk.mediaFrames > 0) {
        writeMedia(output, chunk);
        audio.frameHistory.append(
            callbackEpoch,
            requestedFrames,
            chunk.mediaFrames);
    } else {
        writeSilence(output, requestedFrames);
        audio.frameHistory.append(
            callbackEpoch,
            requestedFrames,
            0);                         // backend advances, media does not
    }

    signalCallbackProgress(callbackEpoch);
    return requestedFrames;
}
```

### Establishing the new anchor

```cpp
void maybeFinishAudioRecovery()
{
    if (audio.sinkStatus != AudioSinkStatus::Priming)
        return;

    if (!enoughMediaPreroll() || !hasReliableBackendPosition())
        return;

    uint64_t backendPosition = cubebPosition();
    MediaTime mediaPosition =
        audio.frozenMediaTime.value_or(currentDecodedAudioTime());

    audio.anchor = {
        .epoch = audio.streamEpoch,
        .backendFrame = backendPosition,
        .mediaTime = mediaPosition,
    };

    audio.frozenMediaTime.reset();
    audio.sinkStatus = AudioSinkStatus::Running;
    playback.blockers.remove(Blocker::AudioRecovery);

    if (playback.userWantsPlaying)
        ensureCubebStarted();
    else
        ensureCubebStoppedOrPaused();
}
```

### Published audio clock

```cpp
std::optional<MediaTime> audioMediaTime()
{
    if (audio.frozenMediaTime)
        return audio.frozenMediaTime;

    if (!audio.anchor.valid())
        return std::nullopt;

    uint64_t backendPosition = cubebPosition();

    auto mediaFrames = audio.frameHistory.mediaFramesBetween(
        audio.anchor.backendFrame,
        backendPosition,
        audio.streamEpoch);

    if (!mediaFrames)
        return std::nullopt;

    return audio.anchor.mediaTime +
           framesToTime(*mediaFrames, requestedStreamRate);
}
```

### Recovery fallback

```text
device-change hint
    │
    ├─ cubeb resumes valid callbacks
    │      → preroll → new anchor → continue
    │
    ├─ cubeb reports error
    │      → recreate default stream once
    │
    └─ prolonged no-progress confirmed by integration testing
           → one controlled recreate
           → on failure, wait for a new device event
```

No nested recovery loop and no automatic resume without consulting current intent.

---

## 10. Minimal high-value test matrix

| Scenario                                            | Deterministic shared-policy test |  Real backend integration | Manual hardware |    Physical measurement |
| --------------------------------------------------- | -------------------------------: | ------------------------: | --------------: | ----------------------: |
| Display target A → B                                |                              Yes |                       Yes |             Yes |                Optional |
| Rapid A → B → A                                     |                              Yes |                       Yes |             Yes |                      No |
| Delayed stale display result                        |   Only for Wayland/async adapter |             Protocol mock |              No |                      No |
| Reference-white change while paused                 |                              Yes |                       Yes |             Yes |    Colorimeter valuable |
| HDR toggle                                          |                  Semantic policy | Windows/macOS integration |             Yes |             Colorimeter |
| Same display semantics, new native ID               |                              Yes |                  Optional | Dock/MST manual |                      No |
| Swapchain color-space mismatch                      |                   Yes for policy |       Windows integration |             Yes | HDR capture/colorimeter |
| Default audio-device switch                         |              Yes with cubeb shim |                       Yes |   USB/Bluetooth |         Loopback useful |
| Device disappears, replacement exists               |                              Yes |                       Yes |             Yes |    A/V sync measurement |
| No output device exists                             |                              Yes |                       Yes |             Yes |                      No |
| Bluetooth reconnect/profile change                  |               Limited simulation |        Yes where feasible |       Essential |         A/V sync useful |
| User pauses during recovery                         |                        Essential |                       Yes |             Yes |                      No |
| Backend position resets                             |                        Essential |              Fake backend |    Not required |                      No |
| Backend position stays monotonic but offset changes |                        Essential |              Fake backend |          Useful |                A/V sync |
| Hold silence                                        |                        Essential |                Cubeb shim |        Optional |          Audio loopback |
| Recovery during seek                                |                        Essential |                       Yes |             Yes |                      No |
| Recovery during close                               |                        Essential |          Sanitizer/stress |             Yes |                      No |
| Device loss during callback                         |           Thread/lifetime stress |                       Yes |      USB unplug |                      No |
| Wayland preferred description superseded            |                        Essential |  Mock compositor/protocol |             Yes |    Colorimeter optional |

### Highest-value deterministic regressions

1. **Pause during recovery never resumes unexpectedly.**

```text
playing → recovery starts → user pauses → recovery completes
expected: paused
```

2. **Old audio observations cannot affect a new epoch.**

```text
epoch 4 position callback arrives after epoch 5 begins
expected: ignored
```

3. **Hold silence does not advance media time.**

```text
backend position advances by 4,800 frames
all 4,800 frames are hold silence
expected media advance: zero
```

4. **Monotonic backend position can still be reanchored.**

```text
old backend position = 100,000
new backend first position = 100,512
frozen media time = 12.0 s
expected new media time starts at 12.0 s, not inferred from old slope
```

5. **A final display target wins after rapid movement.**

```text
A event → delayed query
B event → current query
A result arrives late
expected final target: B
```

On Windows/macOS this test is unnecessary if queries remain synchronous. On Wayland, use the compositor description identity.

6. **Paused-frame target change schedules exactly one eventual redraw.**

Repeated identical target events must not create a swapchain-recreation or redraw storm.

7. **Shutdown wins over recovery.**

Once closing begins, no device event may create or restart another stream.

### Real integration tests

Use a cubeb test backend or thin dependency-injection shim that can:

* Reset or offset its reported position.
* Delay callbacks.
* Report a device-change event.
* Report an error.
* Change latency.
* Continue consuming silence.
* Reject stream creation because no device exists.

Do not mock WASAPI, CoreAudio, or Pulse object-by-object.

For display code, test the platform adapter and semantic policy separately:

* Adapter integration confirms native observations are converted correctly.
* Shared deterministic tests confirm comparison, redraw, and presentation-resource policy.

### Manual acceptance scenarios

**Windows**

* HDR monitor plus SDR monitor.
* Two HDR displays with different SDR-white settings.
* Move slowly across the boundary and stop on each side.
* Drag rapidly across and back.
* Change the Windows SDR-content-brightness slider.
* Toggle HDR.
* Sleep/wake.
* Disconnect and reconnect MST/USB-C dock.
* Disconnect Bluetooth A2DP during playback.
* Switch from A2DP to internal speakers and back.
* Temporarily disable every output endpoint.

**macOS**

* Internal EDR display plus external SDR/HDR display.
* Brightness changes while paused.
* ColorSync profile change.
* Sleep/wake.
* AirPlay or Sidecar connection/disconnection.
* AirPods disconnect/reconnect and profile transition.

**Wayland**

* A compositor implementing `color-management-v1` version 2.
* Movement between outputs with distinct descriptions.
* Description replacement before an earlier description becomes ready.
* Output removal while a description is pending.
* Compositor without color management, confirming explicit SDR fallback.

### Physical measurements

Use a colorimeter or photometer for:

* `1.0` reference-white luminance.
* Peak clipping after target changes.
* Paused-frame update after SDR-white/EDR changes.
* HDR/SDR surface colorspace correctness.

Use audio loopback or a high-speed camera for:

* Lip-sync immediately before and after device migration.
* Clock jumps after Bluetooth reconnect.
* Time from device return to first correctly synchronized frame.

---

## 11. Unresolved experiments

### Windows

**[Experiment] Stable FP16 swapchain**

Determine whether the QRhi D3D11 path can retain a single FP16 extended-linear swapchain while:

* Moving between SDR and HDR displays.
* Toggling Advanced Color.
* Changing SDR white.
* Changing the swapchain’s declared color space in place.

If it can, delete swapchain recreation on normal display-target changes.

**[Experiment] Selection disagreement**

Place a window so its center is on display A while most of its client area is on display B. Compare:

* HWND-bound `DisplayInformation`.
* `MonitorFromWindow`.
* `GetContainingOutput`.
* QRhi swapchain HDR information.
* Actual DWM output behavior.

Use the HWND-bound Advanced Color result as the initial expected authority.

**[Experiment] Peak reliability**

Record WinRT and DXGI peak/full-frame values for known monitors and compare them with:

* EDID/DisplayID data.
* Windows Advanced Display UI.
* Measured clipping.
* Manufacturer DisplayHDR data.

This decides whether a small conservative clamp is warranted. It does not justify a general confidence framework.

### macOS

**[Experiment] EDR activation timing**

After setting `wantsExtendedDynamicRangeContent`, determine when current EDR headroom becomes greater than 1.0:

* Before first drawable.
* After first EDR frame.
* After a screen-parameters notification.
* Only while visible/unoccluded.

**[Experiment] Paused EDR update**

Change brightness/headroom while paused and determine whether:

* Re-presenting the existing extended-linear texture is sufficient.
* Final composition must be rerun.
* The libplacebo target-dependent frame must be rerendered.

**[Experiment] External routes**

Test AirPlay, Sidecar, clamshell mode, and external HDR reconnect. Confirm that ordinary screen/profile/parameter notifications converge without named route states.

### Wayland

**[Experiment] Deployment matrix**

For each supported compositor/version, record:

* `color-management-v1` version.
* Parametric-description support.
* Preferred-description contents.
* HDR presentation availability.
* Behavior when an output disappears.
* Behavior when a surface spans outputs.
* Whether the compositor performs expected SDR/HDR mapping.

Keep this as a compatibility table, not runtime architecture.

### Audio

**[Experiment] Cubeb position around real migration**

For WASAPI, CoreAudio, and Pulse:

1. Log cubeb position and latency before the change.
2. Trigger a default-device migration.
3. Log device callback, state callback, data callbacks, position, and latency.
4. Determine whether position resets, jumps, stalls, or stays monotonic.
5. Verify that SunPlayer’s epoch anchor removes all visible media-time jumps.

**[Experiment] Application fallback threshold**

First ship or test with cubeb-owned recovery and error-triggered recreation. Add a no-progress watchdog only if real devices can remain indefinitely wedged without emitting an error.

A candidate experimental policy is:

```text
if user wants playback
and no callback/position progress occurs
for max(2 seconds, 8 × reported latency)
after a device-change hint:
    perform one application-level stream recreation
```

The exact threshold must be validated against slow Bluetooth endpoints. It should not be hard-coded as a universal platform truth.

**[Experiment] Stable PCM policy**

Compare fixed 48 kHz float stereo against dynamically chosen channel/rate configurations for:

* Bluetooth.
* HDMI receivers.
* USB DACs.
* Surround outputs.

For V1, continuity should win over format churn unless multichannel output is already a firm requirement.

---

## 12. Primary-source bibliography

### mpv and libplacebo

* mpv Windows common output, `video/out/w32_common.c`, commit `1d15686142fd5d53c954aab7526cedab05ef9dc3`.
* mpv audio-output core, `audio/out/ao.c`, same commit.
* mpv player audio reconstruction, `player/audio.c`, same commit.
* mpv 0.41.0 release notes, including Wayland color-management and redrawn-frame fixes.
* mpv target-colorspace documentation.
* libplacebo renderer source snapshot, commit `4d82c6898551068d4ae6a6b5538efcddc2c7cf64`.

### VLC

* VLC Windows MMDevice output, `modules/audio_output/mmdevice.c`, commit `e6779c9a91861e73867cfb7bb920234e1157c0c6`.
* VLC default-device notification/restart path, same file and commit.
* VLC D3D11 video output, `modules/video_output/win32/direct3d11.cpp`, same commit.

### Chromium

* Chromium HDR metadata helper, `ui/gl/hdr_metadata_helper_win.cc`, commit `53d828797251d2140f40aed354ffce4218d23269`.
* Chromium audio output controller and recreation flow, `services/audio/output_controller.cc`, same commit.
* Chromium device-change state restoration, same file.

### Firefox and cubeb

* Firefox `AudioStream.cpp` frame-history and clock mapping.
* Firefox cubeb destruction/callback lifetime handling.
* Cubeb WASAPI, `src/cubeb_wasapi.cpp`, commit `ef47ae581df7c2f76058d554b3edde17f9ee7cba`.
* Cubeb 2026 render-thread device-invalidated recovery fix, same commit line.
* Cubeb Rust CoreAudio backend, commit `840f69c1b3f95accd582555e10abcef8ee67514b`.
* Cubeb Rust Pulse backend, commit `cbcf979feec50c8f024cfa903d492bbe2a2bddf2`.
* Cubeb issue 694, default-following versus explicitly selected device policy.

### Qt and SDL

* Qt Multimedia `QMediaDevices` 6.11.1 documentation.
* Qt Multimedia `QAudioOutput` implementation, commit `6c4fe271aced360109765e1f89c1e3c6120e19ae`.
* SDL audio core, `src/audio/SDL_audio.c`, commit `c15b6a14578bf6544cad834473d35bf2e38ff3fd`.

### Windows platform documentation

* `IDisplayInformationStaticsInterop::GetForWindow`.
* DirectX with Advanced Color and spanning-window behavior.
* `IDXGISwapChain::GetContainingOutput`.
* `IDXGISwapChain3::CheckColorSpaceSupport`.
* `IDXGIFactory1::IsCurrent`.
* `DXGI_OUTPUT_DESC1`.
* `DISPLAYCONFIG_SDR_WHITE_LEVEL`.
* WASAPI `OnSessionDisconnected`.
* WASAPI stream-routing notification ordering.
* Recovering from invalid-device errors.

### Apple platform documentation

* Current EDR headroom.
* Potential EDR headroom.
* Reference EDR headroom.
* Window screen-change notification.
* Window screen-profile notification.

### Wayland and Linux audio documentation

* Wayland `color-management-v1`, including immutable descriptions and `preferred_changed2`.
* PulseAudio modules and default-device stream movement.
* PulseAudio profiles and fallback/default semantics.

### Issue evidence

* Firefox Bluetooth output disconnect freeze, Bug 2009171.
* Firefox Bluetooth reconnect/no-sound bug, Bug 1678846.
* Firefox stable sample-rate rationale, Bug 1675878.
* Firefox/cubeb CoreAudio Bluetooth unplug crash, Bug 1570080.
* mpv Windows SDR in HDR issue 14648.

## Final recommendation

SunPlayer should converge on this design:

```text
Display:
    native events are hints
    → query current semantic target
    → compare
    → reconfigure presentation only if its format/encoding changed
    → rerender or recompose if tone-map state changed

Audio:
    cubeb owns normal default-endpoint migration
    → SunPlayer marks a new audio epoch
    → freezes media time
    → accepts recovery or recreates after error
    → prerolls
    → establishes a new backend-position/media-time anchor
    → resumes according to current user intent

Strict ordering:
    decoded media generation
    GPU/native-resource lifetime
    audio stream epoch and clock anchor
    Wayland pending-description lifetime

Eventual consistency:
    display selection and capabilities
    topology and diagnostic identity
    display provenance
    audio device labels and route diagnostics
```

This is sufficient to converge reliably without making harmless transient display state globally atomic.
