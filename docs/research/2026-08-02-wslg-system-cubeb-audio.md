# WSLg and Ubuntu system-cubeb audio

## Question

Can Sunroom use Ubuntu 26.04's system cubeb package to open an advancing
device-backed default-output stream through WSLg, and what source differences
must the existing Windows cubeb sink accommodate?

This note records dependency-source inspection and a direct runtime probe. It
is implementation evidence, not a general PulseAudio, PipeWire, or native-
Ubuntu support claim.

## Environment

The inspected environment is Ubuntu 26.04 `resolute` under WSL2/WSLg. It has:

* `libcubeb0` and `libcubeb-dev`
  `0.0~git20250401.975a727+ds-1`;
* `PULSE_SERVER=unix:/mnt/wslg/PulseServer`;
* live `/mnt/wslg/PulseServer` and
  `/mnt/wslg/runtime-dir/pulse/native` Unix sockets; and
* the exact matching Ubuntu cubeb source package under the local dependency
  source tree.

Ubuntu exports cubeb through `find_package(cubeb CONFIG)` and the
`cubeb::cubeb` target. It does not install a pkg-config file.

## Package and source findings

The installed `libcubeb.so.0` directly depends on `libpulse.so.0`,
`libasound.so.2`, `libjack.so.0`, and `libsndio.so.7`. Its matching source was
built with `libpulse-dev`, ALSA, JACK, and sndio development packages.

The matching `src/cubeb.c` default backend list attempts the compiled Pulse
backend before JACK, sndio, and ALSA. Passing a null backend name therefore
selects Pulse when WSLg's server is available without making Pulse a Sunroom
policy constant. This matches the accepted system-default-route design.

The Pulse implementation supplies:

* stream position;
* reported latency; and
* output device-collection change notifications.

It does not supply cubeb's stream-specific device-changed callback. Sunroom
does not use that callback: the existing sink already treats collection
revision as optional diagnostics rather than proof of a completed route
migration.

The installed public header states that passing a null output `cubeb_devid`
allows the stream to follow the operating system's default output. That is the
portable routing contract Sunroom consumes. In the matching Pulse backend, a
server/default change refreshes cubeb's internal `default_sink_info` but does
not invoke the device-collection callback; that callback covers device
addition and removal. Collection revision therefore cannot be treated as a
default-switch completion event. PulseAudio, PipeWire-Pulse, or the desktop
session owns any live movement of the null-device stream.

Ubuntu's `cubeb_stream_params` ends after `prefs`; it does not contain the
newer `input_params` member initialized by Sunroom's Windows build. That member
does not describe an output-only stream requirement. Omitting it from the
aggregate initializer value-initializes any trailing member in the newer
header and compiles against the Ubuntu snapshot. A compatibility wrapper or
version macro is unnecessary.

The Ubuntu header and shared library also do not expose the newer
`cubeb_get_backend_names()` enumeration helper used by Sunroom's Windows
dependency test. Linux can verify the common public ABI without a device and
observe the selected backend only after `cubeb_init()`. Sunroom should not
infer compiled-backend support by inspecting ELF dependencies at runtime.

## Runtime probe

A temporary program used only cubeb's public C API to:

1. call `cubeb_init` with a null backend name;
2. request the current Sunroom output format of 48 kHz, stereo,
   native-endian float32;
3. query the minimum latency;
4. open the default output device;
5. start a callback that produces silence;
6. wait up to three seconds for a nonzero playback position; and
7. stop and destroy the stream.

It was compiled and run with:

```sh
c++ -std=c++20 -Wall -Wextra -Wpedantic \
  /tmp/sunroom-cubeb-wslg-probe.cpp \
  -lcubeb -pthread -o /tmp/sunroom-cubeb-wslg-probe
/tmp/sunroom-cubeb-wslg-probe
```

The result was:

```text
Failed to load cookie file from cookie: No such file or directory
backend=pulse requested_latency_frames=1200 started=1 \
position_frames=307 latency_result=0 reported_latency_frames=1211 \
callback_error=0
```

The process exited successfully. The Pulse client printed a missing-cookie
diagnostic, but WSLg accepted the connection and the cubeb stream started,
advanced, and reported latency. Sunroom should not add cookie discovery or a
WSLg special case for a connection that already succeeds.

## Consequences

The first Linux audio implementation can reuse `CubebAudioSink` directly:

* retain its dedicated control thread, callback, queue, output ledger, clock,
  drain, gain, and diagnostics;
* compile COM initialization and teardown only on Windows;
* pass `"wasapi"` on Windows and a null backend name on Linux;
* omit the optional `cubeb_stream_params::input_params` initializer;
* compile and link the existing sink on both currently supported platforms;
* instantiate it from `MediaSession` on Linux; and
* validate the selected backend through existing typed diagnostics after the
  physical context opens.

The probe does not establish audible correctness, default-route migration,
service interruption, suspend/resume, Bluetooth behavior, PipeWire-Pulse
behavior, or physical A/V synchronization. WSLg can close the first open/start/
position and production playback gates; native Linux systems remain required
for the wider audio support matrix. Default switching is supported by cubeb's
null-device contract, but remains unvalidated on native PulseAudio and
PipeWire-Pulse until a real route-change run proves continued output and clock
progress.
