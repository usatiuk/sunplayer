# Cross-Platform HDR Video Player Plan

## Goals

* **Cross-platform by design.** Support Windows, macOS, and Linux while maximizing shared code and consistent behavior.
* **Reuse mature components.** Prefer Qt, QRhi, FFmpeg, libplacebo, libass, and operating-system facilities over custom implementations where they fit.
* **Correct modern HDR and color handling.** Support HDR and SDR content, modern platform HDR pipelines, multiple displays, dynamic display changes, SDR white level, display capabilities, operating-system color information, and relevant HDR metadata.
* **Simple and opinionated.** Provide sane defaults and a small user-facing option surface.
* **Hardware accelerated and efficient.** Prefer hardware decoding, GPU-native rendering, and minimal data movement without prematurely chasing platform-specific micro-optimizations.
* **Modular, extensible, and testable.** Keep media, playback, rendering, graphics, audio, subtitles, UI, and platform integration independently understandable.
* **Responsive and resilient.** Slow, unreliable, or disconnected media sources must not freeze the interface or unrelated subsystems.
* **Useful before feature-complete.** Prioritize a small, reliable player over broad but shallow feature coverage.

## Non-goals for the initial version

* YouTube, streaming-service, DRM, or service-specific integrations.
* Media-library management, metadata scraping, recommendations, accounts, or synchronization.
* Casting, remote control, watch parties, or media-server functionality.
* DVD, Blu-ray, television, or optical-disc navigation.
* Video editing, transcoding, color grading, or general-purpose filtering.
* A user-selectable matrix of rendering and decoding backends.
* Exposing the full FFmpeg or libplacebo option surface.
* Custom shaders and extensive image-adjustment controls.
* Perfect support for every obscure codec, container, subtitle feature, or HDR profile.
* Perfect zero-copy behavior on every platform in the first implementation.
* Platform-specific optimization branches before profiling shows they are needed.
* A complex library or browser interface before playback is dependable.

Temporary capability differences between platforms are acceptable. The overall architecture and rendering model should remain shared.

## Initial user-visible scope

* Open a local file.
* Drag and drop a media file.
* Play and pause.
* Seek using a timeline.
* Display current position and duration.
* Jump backward and forward.
* Select an audio track.
* Select a subtitle track or disable subtitles.
* Control volume and mute.
* Enter and leave fullscreen.
* Display clear loading, buffering, and error states.
* Provide basic keyboard shortcuts.
* Provide a diagnostics view for the active decode, rendering, display, and fallback paths.

## System overview

```text
Media source
→ FFmpeg demuxing and decoding
→ libplacebo video rendering
→ QRhi composition with subtitles and Qt Quick UI
→ native HDR, EDR, or SDR presentation
```

Audio, subtitles, playback scheduling, display observation, and source buffering remain separate subsystems around this path.

Detailed technical context is recorded in `docs/ARCHITECTURE_NOTES.md`.

## Current implementation status

As of 2026-07-28, the repository contains a Windows presentation foundation,
not yet a media player:

* A single custom `QWindow` owns an application-created D3D11 QRhi and
  swapchain.
* Qt Quick renders through `QQuickRenderControl` into an application-owned
  RGBA16F texture.
* A temporary QRhi producer renders the procedural HDR diagnostic pattern into
  an explicitly described, canvas-sized RGBA16F video surface.
* A narrow final QRhi pass places that already processed video surface,
  combines it with the UI, and presents extended-linear sRGB/scRGB when
  available, with an SDR fallback.
* Qt screen state, QRhi swapchain HDR information, and Windows Advanced Color
  telemetry feed a shared presentation snapshot and diagnostic UI.
* Rendering is demand-driven outside explicit animation, and the graphics
  foundation handles resize, output changes, surface destruction, swapchain
  invalidation, and bounded device recovery.
* The configured Windows Debug target builds successfully with Qt 6.11.1 and
  MSVC.

FFmpeg, libplacebo, libass, decoded video, audio, playback, file opening,
subtitles, and persistence are not integrated. CTest/Qt Test coverage exists
for pure presentation-target policy and rendered-video surface
validity/invalidation and a real D3D11 offscreen producer/compositor capture.
Whole-application scenarios do not yet exist.

