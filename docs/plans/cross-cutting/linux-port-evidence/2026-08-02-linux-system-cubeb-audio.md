# Linux system-cubeb audio evidence

## Scope

This note records the first production Linux audio slice based on commit
`7c7d30b` plus the pending delivery change. It covers Ubuntu system-cubeb
integration, the WSLg Pulse-compatible route, the real sink lifecycle, and the
production audio-first application path. It does not claim native PulseAudio,
PipeWire-Pulse, native Linux speakers, or live route-change recovery.

## Environment

```text
Distribution: Ubuntu 26.04 resolute under WSL2/WSLg
Kernel: 6.18.33.2-microsoft-standard-WSL2
Pulse server: unix:/mnt/wslg/PulseServer
libcubeb0: 0.0~git20250401.975a727+ds-1
libcubeb-dev: 0.0~git20250401.975a727+ds-1
Qt: 6.10.2
Presentation: native Wayland, llvmpipe Vulkan
```

`ldd` resolves the installed executable to Ubuntu's
`/usr/lib/x86_64-linux-gnu/libcubeb.so.0`; that library resolves PulseAudio and
ALSA libraries from the same system installation. The runtime selected backend
reported by Sunroom's typed audio diagnostics is `pulse`.

```sh
ldd /tmp/sunroom-linux-audio-install/bin/sunroom \
  | rg 'cubeb|pulse|asound'
dpkg-query -W -f='${Package} ${Version}\n' libcubeb0 libcubeb-dev
printf '%s\n' "$PULSE_SERVER"
uname -r
```

## Implemented contract

The existing `CubebAudioSink` now serves both supported platforms:

* Windows retains its sink-owned COM MTA and explicit `wasapi` backend.
* Linux omits COM and lets `cubeb_init()` select the system backend.
* Both pass a null output-device ID to `cubeb_stream_init()`.
* Cubeb and the operating-system sound service own ordinary default-route
  migration inside the existing cubeb stream and `audioOutputEpoch`.
* Callback, queue, output-ledger, clock, drain, gain, and failure behavior are
  shared without a Linux factory, watcher, Pulse client, or retry state.

Ubuntu's older `cubeb_stream_params` ends after `prefs`. Omitting the optional
newer `input_params` initializer keeps the output-only request source-compatible
and value-initializes that trailing member where it exists.

## Reproducible results

The Debug build completed from a fresh Ninja tree with tests enabled:

```sh
cmake -S . -B /tmp/sunroom-linux-audio-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/sunroom-linux-audio-debug --parallel 2
```

Focused device and application gates passed:

```sh
ctest --test-dir /tmp/sunroom-linux-audio-debug \
  --output-on-failure \
  -R 'cubeb-audio-sink|application-audio-first-playback'
```

```text
cubeb-audio-sink                    Passed
application-audio-first-playback    Passed
2/2 tests passed
```

The complete Debug suite and QML lint passed:

```sh
ctest --test-dir /tmp/sunroom-linux-audio-debug \
  --output-on-failure --parallel 2
cmake --build /tmp/sunroom-linux-audio-debug --target all_qmllint
```

```text
26/26 CTests passed
all_qmllint passed
```

A fresh Release tree configured, built, and installed with tests disabled:

```sh
cmake -S . -B /tmp/sunroom-linux-audio-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/sunroom-linux-audio-install
cmake --build /tmp/sunroom-linux-audio-release --parallel 2
cmake --install /tmp/sunroom-linux-audio-release
```

The installed QML module verified successfully:

```sh
/tmp/sunroom-linux-audio-install/bin/sunroom \
  --verify-qml --no-log-file
```

The installed production playback scenario also passed with Vulkan standard
and synchronization validation enabled:

```sh
env VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  VK_LAYER_VALIDATE_SYNC=1 \
  /tmp/sunroom-linux-audio-install/bin/sunroom \
  --playback-smoke --no-log-file \
  tests/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv
```

```text
Sunroom playback smoke passed: positionMs=1533,
audioBackend=pulse, audioPresented=73599
```

This crosses the Qt Wayland window, Vulkan/QRhi/libplacebo presentation,
single-pass FFmpeg A/V decode, system cubeb, WSLg Pulse server, advancing cubeb
position, application audio clock, and orderly automatic shutdown.

The Pulse client printed `Failed to load cookie file from cookie`, but the
context, stream, position, and application scenario all succeeded. No WSLg
cookie special case was added.

The user also opened and played a real file through the WSLg build and
confirmed audible output and working playback. This closes the WSLg audible-
playback observation only; it is not native PulseAudio/PipeWire-Pulse or route-
switch evidence.

## Non-gating fullscreen observation

The audio-bearing installed fullscreen scenario was run explicitly with
Vulkan validation. WSLg terminated the Wayland connection after a valid
maximized `0 x 0` size hint, which lets the client choose its size, and a later
buffer/configure mismatch involving the submitted `3840 x 2160` buffer. The
application exited with a protocol error before the scenario's final audio-
continuity assertion. Ownership and root cause remain unresolved.

```sh
env VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  VK_LAYER_VALIDATE_SYNC=1 \
  /tmp/sunroom-linux-audio-install/bin/sunroom \
  --fullscreen-smoke --no-log-file \
  tests/fixtures/media/sdr-bt709-ffv1-video-late-flac.mkv
```

```text
qt.qpa.wayland: Configure event with maximized or fullscreen state contains invalid width: 0
qt.qpa.wayland: Configure event with maximized or fullscreen state contains invalid height: 0
xdg_wm_base#3: error 4: xdg_surface buffer (3840 x 2160) does not match the configured maximized state (0 x 0)
The Wayland connection experienced a fatal error: Protocol error
process exit: 255
```

The first two lines are Qt's diagnostics, not a protocol classification in
this note; xdg-shell permits the zero-size hint.

This is retained as compositor/lifecycle evidence under the existing WSLg
fullscreen gap. It does not invalidate the focused sink or application
playback audio gates and is not registered as a default Linux CTest.

## Remaining evidence

Native Linux must still validate:

* audible playback on PulseAudio and PipeWire-Pulse sessions;
* changing the system-default output during playback while the same Sunroom
  `audioOutputEpoch` remains live and presented media progress resumes;
* USB/Bluetooth loss and reconnection;
* sound-service interruption and restart;
* suspend/resume, callback jitter, and long-run clock behavior; and
* physical speaker-to-display A/V offset.

No custom watcher or stream-recreation mechanism is planned for ordinary
default switching. Application-level replacement remains deferred until a
cubeb error or demonstrated persistent no-progress condition requires it.

The post-change Windows regression build and device/application tests were not
available in this Linux environment. The shared compile/source changes are
reviewed for Windows compatibility, but the Windows rerun remains the explicit
`BUILD-12` gate.

## Review dispositions

Independent correctness, evidence, and simplicity reviews found no production
audio defect and rejected any Linux-specific sink, backend factory, device
watcher, Pulse/PipeWire client, retry state, or duplicate clock.

Accepted review corrections:

* made the cubeb plan complete on the focused audio gates rather than the
  unrelated WSLg fullscreen result;
* made ADR 0011 distinguish pinned Windows cubeb from Linux system cubeb;
* added direct `<cstdint>` ownership and the Windows fullscreen `audio` label;
* described the valid zero-size Wayland hint and later protocol failure without
  assigning root cause;
* separated historical video-only fullscreen evidence from the current audio-
  bearing scenario;
* scoped build-matrix claims to the configurations rerun by this change; and
* retained the missing Windows rerun and native Linux device matrix explicitly.
