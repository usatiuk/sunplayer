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

### No libplacebo or decoded-video producer

The explicit video surface and narrow final compositor now exist, but the
current producer still renders only a procedural pattern with a simple
diagnostic tone mapper. This is intentionally not the video color pipeline.
FFmpeg source metadata normalization and a persistent libplacebo producer are
still required for SDR, HDR10/PQ, HLG, dynamic HDR, and differing source color
spaces.

Track under graphics milestones 3–4.

### Qt private API compatibility

The implementation uses `Qt6::GuiPrivate` and QRhi private headers and pins Qt
6.11.1 exactly. Updating Qt requires deliberate compile and runtime validation
of resource creation, `QQuickRenderControl`, swapchain HDR information, surface
loss, and device recovery. A narrow wrapper reduces exposure but cannot remove
this maintenance cost.

### Runtime HDR validation and graphics tests

The Debug target builds and a headless D3D11 test captures the RGBA16F producer
surface plus SDR and extended-linear offscreen composition, but the project has
no recorded cross-display runtime matrix, extended-linear swapchain capture,
renderer image corpus, deterministic device-loss test, or automated physical
HDR validation. Presentation behavior must not be treated as portable or
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

### Display-state confidence and provenance

`PresentationOutputState` applies a fixed preference between Windows Advanced
Color data and QRhi swapchain data. It does not expose confidence, source
provenance, maximum full-frame luminance, brightness changes, calibration, or
user overrides. Expand the model when libplacebo target selection requires
those distinctions.

## Build and tooling

### Vulkan and SPIR-V optimization tools are not configured

The validated Windows D3D11 build succeeds and packages HLSL shaders, but CMake
does not find Vulkan headers and Qt Shader Tools reports that `spirv-opt` is
unavailable. This does not block the current D3D11 target. Vulkan support and
cross-backend shader validation require a deliberate Vulkan SDK/tooling setup
rather than treating the current shader build as proof of Vulkan readiness.

## Application and player

### No player session or page

The thin QML shell, generic video viewport, and retained HDR Lab exist, but
there is no Player page, local-file workflow, persistent settings, playback
state, audio, subtitles, player controls, structured error model, or general
session diagnostics view yet. These remain in the root `PLAN.md` and should be
implemented as coherent vertical slices.