The next implementation slices establish the already-known graphics-device,
video-producer, and target-interop contracts around the diagnostic producer,
then extract a thin QML shell, retained HDR Lab page, and generic viewport
without introducing a pretend Player UI. A persistent libplacebo renderer for
known SDR and HDR software-backed images follows. The real Player page arrives
with the file/session model and first FFmpeg-decoded frame. Testing continues
alongside that work with real-backend capture and the first deterministic frame
scenario.

## Subsystems

### 1. Application shell

* [x] Basic application and presentation-window lifecycle for the current
  Windows diagnostic shell
* [ ] Settings and persistence
* [ ] File-open, drag-and-drop, and platform file associations
* [ ] Top-level error and logging integration

Documentation: `docs/subsystems/application/`

### 2. Media sources and file loading

* [ ] Local-file source
* [ ] Cancellation and timeout model
* [ ] FFmpeg AVIO integration
* [ ] Bounded read-ahead and byte caching
* [ ] Unreliable or blocking source isolation model
* [ ] Clear source-stall and recovery states

Documentation: `docs/subsystems/media-io/`

### 3. FFmpeg media integration

* [ ] Container opening and probing
* [ ] Stream, chapter, and attachment discovery
* [ ] Packet demuxing
* [ ] Video decoding
* [ ] Audio decoding
* [ ] Subtitle decoding
* [ ] Hardware-device capability discovery
* [ ] Metadata and timestamp normalization

Documentation: `docs/subsystems/media/`

### 4. Playback core

* [ ] Playback-session state model
* [ ] Bounded packet and frame queues
* [ ] Cancellation and generation-based invalidation
* [ ] Seeking
* [ ] Audio/video clock and synchronization
* [ ] Buffering and end-of-stream behavior
* [ ] Track selection and switching
* [ ] Recovery from source, decoder, audio, and graphics failures

Documentation: `docs/subsystems/playback/`

### 5. Graphics and display integration

* [x] One-device ownership model for the current presentation domain
* [ ] Factory-selected graphics-device domain and backend contract
* [x] Windows D3D11 QRhi integration
* [x] Redirected Qt Quick rendering integration
* [ ] Final video, subtitle, and UI compositor (the explicit video/UI boundary
  and narrow final pass exist; subtitles do not)
* [x] Windows extended-linear HDR and SDR swapchain presentation
* [ ] macOS EDR and SDR swapchain presentation
* [ ] Linux HDR and SDR swapchain presentation
* [x] Shared presentation and display-state model
* [x] Windows multiple-display and window-movement observation
* [x] Windows dynamic display-property notifications
* [x] Windows display adapter
* [ ] macOS display adapter
* [ ] Linux display adapter
* [x] Graphics-device loss and recreation

Documentation: `docs/subsystems/graphics/`

### 6. libplacebo video rendering

* [ ] Shared rendered-video producer contract
* [ ] Backend target-interop contract and observable copy/fallback paths
* [ ] Persistent libplacebo renderer lifecycle
* [ ] Effective FFmpeg metadata mapping
* [ ] Software-frame upload path
* [ ] Hardware-frame importer abstraction
* [ ] D3D11 importer
* [ ] Vulkan, VAAPI, and DRM PRIME importer
* [ ] VideoToolbox, IOSurface, and MoltenVK importer
* [x] Offscreen HDR render-target contract and temporary QRhi producer
* [ ] Display-target and SDR-white updates
* [ ] HDR10, HLG, HDR10+, and Dolby Vision capability reporting
* [ ] Quality and energy profiles
* [ ] Rendering diagnostics and copy detection

Documentation: `docs/subsystems/video-rendering/`

### 7. Audio

* [ ] Audio-output backend selection
* [ ] Audio decoding and resampling
* [ ] Bounded PCM buffering
* [ ] Real-time-safe device callback
* [ ] Volume and mute
* [ ] Device changes and recovery
* [ ] Audio-backed master clock

Documentation: `docs/subsystems/audio/`

