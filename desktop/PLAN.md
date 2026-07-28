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

## Subsystems

### 1. Application shell

* [ ] Application and window lifecycle
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

* [ ] Shared graphics-device ownership model
* [ ] QRhi integration
* [ ] Qt Quick rendering integration
* [ ] Final video, subtitle, and UI compositor
* [ ] HDR, EDR, and SDR swapchain presentation
* [ ] Shared display-state model
* [ ] Multiple-display and window-movement handling
* [ ] Dynamic display-property notifications
* [ ] Windows display adapter
* [ ] macOS display adapter
* [ ] Linux display adapter
* [ ] Graphics-device loss and recreation

Documentation: `docs/subsystems/graphics/`

### 6. libplacebo video rendering

* [ ] Persistent libplacebo renderer lifecycle
* [ ] Effective FFmpeg metadata mapping
* [ ] Software-frame upload path
* [ ] Hardware-frame importer abstraction
* [ ] D3D11 importer
* [ ] Vulkan, VAAPI, and DRM PRIME importer
* [ ] VideoToolbox, IOSurface, and MoltenVK importer
* [ ] Offscreen HDR render target
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

* [ ] Core unit-test structure
* [ ] Playback and seeking tests
* [ ] Color-metadata normalization tests
* [ ] Renderer image tests
* [ ] Subtitle layout tests
* [ ] Hardware decode and frame-import integration tests
* [ ] Display-change and multi-monitor tests
* [ ] Unreliable-source and cancellation tests
* [ ] Cross-platform performance and power measurements

Documentation: `docs/subsystems/testing/`

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

    decisions/
        README.md

    research/
        README.md

    subsystems/
        <subsystem>/
            README.md
            PLAN.md
```

Subsystem folders and subsystem plans should be created when enough concrete architecture or active work exists to justify them.

## Documentation roles

* `PLAN.md` tracks high-level scope and progress.
* `docs/ARCHITECTURE_NOTES.md` contains broad technical context and possibilities.
* Subsystem `README.md` files describe the current accepted architecture.
* Subsystem `PLAN.md` files track detailed active work where useful.
* `docs/decisions/` records significant architectural decisions.
* `docs/research/` records investigations and experiments.
* `docs/DEFERRED.md` records known issues and intentionally postponed work.
