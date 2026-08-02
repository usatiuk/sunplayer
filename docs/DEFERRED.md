# Deferred work and known limitations

This file records both deferred work and important limitations of the current
implementation. An active limitation must also appear in its subsystem plan;
the subsystem plan is authoritative for sequencing and acceptance. No item is
accepted behavior merely because it is documented here.

## Graphics and display

### Remaining macOS native-hardware and packaging validation

The Apple-Silicon macOS path now runs QRhi Metal presentation, direct
Metal/MoltenVK video-target interop, AppKit relative EDR observation,
VideoToolbox NV12/P010 import, subtitles, seeking, and cubeb AudioUnit playback
on the available Apple M2/macOS 26 host. That host's built-in display reported
current EDR headroom `1.0` and potential headroom `2.0`. Physical output above
SDR white, movement between unlike SDR/EDR displays, live HDR/SDR switching,
brightness/headroom changes, hotplug, sleep/wake, and paused-frame behavior
across those changes remain native-hardware checks. Playback is user-confirmed
audible on the current macOS default device; a live system-default route change
remains unverified.

Packaging is intentionally after the non-packaging port. It must build and
inspect a self-contained bundle, close Qt/plugin/native-library dependencies,
verify the signature/runtime from outside the build tree, and rebuild the
vcpkg dependency archives for the declared macOS 13 floor. The current cached
archives were built for the newer host target, so the application binary's
`minos 13.0` load command alone is not a macOS 13 compatibility claim. Intel,
notarization, and App Store distribution remain out of scope.

One direct fullscreen smoke passed and interactive fullscreen works in
user-confirmed playback. Repeated live-desktop AppKit automation was sensitive
to concurrent user input and is not registered as a deterministic CTest.
Revisit automation only with reproducible supported-environment evidence or a
controlled boundary; do not add timing or event-order workarounds.

### Remaining decoded-video coverage and cross-platform hardware import

The explicit video surface, narrow final compositor, persistent libplacebo
renderer, and direct D3D11 QRhi target bridge now exist. Their analytic input
proves relative sRGB plus one fixed 1000-nit BT.2020/PQ signal across 80-, 100-,
and 203-nit reference-white values on a constant 600-nit target. This covers
the reference-white-relative target conversion from libplacebo's fixed
203-nit coordinate system, no-expansion behavior while the source fits,
highlight compression when it does not, and exactly one final Windows scRGB
scale. A retained software `AVFrame` path demuxes and decodes pinned lossless
RGB, compressed SDR, and four-frame Main10 HEVC fixtures. The HDR corpus covers
static PQ mastering/content-light metadata and analytical patches, two HLG
targets, two frame-local HDR10+ scenes, and mapped Dolby Vision Profile 8.1
reshape data. A deterministic FFV1 fixture covers compressed limited-range
BT.709 YUV420P and non-square-pixel aspect fitting. The Windows graphics domain
owns a video-capable,
multithread-protected D3D11 device; an H.264 scenario proves D3D11VA NV12 direct
plane import, zero input copies/transfers, and observable software fallback.
Windows P010/P012/P016 capture, same-device-copy and CPU fallback paths, real
device-loss injection, and the Linux native importer remain required. macOS
H.264/NV12 and Main10 HEVC/P010 direct import are capture-validated across
multiple frames.
General display-matrix rotation still lacks a dedicated render capture.

All SDR, PQ/HDR10, HLG, HDR10+, and Dolby Vision inputs use the same
FFmpeg/libplacebo path. Static PQ has the strongest numerical evidence. The
tested HLG behavior is accepted for display-relative playback, but does not
claim absolute-reference monitoring; a focused upstream physical-peak/output-
coordinate separation remains the next option if measurements reject it.
HDR10+ and Dolby Vision support is proven only for the checked-in sequences,
not every profile, target trim, or enhancement-layer path. Broader BT.601,
BT.2020 SDR, full-range, 12-bit, chroma-location, contradictory-metadata, and
dynamic-HDR profile coverage remains deferred. None of those gaps justifies a
parallel source parser, decoder, or color pipeline.

