# Deferred work and known limitations

This file records both deferred work and important limitations of the current
implementation. An active limitation must also appear in its subsystem plan;
the subsystem plan is authoritative for sequencing and acceptance. No item is
accepted behavior merely because it is documented here.

## Graphics and display

### Windows-only presentation backend

The factory-selected implementation currently configures Qt Quick and QRhi for
D3D11 and requests a Direct3D window surface. No non-Windows factory
implementation exists, so startup terminates on those platforms. macOS
Metal/EDR and Linux Vulkan/compositor presentation adapters remain
unimplemented.

Track under the backend-realization section of
`docs/subsystems/graphics/PLAN.md`.

### Incomplete decoded-video metadata and cross-platform hardware import

The explicit video surface, narrow final compositor, persistent libplacebo
renderer, and direct D3D11 QRhi target bridge now exist. Their analytic input
proves relative sRGB plus one fixed 1000-nit BT.2020/PQ signal across 80-, 100-,
and 203-nit reference-white values on a constant 600-nit target. This covers
the reference-white-relative target conversion from libplacebo's fixed
203-nit coordinate system, no-expansion behavior while the source fits,
highlight compression when it does not, and exactly one final Windows scRGB
scale. A retained software `AVFrame` path now demuxes and decodes a pinned
lossless RGB fixture, uploads it through libplacebo, and preserves relative SDR
white across target changes. It does not yet cover an FFmpeg-decoded mastered
PQ fixture, HLG's target-dependent OOTF, dynamic HDR metadata, representative
HDR YUV/chroma/range combinations, or full metadata provenance. A deterministic
FFV1 fixture covers compressed limited-range BT.709 YUV420P and non-square-pixel
aspect fitting. The Windows graphics domain owns a video-capable,
multithread-protected D3D11 device; an H.264 scenario proves D3D11VA NV12 direct
plane import, zero input copies/transfers, and observable software fallback.
P010/P012/P016 capture, same-device-copy and CPU fallback paths, real
device-loss injection, and the other platform importers remain required.
General display-matrix rotation still lacks a dedicated render capture.

The existing `203 * physicalPeak / referenceWhite` destination is explicitly
limited to relative SDR and static PQ. Exact libplacebo 7.360.1 source
inspection shows that its destination maximum becomes HLG's physical OOTF
peak, so the virtual destination would evaluate HLG against the wrong physical
display. HLG needs an API/policy that separates physical target peak from
output normalization. HDR10+ and Dolby Vision target-luminance semantics also
remain unverified and must not inherit the static-PQ formula automatically.
The current renderer does not yet gate those formats and still constructs the
same virtual target for every mapped source. Until the next effective-source
slice classifies and validates them, a resulting HLG or dynamic-HDR image is
experimental prototype behavior rather than proof of correct playback. The
next slice must preserve existing decodability while exposing whether Dolby
Vision metadata or only an HDR10-compatible base layer was used.

This is active work in the immediate FFmpeg/libplacebo format-acceptance
milestone, not a later static-PQ follow-up. HLG, HDR10+, and Dolby Vision are
required V1 inputs and remain listed here only as current integration
limitations until that milestone passes; Sunroom is not implementing parallel
format decoders or color pipelines.

Sunroom also does not yet propagate actual target display primaries to
libplacebo. The extended-linear BT.709 surface can encode wide-gamut
chromaticities, but the current unset `target.color.hdr.prim` is inferred as a
BT.709 target gamut. Platform target-gamut observation and shared propagation
are required before claiming wide-gamut output.

Track under graphics milestone 5 and the active testing plan.

### Qt private API compatibility

The implementation uses `Qt6::GuiPrivate` and QRhi private headers and pins Qt
6.11.1 exactly. Updating Qt requires deliberate compile and runtime validation
of resource creation, `QQuickRenderControl`, swapchain HDR information, surface
loss, and device recovery. A narrow wrapper reduces exposure but cannot remove
this maintenance cost.

### Runtime HDR validation and graphics tests

The Debug target builds and a headless D3D11 test captures both QRhi- and
libplacebo-produced RGBA16F surfaces plus SDR and extended-linear offscreen
composition. The project still has no recorded cross-display runtime matrix,
extended-linear swapchain capture, maintained renderer image corpus,
deterministic device-loss test, or automated physical HDR validation.
Presentation behavior must not be treated as portable or colorimetrically
verified until those tests exist.

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