### 8. Subtitles

* [ ] Subtitle timeline
* [ ] libass integration
* [ ] Plain-text subtitle handling
* [ ] Embedded fonts and attachments
* [ ] Bitmap subtitle rendering
* [ ] Authored and user-controlled positioning
* [ ] Scaling across resolutions and display densities
* [ ] Subtitle luminance in HDR
* [ ] Control-overlay avoidance

Documentation: `docs/subsystems/subtitles/`

### 9. User interface

* [ ] Thin application shell and page structure
* [ ] Player page with truthful session states
* [ ] Retained HDR Lab diagnostics page
* [ ] Generic active video-viewport contract
* [ ] Open-file and drag-and-drop interface
* [ ] Play and pause controls
* [ ] Seek bar and timestamps
* [ ] Jump backward and forward
* [ ] Audio-track selection
* [ ] Subtitle-track selection
* [ ] Volume and mute
* [ ] Fullscreen
* [ ] Loading, buffering, and error presentation
* [ ] Keyboard shortcuts
* [ ] Minimal settings surface
* [ ] Playback-pipeline diagnostics view

Documentation: `docs/subsystems/ui/`

### 10. Reliability and diagnostics

* [ ] Structured subsystem errors
* [ ] Pipeline capability and fallback report
* [ ] Queue, buffering, and stall telemetry
* [ ] Decode and render timing
* [ ] Frame-drop and audio-underrun reporting
* [ ] CPU and GPU copy reporting where detectable
* [ ] Active graphics-adapter reporting
* [ ] Recovery diagnostics

Documentation: `docs/subsystems/diagnostics/`

### 11. Testing

* [x] Testing principles and staged subsystem plan
* [x] CTest and Qt Test structure
* [x] Presentation-target policy tests
* [x] Rendered-surface description and invalidation tests
* [x] Real D3D11 QRhi compositor capture smoke test
* [ ] Deterministic first-frame and playback scenarios
* [ ] Playback and seeking tests
* [ ] Color-metadata normalization tests
* [ ] Renderer image tests
* [ ] Subtitle layout tests
* [ ] Hardware decode and frame-import integration tests
* [ ] Display-change and multi-monitor tests
* [ ] Unreliable-source and cancellation tests
* [ ] Cross-platform performance and power measurements
* [ ] Physical HDR and A/V output verification

Documentation: `docs/TESTING.md` and `docs/subsystems/testing/`

### 12. Build and packaging

* [ ] Dependency and feature discovery
* [ ] Reproducible FFmpeg, libplacebo, and libass integration
* [ ] Windows packaging
* [ ] macOS packaging
* [ ] Linux packaging
* [ ] Runtime capability reporting for optional dependency features

Documentation: `docs/subsystems/build/`

## Project documentation

```text
AGENTS.md
PLAN.md

docs/
    ARCHITECTURE_NOTES.md
    DEFERRED.md
    TESTING.md

    decisions/
        README.md
        0001-application-owned-qrhi-composition.md
        0002-extended-linear-srgb-presentation.md
        0003-display-targeted-video-surface.md
        0004-cross-platform-graphics-domain-and-video-interop.md

    research/
        README.md
        2026-07-28-testing-tools-and-boundaries.md

    subsystems/
        application/
            README.md

        build/
            README.md

        graphics/
            README.md
            PLAN.md

        testing/
            README.md
            PLAN.md

        ui/
            README.md

        video-rendering/
            README.md
```

Subsystem folders and subsystem plans should be created when enough concrete architecture or active work exists to justify them.

## Documentation roles

* `PLAN.md` tracks high-level scope and progress.
* `docs/ARCHITECTURE_NOTES.md` contains broad technical context and possibilities.
* `docs/TESTING.md` defines project-wide testing principles, boundaries, and
  coverage tiers.
* Subsystem `README.md` files describe the current accepted architecture.
* Subsystem `PLAN.md` files track detailed active work where useful.
* `docs/decisions/` records significant architectural decisions.
* `docs/research/` records investigations and experiments.
* `docs/DEFERRED.md` records known issues and intentionally postponed work.