Sunroom also does not yet propagate actual target display primaries to
libplacebo. The extended-linear BT.709 surface can encode wide-gamut
chromaticities, but the current unset `target.color.hdr.prim` is inferred as a
BT.709 target gamut. Platform target-gamut observation and shared propagation
are required before claiming wide-gamut output.

Track under graphics milestone 5 and the active testing plan.

### Qt private API compatibility

The implementation uses `Qt6::GuiPrivate`, QRhi private headers, and on Linux
`Qt6::WaylandClientPrivate`. Windows pins Qt 6.11.1 exactly; Linux accepts the
system Qt 6.10 family. Updating either family requires deliberate compile and
runtime validation of resource creation, `QQuickRenderControl`, swapchain HDR
information, surface loss, device recovery, and the matching Wayland private
ABI where applicable. Narrow wrappers reduce exposure but cannot remove this
maintenance cost. A Qt version containing qtdeclarative commit
`bd1da1d7972f02a3be6e872a5fa05f73556d56d3` may make Sunroom's explicit macOS
native-dialog parent binding redundant; remove it only after native and forced
non-native dialog paths are both verified with the new Qt build.

### Runtime HDR validation and graphics tests

Headless graphics tests capture QRhi- and libplacebo-produced RGBA16F surfaces
plus SDR and extended-linear offscreen composition. WSLg exercises the real
native-Wayland Vulkan/QRhi/libplacebo software path, but supplies neither a
native GPU nor a managed HDR display. The project still has no recorded cross-
display runtime matrix, extended-linear swapchain capture, maintained renderer
image corpus, deterministic device-loss test, or automated physical HDR
validation. Presentation behavior must not be treated as portable or
colorimetrically verified until those tests exist.

### Flattened translucent Qt Quick content

Qt Quick composites the interface into one texture before the final shader
decodes the flattened layer to linear light. Overlapping translucent elements
are therefore not perfectly colorimetric. The current mostly opaque UI accepts
this limitation. Measure visible impact before considering per-item linear
composition or another integration shape.

### Incomplete redirected input

Mouse, wheel, and keyboard events are forwarded to the redirected Quick window.
Touch, tablet, input methods, accessibility, drag-and-drop, and richer pointer
semantics are not integrated.

Address as part of the application/UI shell rather than adding isolated event
forwarders without an input model.

### Recovery ends in process termination

Device loss has bounded retries and unexpected frame failures get one rebuild,
but exhaustion calls `qFatal`. The player eventually needs structured
presentation errors, a user-visible recovery state, and a way to keep unrelated
application state alive.

### One window and one presentation domain

The current ownership model assumes one `PresentationWindow`, one QRhi, and one
target display at a time. Multi-window playback, picture-in-picture, mirroring,
and simultaneous rendering for displays with different HDR targets are
deferred until a single-window playback pipeline is dependable.

### Full-window RGBA16F UI cost

Redirected Qt Quick uses a full-window RGBA16F texture and a final fullscreen
pass. This is accepted for explicit composition. Memory bandwidth, power, and
damage behavior must be profiled with real playback before optimizing the
integration or adding platform-specific variants.

### Additional display capability diagnostics

`PresentationOutputState` applies a fixed preference between Windows Advanced
Color data and QRhi swapchain data. It does not expose maximum full-frame
luminance, detailed raw capability provenance, calibration identity, or user
overrides. Add a raw fact only when renderer policy or a concrete support
diagnostic consumes it; rendering continues to use one effective semantic
target.

Stable DisplayConfig identity, a custom greatest-intersection selector,
topology generations, confidence scores, and a general asynchronous probe
pipeline are not planned correctness mechanisms. Windows' cached HWND-bound
`DisplayInformation` remains the Advanced Color authority. Native identity and
topology data may be added locally for diagnostics or adapter enumeration if a
real backend need appears.

### ICC transforms and calibration fallbacks

Embedded source ICC bytes remain owned by retained FFmpeg frames and are
diagnosed, but Sunroom does not currently apply them on any platform. The
render-local libplacebo frame clears both ICC handles so Ubuntu's LCMS-enabled
system build cannot silently diverge from the LCMS-disabled Windows build.
Enabling source-ICC rendering requires consistent LCMS packaging, semantic
profile validation, and an ICC-versus-scalar policy.
Initial support should be limited to validated SDR RGB profiles; ICC combined
with PQ, HLG, HDR10+, or Dolby Vision remains unsupported pending a separate
target model or upstream-supported integration.