### Display-state confidence and provenance

`PresentationOutputState` applies a fixed preference between Windows Advanced
Color data and QRhi swapchain data. It does not expose confidence, source
provenance, maximum full-frame luminance, brightness changes, calibration, or
user overrides. Expand the model when libplacebo target selection requires
those distinctions.

The current Windows observer also identifies the active `QScreen` from the
window center and listens to WinRT `AdvancedColorInfoChanged`. It does not yet
provide stable DisplayConfig/DXGI display identity, greatest-intersection
selection for spanning windows, topology revision guards, coalesced capability
reprobes, or stale asynchronous-result rejection. These are active in the
video-rendering plan rather than accepted as complete multi-display support.

### ICC transforms and calibration fallbacks

The pinned libplacebo build has LCMS disabled. Embedded source ICC bytes remain
owned by retained FFmpeg frames and can be preserved and diagnosed, but they
are not currently applied. Enabling source-ICC rendering requires reviewed
LCMS packaging, semantic profile validation, and an ICC-versus-scalar policy.
Initial support should be limited to validated SDR RGB profiles; ICC combined
with PQ, HLG, HDR10+, or Dolby Vision remains unsupported pending a separate
target model or upstream-supported integration.

Sunroom relies on the operating system or compositor for final display-profile
calibration on managed paths: Windows Advanced Color, future macOS
ColorSync/EDR, and future Wayland color-management-v1. Ordinary Windows
DirectX SDR output with Advanced Color inactive is an unmanaged sRGB-assumed
fallback. Application-managed display ICC is deferred; if implemented it must
transform the complete post-QRhi composition rather than video alone.

### HDR Lab target control affects production playback state

HDR Lab's manual target-headroom control currently shares presentation
settings with the Player. A later player-reliability slice must separate the
diagnostic override from production display policy so experiments cannot
silently alter ordinary playback.

## Build and tooling

### Vulkan and SPIR-V optimization tools are not configured

The validated Windows D3D11 build succeeds and packages HLSL shaders, but CMake
does not find a system Vulkan SDK and Qt Shader Tools reports that `spirv-opt`
is unavailable. The libplacebo port stages its pinned Vulkan-Headers source
snapshot only because disabled-backend stubs and public declarations require
those types; it does not enable or install Vulkan. This does not block the
current D3D11 target. Vulkan support and cross-backend shader validation
require a deliberate Vulkan SDK/tooling decision rather than treating the
current dependency build as proof of Vulkan readiness.

## Application and player

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
controlled output or the default Windows WASAPI device through cubeb. Presented
audio is the ordinary master clock; video-only media uses a monotonic clock,
and video continuing after audio drain uses an explicitly anchored monotonic
tail. The active graphics capability and whole-operation hardware fallback pass
through the same media operation, so audio never requires a second
`AVFormatContext`.

Short device underruns already map to hold silence without advancing media.
Sustained holds now enter a visible `Buffering` interruption, freeze the
timeline, and remain separate from explicit user play/pause intent. Sustained
loss of an established presentation clock is still terminal because the
player does not yet replace the physical stream for default-device changes,
Bluetooth reconnect, sleep/wake, or service interruption.
Volume and mute now apply at the output boundary without changing audio-clock
progression, and `MediaSession` exposes the active clock, PCM queue, submitted
and presented frames, and underrun count through a typed low-rate snapshot.
The visible Player summary currently renders the clock, backend, PCM queue,
and underruns. It still lacks click-free gain ramps, persistence, subtitles,
track selection, and a general diagnostics view. macOS and Linux physical
audio backends are not yet packaged or validated.

Pinned cubeb's WASAPI backend can migrate a null-device stream internally, but
does not expose a stream-specific success notification or guarantee that its
monotonic logical position excludes discarded old-device audio. Sunroom now
enumerates and opens the explicit multimedia default and disables opaque
default switching. Its narrow cubeb overlay patch also rejects same-endpoint
WASAPI client reconfiguration, so invalidation fails closed. The next slice will
re-enumerate on collection changes and replace only the audio-output epoch from
the last confident presented position. The single demux/video pipeline remains
alive; reopening the source is an explicit fallback for irreconcilable timeline
state.
See the dated Cubeb recovery research note.

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
