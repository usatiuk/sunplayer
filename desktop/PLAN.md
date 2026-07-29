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

As of 2026-07-29, the repository contains a Windows presentation foundation
and a first visible media-opening slice, not yet continuous playback:

* A factory-selected graphics-device domain owns the current D3D11 QRhi,
  same-device libplacebo GPU, and device generation; the presentation engine
  owns its window swapchain.
* Qt Quick renders through `QQuickRenderControl` into an application-owned
  RGBA16F texture.
* A thin QML application shell defaults to a truthful Player page and keeps the
  retained HDR Lab reachable through simple top-level navigation. The active
  page publishes a generic root-coordinate video viewport with explicit
  visibility. HDR Lab uses libplacebo by default and can select the retained
  procedural QRhi producer for diagnostic comparison; that switch is not a
  playback fallback or player preference.
* Both diagnostic producers render the same grayscale, color-spectrum, and
  stepped pattern through the shared source, producer, and target lifecycle
  contracts. The QRhi implementation uses its temporary diagnostic shader.
  The libplacebo implementation models a changing software frame with one
  persistent 640×360 RGBA32F input texture and buffer, one observable CPU
  upload per changed input frame, cached reuse for target-only rerenders, and
  GPU scaling through a persistent renderer. Its CPU work and allocation size
  do not grow with the window.
* On Windows, libplacebo directly wraps the QRhi-owned RGBA16F D3D11 texture,
  shares QRhi's immediate context without an output copy, and normalizes its
  internal 203-nit linear convention to Sunroom's active-reference-white
  surface convention.
* A narrow final QRhi pass places that already processed video surface,
  combines it with the UI, and presents extended-linear sRGB/scRGB when
  available, with an SDR fallback. It can also compose UI without an active
  video layer.
* Qt screen state, QRhi swapchain HDR information, and Windows Advanced Color
  telemetry feed a shared presentation snapshot and diagnostic UI.
* Diagnostics distinguish the producer input path and its CPU transfers from
  output-target GPU copies, output CPU transfers, synchronization, and fallback
  reason.
* Rendering is demand-driven outside explicit animation, and the graphics
  foundation handles resize, output changes, surface destruction, swapchain
  invalidation, and bounded device recovery.
* The configured Windows Debug target builds successfully with Qt 6.11.1,
  MSVC, pinned D3D11-only libplacebo 7.360.1, and official minimal FFmpeg
  8.1.2 dependencies built through the project-local vcpkg configuration.
* `MediaSession` opens a local file and decodes its first video frame on a
  cooperatively cancellable worker. Nonzero playback generations reject stale
  completions, and ordinary open/import/render failures become visible session
  errors instead of terminating the process.
* A stable active-source router switches the presentation engine between the
  Player's decoded source and HDR Lab at a render boundary. The Player fits the
  decoded display aspect ratio inside its viewport and shows the resulting
  paused first frame through the production libplacebo path.
* Pinned RGB and compressed BT.709 limited-range YUV fixtures cross real FFmpeg
  demux/decode and libplacebo upload. The YUV fixture is a three-frame
  Matroska/FFV1 stream with non-square pixels and known signal/timing values.

libass, continuous decoding, audio, playback scheduling, drag-and-drop,
subtitles, and persistence are not integrated. CTest/Qt Test coverage exists for pure
presentation-target policy, video-viewport state, the real QML shell's
viewport publication and diagnostic renderer selection, rendered-video surface
validity/invalidation, decoded-frame ownership, the libplacebo and FFmpeg
binary boundaries, and real D3D11 offscreen QRhi/libplacebo
producer/compositor capture. The GPU capture covers SDR
targets at 80, 100, and 203 nits and a target-relative PQ diagnostic at 100 and
203 nits, plus known pixels from the first FFmpeg-decoded frame at two SDR-white
targets. A sustained headless probe also exercises 60 animated 640×360 frames
into a 1100×600 target without viewport-sized CPU generation.
Whole-application scenarios, hardware decode, a fixed mastered PQ source moved
between targets, and physical-output validation do not yet exist.

The next media slice makes the Windows graphics domain own a video-capable,
multithread-protected D3D11 device, supplies that same device to FFmpeg,
implements direct D3D11VA NV12/P010 plane import, and proves zero CPU transfers
against a representative H.264 or HEVC fixture. Playback prefers that path
when supported while keeping software decode observable and functional.
Continuous packet/frame queues and scheduling then replace the one-frame open
operation without changing the Player/source/compositor contracts. Playback
uses libplacebo as its video renderer; the procedural producer remains
HDR-Lab-only diagnostic tooling.

## Subsystems

### 1. Application shell

* [x] Basic application and presentation-window lifecycle for the current
  Windows diagnostic shell
* [ ] Settings and persistence
* [x] Local file dialog and optional positional command-line open
* [ ] Drag-and-drop and platform file associations
* [ ] Top-level logging and non-media error integration

Documentation: `docs/subsystems/application/`

### 2. Media sources and file loading