Sunroom relies on the operating system or compositor for final display-profile
calibration on managed paths: Windows Advanced Color, implemented macOS
ColorSync/EDR surface declaration, and the implemented Qt-owned Wayland
managed-SDR declaration.
Managed Wayland HDR transitions remain pending. Ordinary Windows DirectX SDR
output with Advanced Color inactive and native Wayland without a usable
managed-SDR capability are unmanaged sRGB-assumed fallbacks.
Application-managed display ICC is deferred; if implemented it must transform
the complete post-QRhi composition rather than video alone.

### HDR Lab target control affects production playback state

HDR Lab's manual target-headroom control currently shares presentation
settings with the Player. A later player-reliability slice must separate the
diagnostic override from production display policy so experiments cannot
silently alter ordinary playback.

## CI and release engineering

### Hosted hardware and mixed-test coverage

Generic Windows CI builds the complete codebase but excludes CTests labeled
`device` or `gpu`. A future dedicated Windows runner with a supported physical
GPU and audio endpoint should run the full suite, including D3D rendering,
D3D11VA, Advanced Color, HDR scenarios, and real cubeb output. The current
`ffmpeg-first-frame` registration mixes software/HDR coverage with required
D3D11VA cases, so its complete executable is excluded on hosted Windows;
splitting a useful hosted software subset is deferred instead of refactoring
the test solely for initial CI.

Linux hosted CI uses lavapipe and a Pulse null sink. A VAAPI/DRM PRIME hardware
runner, an HDR/display lab, and physical-audio/default-route scenarios remain
separate future lanes. Packaging/release workflows remain deferred until
packaging is defined. macOS CI remains deferred until its bundle/deployment
contract and useful hosted-versus-native-hardware test split are defined.

## Application and player

### Wayland decoration negotiation is not observable through public Qt API

Sunroom selects system decoration when xdg-decoration is advertised and
application chrome when it is absent. Advertisement does not guarantee that
the compositor's final choice will be server-side, while Qt does not expose
that result publicly. A compositor that advertises the protocol but selects
client-side mode can therefore leave the Vulkan window undecorated. This rare
mismatch is accepted for the current high-level integration; do not add a Qt-
private listener, compositor allowlist, or runtime ownership switch without a
reproducible supported-environment failure.

### Remote media input, source read-ahead, and Jellyfin

`MediaSession` currently rejects non-local URLs and the FFmpeg request carries
a filesystem path. The shared production operation nevertheless has the right
single-pass shape for remote media: one `AVFormatContext` feeds both selected
audio and video streams. Its 128-packet/8-MiB router is post-demux buffering,
not a controllable source-byte cache, so slow SMB reads or network bursts can
still drain playback.

The planned first step is a small sanitized media-input request and FFmpeg's
native HTTP/range/HLS support, followed by timeout, stall, occupancy, and
Buffering telemetry. A duration-aware encoded-packet budget may be enough for
ordinary burstiness. Custom AVIO with bounded memory or disk read-ahead remains
available when real sources demonstrate that FFmpeg-native I/O cannot provide
the required caching, credential refresh, retry, or cancellation behavior.
It must replace the existing input edge rather than create a second reader.

A Jellyfin client is possible future product scope, not part of the current V1
commitment. A server layer would own authentication, library/navigation,
playback negotiation, progress reporting, and transcode-session lifetime, then
hand one direct-play, remux, or HLS locator to the ordinary media pipeline.
Audio-track selection, remote subtitle discovery/download, and efficient remote
seeking remain prerequisites for a complete experience. See
[media input and source buffering](subsystems/media-io/README.md).

### Pre-Qt startup diagnostics

The bounded session logger is installed after `QGuiApplication` exists and
command-line options have been parsed. A missing or unloadable Qt platform
plugin can therefore fail before the session file is available. The packaged
application test covers the normal deployed runtime, but a future early
bootstrap or launcher should capture loader/platform initialization failures
without duplicating Sunroom's command-line and logging policy.

### Playback lacks physical audio-device replacement

