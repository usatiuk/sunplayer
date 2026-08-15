# Linux system-cubeb audio

Status: Complete

## Goal

Turn the native-Wayland Linux path from video-only presentation into a usable
synchronized local-file player by running the existing production cubeb sink
against Ubuntu's system audio service.

Completion means:

* the same `CubebAudioSink` implementation serves Windows and Linux;
* Linux lets system cubeb select its backend and follows the system-default
  output route;
* presented cubeb frames become the existing audio master clock without a
  second media operation or Linux playback policy;
* the device-backed sink and registered audio-first application A/V scenario
  pass through WSLg's PulseAudio-compatible server; and
* unavailable native-hardware recovery evidence remains explicit instead of
  being approximated by new fallback machinery.

The matching package, source, header, and WSLg runtime evidence is recorded in
[the system-cubeb research note](../../research/2026-08-02-wslg-system-cubeb-audio.md).

## Grounded baseline

At commit `7c7d30b`:

* Ubuntu already provides and CMake discovers `cubeb::cubeb`.
* The shared audio decode, resampling, bounded PCM queue, output ledger,
  playback clock, buffering state, volume/mute behavior, and diagnostics are
  built and tested on Linux.
* `CubebAudioSink` itself is compiled only on Windows.
* Its control thread unconditionally includes COM, initializes an MTA, and
  requests cubeb's `wasapi` backend by name.
* `MediaSession::createAudioSink()` returns no physical sink outside Windows.
* The device-backed sink test and audio-bearing application smoke tests are
  registered only on Windows.
* Ubuntu's cubeb header omits the output-irrelevant
  `cubeb_stream_params::input_params` member used by the newer Windows pin.
* A direct public-API probe proves the installed cubeb selects `pulse`, opens
  the WSLg default route, starts its callback, advances playback position, and
  reports latency.

No new audio interface is required. `AudioSink` is already the playback edge,
and `CubebAudioSink` is already the physical implementation.

## Fixed design and invariants

### One shared physical sink

Keep one `CubebAudioSink`. The platform difference is limited to control-
thread initialization:

```text
Windows control thread
    initialize COM MTA
    cubeb_init(..., "wasapi")

Linux control thread
    cubeb_init(..., nullptr)

both
    default output device
    existing stream/queue/ledger/clock state machine
```

Do not add `LinuxAudioSink`, an audio-backend factory, a PulseAudio client, a
PipeWire client, or a general platform hook. Cubeb is the accepted portable
physical-output boundary.

The dedicated control thread remains shared. Linux does not require COM, but
retaining one owner for cubeb context, stream lifecycle, position queries, and
destruction preserves the already-tested ordering and avoids introducing a
second concurrency model.

### System backend and route policy

Windows continues to request `wasapi` by name because that pinned dependency
and packaging contract is deliberate. Linux passes a null backend name and
records `cubeb_get_backend_id()` in the existing diagnostic snapshot.

SunPlayer does not force `pulse`. Ubuntu cubeb's own default order selects Pulse
when available, which covers a real PulseAudio server, PipeWire-Pulse, and
WSLg. A different selected backend is observable; it is not silently rewritten
into application policy.

The output device remains null in `cubeb_stream_init()`. Cubeb and the sound
server own ordinary default-route migration inside one cubeb stream epoch.
This is the public cubeb contract for following the system default. Device-
collection notification remains an optional diagnostic fact, not a default-
change or recovery-completion signal: Ubuntu's Pulse backend updates its own
default-sink information for a server change but does not invoke the
collection callback for that event.

Do not poll device enumeration or recreate the stream merely to manufacture a
switch notification. WSLg can prove default-route opening but not a native
Pulse/PipeWire device move. Native acceptance must change the system default
while playback runs and observe that cubeb/platform routing follows it with
one surviving SunPlayer output epoch. A demonstrated backend error or persistent
no-progress condition belongs to the separately bounded stream-recreation
slice already allowed by ADR 0016.

### Playback and failure behavior

The current playback contracts remain unchanged:

* 48 kHz stereo native-endian float32 is the requested physical format.
* The real-time callback performs no allocation, blocking, Qt calls, logging,
  decode, or recovery.
* Cubeb's presented position maps through the existing bounded output ledger
  to normalized media time.
* Before the first valid audio observation, the existing provisional monotonic
  clock remains in effect.
* A video-only source continues to use the monotonic clock and need not open a
  cubeb stream.
* A source with audio and an unavailable context/device becomes the existing
  visible session error. It does not silently discard audio and continue as a
  video-only source.
* Short underruns remain hold silence; sustained holds enter the existing
  `Buffering` state without changing user play intent.
* A cubeb callback or stream failure remains terminal for the current
  generation until the separately deferred output-epoch replacement work is
  implemented.
* `CUBEB_STATE_DRAINED` remains the authoritative terminal boundary. Generalize
  the existing WASAPI-specific explanatory comment only; do not change the
  behavior before a Linux test demonstrates a defect.