* [x] Cancellable local-file first-frame open
* [x] Generation invalidation for superseded first-frame opens
* [ ] Continuous-source cancellation and timeout model
* [ ] FFmpeg AVIO integration
* [ ] Bounded read-ahead and byte caching
* [ ] Unreliable or blocking source isolation model
* [ ] Clear source-stall and recovery states

Documentation: `docs/subsystems/media-io/`

### 3. FFmpeg media integration

* [x] Official minimal FFmpeg dependency and runtime deployment
* [x] Retained decoded-frame ownership/timing/storage contract
* [x] Asynchronous first local-file frame demux/decode integration
* [x] Initial container open and best-video-stream probing
* [ ] Stream, chapter, and attachment discovery
* [ ] Packet demuxing
* [ ] Video decoding
* [ ] Audio decoding
* [ ] Subtitle decoding
* [ ] Hardware-device capability discovery
* [ ] Metadata and timestamp normalization

Documentation: `docs/subsystems/media/`

### 4. Playback core

* [x] Initial Empty/Opening/Ready/Error session state model
* [ ] Bounded packet and frame queues
* [x] First-frame open cancellation and generation invalidation
* [ ] Seeking
* [ ] Audio/video clock and synchronization
* [ ] Buffering and end-of-stream behavior
* [ ] Track selection and switching
* [ ] Recovery from source, decoder, audio, and graphics failures

Documentation: `docs/subsystems/playback/`

### 5. Graphics and display integration

* [x] One-device ownership model for the current presentation domain
* [x] Factory-selected graphics-device domain and backend contract
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

* [x] Pinned D3D11-only dependency and installed-configuration/lifecycle
  verification
* [x] Shared rendered-video producer contract
* [x] Direct QRhi target lifecycle and output-path diagnostic schema
* [x] Native D3D11 libplacebo direct-target interop and observable zero-copy
  path
* [ ] Same-device GPU-copy and explicit CPU target fallbacks
* [x] Persistent libplacebo GPU and renderer lifecycle
* [ ] Effective FFmpeg metadata mapping
* [x] Deterministic software-backed RGBA32F diagnostic upload path
* [x] FFmpeg software-frame importer and persistent upload reuse
* [x] Shared hardware-frame import result/diagnostic contract
* [ ] D3D11 importer
* [ ] Vulkan, VAAPI, and DRM PRIME importer
* [ ] VideoToolbox, IOSurface, and MoltenVK importer
* [x] Offscreen HDR render-target contract and temporary QRhi producer
* [x] Display-target and SDR-white updates
* [ ] HDR10, HLG, HDR10+, and Dolby Vision capability reporting
* [ ] Quality and energy profiles
* [x] Initial rendering-path and copy/transfer diagnostics

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

* [x] Thin application shell and page structure
* [x] Player page with truthful first-frame session states
* [x] Retained HDR Lab diagnostics page
* [x] Generic active video-viewport contract
* [x] Open-file interface
* [ ] Drag-and-drop interface
* [ ] Play and pause controls
* [ ] Seek bar and timestamps
* [ ] Jump backward and forward
* [ ] Audio-track selection
* [ ] Subtitle-track selection
* [ ] Volume and mute
* [ ] Fullscreen
* [x] First-frame loading and media error presentation
* [ ] Continuous buffering presentation
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
* [x] Pinned libplacebo dependency and public-API lifecycle test
* [x] Real D3D11 QRhi compositor capture smoke test
* [x] Real D3D11 libplacebo SDR/PQ surface and compositor capture
* [x] Deterministic RGB and compressed-YUV FFmpeg first-frame scenarios
* [x] First-frame session cancellation and stale-generation tests
* [ ] Deterministic playback scenarios
* [ ] Playback and seeking tests
* [ ] Color-metadata normalization tests
* [ ] Renderer image tests
* [ ] Subtitle layout tests
* [ ] Hardware decode and frame-import integration tests
* [ ] Display-change and multi-monitor tests
* [x] Cooperative first-frame cancellation tests
* [ ] Unreliable-source and mounted-filesystem containment tests
* [ ] Cross-platform performance and power measurements
* [ ] Physical HDR and A/V output verification

Documentation: `docs/TESTING.md` and `docs/subsystems/testing/`

### 12. Build and packaging

* [x] Project-local vcpkg manifest, pinned baseline, and Windows dependency
  triplet
* [x] Reproducible D3D11-only libplacebo dependency integration
* [x] Reproducible FFmpeg integration
* [ ] Reproducible libass integration
* [ ] Cross-platform libplacebo dependency configurations
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
        0005-retain-ffmpeg-frames-at-the-decoded-frame-boundary.md
        0006-asynchronous-media-session-and-stable-active-video-source.md

    research/
        README.md
        2026-07-28-testing-tools-and-boundaries.md
        2026-07-29-ffmpeg-windows-dependency-and-frame-import.md
        2026-07-29-compressed-sdr-fixture.md

    subsystems/
        application/
            README.md

        build/
            README.md

        graphics/
            README.md
            PLAN.md

        media/
            README.md

        playback/
            README.md

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