The thin QML Player now continuously demuxes, decodes, schedules, and presents
local video with bounded packet/frame channels, working play/pause/replay, and
a position/duration seek timeline. Seeking, hardware-import fallback, and
graphics-device recovery share a fresh-context, keyframe-anchored restart and
preserve the logical position.

The production session now uses one-open A/V packet routing, real FFmpeg audio
decode, libswresample conversion, a common nonzero timeline, and either bounded
controlled output or the system-default cubeb route on Windows, macOS, and
Linux.
Presented audio is the ordinary master clock; video-only media uses a monotonic
clock, and video continuing after audio drain uses an explicitly anchored
monotonic tail. The active graphics capability and whole-operation hardware
fallback pass through the same media operation, so audio never requires a
second `AVFormatContext`.

Short device underruns already map to hold silence without advancing media.
Sustained holds now enter a visible `Buffering` interruption, freeze the
timeline, and remain separate from explicit user play/pause intent. Sustained
loss of an established presentation clock is still terminal because the
player does not yet replace a failed physical stream after route loss,
Bluetooth reconnect, sleep/wake, or service interruption. Ordinary default-
route switching remains cubeb and the sound service's responsibility.
Volume and mute now apply at the output boundary without changing audio-clock
progression, and `MediaSession` exposes the active clock, PCM queue, submitted
and presented frames, and underrun count through a typed low-rate snapshot.
The visible Player summary currently renders the clock, backend, PCM queue,
and underruns. It still lacks click-free gain ramps, persistence, audio-track
selection, and a general diagnostics view. WSLg system-cubeb output and its
advancing clock are validated; native PulseAudio/PipeWire-Pulse switching and
recovery and acoustic output, physical speaker-to-display A/V measurement, and
live macOS default-route movement remain unvalidated. macOS AudioUnit sink and
production playback checks pass on the available Apple M2 host, where playback
is user-confirmed audible; macOS packaging remains separate.

### Expanded subtitle features

Embedded subtitle discovery, selection, text/ASS rendering, embedded fonts, and
FFmpeg-decoded bitmap composition are integrated. Deferred work includes
external sidecar discovery and downloads, Jellyfin/server subtitle APIs, two
simultaneous tracks, user font/color/position/scale/opacity/delay settings,
forced-only policy, control-overlay avoidance, non-square-pixel ASS script
geometry, and exact VSFilter color behavior. Seeking deliberately does not scan
or reread subtitle history; an already-active cue may remain absent until the
next naturally decoded update.

Cubeb's WASAPI, AudioUnit, and Pulse-family backends can follow a null-device
stream through ordinary system-default routing, but they do not expose one
portable stream-specific success notification or guarantee gapless delivery
of audio queued to the old endpoint. Sunroom opens cubeb's default route and
keeps one output epoch for one cubeb stream while cubeb and the sound service perform ordinary
route migration, as decided by ADR 0016.

Application-level recreation after a cubeb error remains deferred. That path
will replace only the audio stream, start and re-anchor a new output epoch from
the last confident media position, and preserve the single demux/video
pipeline. A no-progress watchdog is added only if real-device tests demonstrate
a wedge without an error. See the original Cubeb recovery note and the later
display/audio migration reconciliation.

The current seek implementation reopens and reprobes a local file for each
restart. A future persistent-context optimization requires an explicit
demux/decoder ownership barrier, stale-packet discard, and codec flush; it must
not be introduced as an ad hoc command into the current bounded queues.

The local-file operation uses FFmpeg's interrupt callback and stop-aware
bounded channels. Cancel and replacement invalidate UI state immediately
without joining the worker; only the latest pending open is retained. A
mounted filesystem blocked uninterruptibly inside the kernel can still stall
final shutdown. Helper-process containment remains the intended stronger
boundary for unreliable network filesystems.

The deterministic session scenarios reach real FFmpeg decode, fill the
production frame mailbox, and now prove bounded playback reaches end of stream
without any presentation consumer. A registered actual-process audio-first
scenario also requires two distinct frames to traverse the production
QRhi/libplacebo swapchain while the real Cubeb clock advances. Command-driven
actual-process play/pause/seek/error scenarios remain missing. Direct router
saturation/cancellation tests exist, but no real media fixture deliberately
exhausts the startup packet budget with pathological stream interleave.
