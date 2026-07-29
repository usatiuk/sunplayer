# Deferred work and known limitations

This file records important issues that are intentionally outside the current
implementation slice. Items here are not accepted behavior merely because they
are documented; move them into a subsystem plan when they become active work.

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
proves relative sRGB and target-relative BT.2020/PQ rendering across multiple
active reference-white values, including the explicit conversion from
libplacebo's 203-nit linear convention to Sunroom's display-relative surface
convention. A retained software `AVFrame` path now demuxes and decodes a pinned
lossless RGB fixture, uploads it through libplacebo, and preserves relative SDR
white across target changes. It does not yet prove one fixed, absolutely
mastered PQ frame, representative YUV/chroma/range combinations, or full
metadata provenance. A deterministic FFV1 fixture now covers compressed
limited-range BT.709 YUV420P and non-square-pixel aspect fitting, but not a
fixed absolutely mastered PQ frame. The Windows graphics domain now owns a
video-capable, multithread-protected D3D11 device; an H.264 scenario proves
D3D11VA NV12 direct plane import, zero input copies/transfers, and observable
software fallback. P010/P012/P016 capture, same-device-copy and CPU fallback
paths, real device-loss injection, and the other platform
importers remain required. General display-matrix rotation still lacks a
dedicated render capture.

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

### Playback has no seek, audio clock, or unified buffering recovery

The thin QML Player now continuously demuxes, decodes, schedules, and presents
local video with bounded packet/frame channels and working play/pause/replay.
It does not yet expose position/duration, seek, audio, subtitles, track
selection, source-stall buffering/recovery, persistent settings, or a general
diagnostics view.

Hardware-import fallback and graphics-device recovery restart the current file
from the beginning. Preserving position requires a shared
keyframe-anchored seek/decode-to-anchor primitive; restarting an arbitrary
hardware decoder from the middle of a packet queue would be incorrect.

The local-file operation uses FFmpeg's interrupt callback and stop-aware
bounded channels. Cancel and replacement invalidate UI state immediately
without joining the worker; only the latest pending open is retained. A
mounted filesystem blocked uninterruptibly inside the kernel can still stall
final shutdown. Helper-process containment remains the intended stronger
boundary for unreliable network filesystems.

The deterministic session scenario reaches real FFmpeg decode and fills the
production frame mailbox, but it drives
`DecodedVideoSource::prepareForPresentation()` directly. A multi-frame
`RhiPresentationEngine` cadence test and an actual-process open/play/pause
scenario remain missing. The packet channel's count/byte limits are enforced
by production code but do not yet have direct saturation diagnostics or a
fixture large enough to assert demux blocking at that boundary.