### Header compatibility

Remove `.input_params` from the `cubeb_stream_params` designated initializer.
The field is absent from Ubuntu's snapshot, irrelevant to the output-only
stream, and value-initialized when present in the newer Windows aggregate.

Do not add cubeb-version detection, configure probes, a compatibility struct,
or preprocessor logic for this optional trailing field.

## Implementation slices

### 1. Compile the shared sink on Linux

1. Guard `<objbase.h>`, `CoInitializeEx`, and `CoUninitialize` with the Windows
   platform condition.
2. Keep COM failure as the existing Windows-only initialization result.
3. Select the cubeb initialization argument at compile time: `"wasapi"` on
   Windows, null on Linux.
4. Remove the incompatible `input_params` designated initializer.
5. Add `CubebAudioSink.cpp/.h` and `cubeb::cubeb` to `sunplayer_audio` for both
   currently supported platforms instead of maintaining duplicate source
   lists.
6. Include and instantiate `CubebAudioSink` from production `MediaSession` on
   both platforms. Do not change injected/controlled-sink constructors.

Exit: the Linux application links the system cubeb library and an audio-
bearing source opens a physical sink rather than silently having no sink.

### 2. Reuse the device-backed tests

1. Build `tst_CubebAudioSink` on Windows and Linux.
2. Keep its behavioral assertions for default route, bounded preroll, start,
   presented position, pause, drain, generation reset, and teardown.
3. Make only backend-specific facts platform-shaped. Windows still requires
   `wasapi`; Linux requires a nonempty selected backend and records the actual
   value. WSLg delivery evidence should show `pulse` without hard-coding Pulse
   as application behavior.
4. Keep device-notification availability optional on Linux because the public
   diagnostics model already represents that capability and it is not a
   portable default-switch completion signal.
5. Keep the Linux dependency test at the public link/ABI boundary. Ubuntu's
   cubeb snapshot does not expose backend enumeration without initializing a
   context, so let the physical sink test prove and diagnose the selected
   backend instead of adding binary inspection or a compatibility shim.

Do not weaken a failed context, stream, position, drain, or callback result
into a skipped test. The device-backed test is explicitly environment-
dependent and should fail honestly when its required audio service is absent.

Exit: the same public sink behavior passes through WASAPI and the WSLg Pulse
server with only capability-shaped assertions differing.

### 3. Enable real Linux A/V application scenarios

1. Register `application-audio-first-playback` on Linux with the existing
   lossless audio-first fixture. It must observe:
   * two distinct video revisions at the production swapchain;
   * a valid, advancing cubeb presentation snapshot;
   * nonzero presented audio frames; and
   * continued media-position progress.
2. Run the audio-bearing `application-fullscreen` scenario explicitly on
   Linux, but do not make it a default cubeb gate while its unrelated WSLg
   cursor-state convergence remains intermittently unstable.
   Preserve its existing native F11, Escape, Space, double-click, cursor, and
   restoration checks. Add only a bounded assertion that the audio clock is
   valid and advances across the complete scenario; do not create per-window-
   state audio epochs.
3. Confirm the Player's existing diagnostics report backend `pulse`, the
   audio clock, PCM occupancy, and underruns in WSLg.
4. On native hardware, run an interactive representative file to verify
   audible output, pause, seek, mute, volume, replay, and close behavior. Keep
   this as native acceptance evidence rather than a WSLg implementation gate.

Exit: the installed native-Wayland application advances synchronized video and
real WSLg-routed audio through the registered audio-first scenario. The
fullscreen scenario is an explicit observation owned by the broader Wayland
lifecycle checklist, not an audio-slice completion gate.

### 4. Record evidence and update project truth

After implementation succeeds:

* add a dated Linux-port evidence note with dependency version, selected cubeb
  backend, WSLg/Pulse environment, commands, outcomes, and known gaps;
* update AUDIO-01 through AUDIO-04 and AUDIO-08 in the persistent Linux port
  checklist according to actual evidence;
* update root `PLAN.md` only for behavior that has passed;
* update the audio, playback, UI, build, diagnostics, and testing subsystem
  documentation where their current Windows-only statements change;
* update ADR 0011's implementation-status wording without changing its
  accepted architecture;
* update `docs/DEFERRED.md` so Linux physical output is no longer described as
  absent while native recovery gaps remain explicit; and
* preserve PulseAudio, PipeWire-Pulse, route-loss, Bluetooth, and suspend
  validation as native-hardware work until it is actually run.

No new ADR is planned: this implements the already accepted Linux port plan,
audio boundary, and semantic output-migration decisions.

## Validation

### Reproducible WSLg checks

Use fresh build directories where practical:

```sh
cmake -S . -B /tmp/sunplayer-linux-audio-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/sunplayer-linux-audio-debug --parallel 2
ctest --test-dir /tmp/sunplayer-linux-audio-debug \
  --output-on-failure --parallel 2
cmake --build /tmp/sunplayer-linux-audio-debug \
  --parallel 2 --target all_qmllint
```

Run the focused physical and production boundaries separately so their output
is retained in delivery evidence:

```sh
ctest --test-dir /tmp/sunplayer-linux-audio-debug \
  --output-on-failure -R 'cubeb-audio-sink|application-audio-first-playback'
```

Configure, build, and install Release, then run the installed playback
scenario against the same pinned fixture. Run fullscreen explicitly as useful
Wayland evidence without making its compositor result an audio gate. Record the
selected backend and advancing clock from typed diagnostics or bounded scenario
output.

Run `git diff --check` and inspect the complete change for accidental Windows-
only policy leakage or Linux-specific playback branches.

### Cross-platform check

Because `CubebAudioSink` and its build ownership become shared, rerun the
supported Windows build, device-backed sink test, application playback test,
and fullscreen test after the implementation. This is validation of the new
audio edit, not a prerequisite detour before beginning Linux work.

### Native-hardware evidence still required

WSLg does not close these gates:

* a native PulseAudio session;
* a native PipeWire-Pulse session;
* default-route replacement or service interruption;
* USB or Bluetooth disconnect/reconnect;
* suspend/resume;
* callback jitter and long-run clock stability; or
* physical speaker-to-display A/V offset.

Record these as missing coverage. Do not simulate them with new production
recovery machinery in this slice.

On native PulseAudio and PipeWire-Pulse, change the system-default output (or
move the active stream using the desktop's normal control) during real
playback. Acceptance requires output on the new route, a non-error session,
one unchanged `audioOutputEpoch`, and resumed monotonic presented/media
progress after a bounded transient. Use `pactl`, `wpctl`, or the desktop UI as
external evidence; do not add a production watcher or test-control seam.

## Review

Before implementation begins, review this plan independently for:

* cubeb lifecycle, callback safety, clock/drain semantics, and source
  compatibility;
* test boundaries, WSLg versus native claims, documentation impact, and
  delivery evidence; and
* simplicity and scope, explicitly rejecting unnecessary backend factories,
  Linux-specific sinks, custom Pulse/PipeWire code, speculative retry state,
  or test orchestration infrastructure.

Resolve substantive findings in this plan. Repeat review after implementation
when code or evidence changes the accepted design materially.

## Delivery result

The shared sink, CMake ownership, and `MediaSession` construction now differ
only where the platforms actually differ: Windows owns COM and names WASAPI;
Linux does neither. Both use cubeb's null/default output route and the existing
queue, callback, output ledger, clock, drain, gain, and error state.

The fresh Debug build passes all 26 registered Linux CTests and both QML lint
targets. The device-backed sink and registered audio-first application tests
pass through WSLg's Pulse-compatible server. A fresh Release build installs and
verifies its QML module; its production playback scenario reports backend
`pulse` and 73,599 presented audio frames under Vulkan validation.

The explicit Linux fullscreen run reached a WSLg buffer/configure protocol
failure after a valid `0 x 0` size hint and before its final audio assertion.
Ownership and root cause remain unresolved. That result is recorded without
reopening this audio plan or registering a flaky Linux CTest. Native audible
output, PulseAudio/PipeWire-Pulse route switching,
Bluetooth, suspend, service recovery, and physical A/V measurement remain the
bounded gaps listed above.

The post-change Windows build and device/application regression tests remain
pending because this delivery environment is Linux. `Complete` records the
implemented and reviewed Linux outcome; it does not override the explicit
cross-platform validation gap.

Correctness, evidence, and simplicity reviews found no need for a Linux audio
sink, device watcher, Pulse/PipeWire policy, backend factory, retry state, or
new clock. Review dispositions and exact validation evidence are retained in
[the delivery note](../cross-cutting/linux-port-evidence/2026-08-02-linux-system-cubeb-audio.md).

## Explicit non-goals

* A native PulseAudio or PipeWire API integration.
* Selecting Pulse by name in production.
* Application-level cubeb stream replacement or retry.
* A no-progress watchdog without a demonstrated backend wedge.
* Gapless route migration or acoustic-continuity claims.
* Multichannel output, passthrough, exclusive mode, resampler drift control,
  click-free gain ramps, or audio-device preferences.
* VAAPI, managed Wayland HDR, track selection, drag-and-drop, or packaging.
* A generic application-control protocol or additional test fixture format.

## Delivery boundary

Ship the source-compatibility edit, shared cubeb build/instantiation, reused
device test, registered Linux audio-first scenario, explicit fullscreen
observation, evidence, and synchronized current-truth documentation as one
coherent change. Do not mix audio recovery, platform-native sound APIs, or
unrelated V1 features into it.

Do not commit or push without separate authorization.
