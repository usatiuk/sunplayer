# Cross-Platform HDR Video Player Plan

## Goals

* **Cross-platform by design.** Support Windows, macOS, and Wayland-based
  Linux desktops while maximizing shared code and consistent behavior.
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
* X11 or XWayland presentation, color management, packaging compatibility, or
  fallback behavior. Linux desktop support targets native Wayland.
* Managed HDR or color-calibrated output on Wayland compositors without the
  required color-management-v1 capability set. Such native Wayland systems
  remain supported through an honest unmanaged, assumed-sRGB SDR mode.
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

As of 2026-08-02, the repository contains Windows D3D11 and native-Wayland
Vulkan presentation foundations plus continuous local-file playback:

* A factory-selected graphics-device domain owns the current D3D11 QRhi,
  same-device libplacebo GPU, FFmpeg D3D11VA device, shared immediate-context
  execution guard, and device generation; the presentation engine owns its
  window swapchain.
* Qt Quick renders through `QQuickRenderControl` into an application-owned
  RGBA16F texture with the matching depth/stencil attachment required by its
  default depth-assisted 2D ordering.
* A thin QML application shell defaults to a video-first Player page and keeps
  the retained HDR Lab reachable from Player's empty state or overflow menu.
  Player publishes a full-page video viewport with a transient transport island
  and optional playback-statistics panel; HDR Lab has one return action. The
  active page publishes generic root-coordinate viewport geometry with explicit
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
  shares QRhi's immediate context without an output copy, and receives a
  reference-white-relative virtual target in its fixed 203-nit coordinate
  system. This construction is capture-validated for relative SDR, analytic
  and real mastered static PQ, and a characterized display-relative HLG
  response. Deterministic four-frame Main10 fixtures prove real FFmpeg decode
  and libplacebo rendering for PQ/HDR10, HLG, two-scene HDR10+, and Dolby
  Vision Profile 8.1 without a second media operation. The importer reports
  retained source facts, usable HDR10+ scene metadata, and whether Dolby Vision
  reshaping was mapped. Source HDR values remain unchanged and no custom post-
  map normalization runs. The renderer explicitly selects libplacebo
  7.360.1's spline tone mapper and perceptual gamut mapper and disables inverse
  mapping, peak detection, and dithering; that policy is visible in
  diagnostics. HLG support is display-relative and does not claim absolute-
  reference monitoring, while broader dynamic-HDR profiles and physical output
  accuracy remain validation work.
* A narrow final QRhi pass places that already processed video surface,
  combines it with the UI, and presents extended-linear sRGB/scRGB when
  available, with an SDR fallback. It can also compose UI without an active
  video layer.
* Qt screen state, QRhi swapchain HDR information, and an initial WinRT
  Advanced Color observer feed a shared presentation snapshot and diagnostic
  UI. Live Advanced Color changes already update SDR white and luminance;
  material target changes rerender a paused frame. ADR 0016's latest semantic
  target reconciliation is implemented: the rendered surface has no display
  revision surrogate, and native screen identity alone no longer recreates the
  swapchain.
* Diagnostics distinguish the producer input path and its CPU transfers from
  output-target GPU copies, output CPU transfers, synchronization, and fallback
  reason.
* Rendering is demand-driven outside explicit animation, and the graphics
  foundation handles resize, output changes, surface destruction, swapchain
  invalidation, and bounded device recovery.
* The configured Windows Debug target builds successfully with Qt 6.11.1,
  MSVC, pinned D3D11-only libplacebo 7.360.1, and official minimal FFmpeg
  8.1.2 dependencies built through the project-local vcpkg configuration.
* Native-Wayland Linux configures from system Qt 6.10, FFmpeg 8, libplacebo,
  cubeb, libass, Vulkan, Wayland, VA-API, and DRM packages. It now selects
  Wayland before Qt startup, inventories optional color/decorations
  capabilities, creates a Vulkan 1.3 QRhi-owned device imported by libplacebo,
  and presents software-decoded video through the shared direct RGBA16F target
  and redirected QML compositor. Missing managed-color capability selects
  unmanaged assumed-sRGB; a complete managed-SDR set selects Qt-declared
  gamma-2.2. One bounded WSLg run completes the unmanaged llvmpipe production
  path, fullscreen/restoration, Vulkan synchronization validation, and
  application teardown. Two other runs timed out waiting for cursor-state
  convergence, and remaining WSLg compositor diagnostics are recorded
  separately, so broader lifecycle acceptance remains in progress.
  All 26 Linux CTests and QML lint pass. Ubuntu's system cubeb selects WSLg's
  Pulse-compatible default route, and both the real sink lifecycle and
  audio-first application playback advance through the production cubeb clock.
  User-confirmed real-file playback is also audible through WSLg.
  Native PulseAudio/PipeWire-Pulse route-change and recovery evidence,
  preferred-target/HDR transitions, VAAPI/DRM PRIME, native GPU/display
  validation, and packaging remain pending.
* Qt remains the sole Wayland toplevel and surface owner. If xdg-decoration is
  absent, one modular QML chrome layer uses public Qt system move/resize/state
  operations and system-theme glyphs with bundled Lucide fallbacks. It
  overlays active video, fades its titlebar with playback chrome, and retains
  one media-independent physical-pixel inner outline around the complete
  client area;
  no libdecor, GTK, private negotiation listener, second surface, or external
  shadow path is introduced.
* `MediaSession` opens a local file and supports play, pause, seek, and replay.
  One shared media operation gives one `AVFormatContext` to the demux owner;
  selected audio and video packets use one count/byte budget and independent
  decoder workers, while the generation-scoped three-frame mailbox and bounded
  PCM sink propagate backpressure upstream.
* The production media operation provides single-pass A/V routing: one
  `AVFormatContext` opens, probes, seeks, and reads the source once; selected
  audio and video packets share one global count/byte budget; and independent
  decoder workers retain a common normalized timeline. Real FFmpeg FLAC decode
  and libswresample produce 48 kHz stereo interleaved float32 PCM for a bounded
  controlled sink with distinct submitted and presented cursors. Video-only
  and synchronized decoding feed one hardware-capable packet decoder. The
  session passes the active graphics capability without adding a parallel
  audio demux context. Hardware fallback intentionally restarts the entire
  playback generation with fresh shared contexts.
* One `CubebAudioSink` opens the system-default route on Windows and Linux. Its
  dedicated control thread owns the cubeb lifecycle and, on Windows, the COM
  MTA. Windows requests WASAPI; Linux lets the distribution cubeb package
  select its backend. The real-time callback consumes preallocated PCM,
  records bounded output-to-media mappings, and represents short underruns as
  hold silence. Cubeb and the operating-system sound service own ordinary
  default-route migration within one cubeb-stream epoch; Sunroom adds no
  parallel device watcher or backend policy.
* Playback exposes user play intent independently from audio interruption.
  Sustained hold-silence enters `Buffering`, preserves the last confident
  audio-master position, and keeps video frozen without switching to a
  monotonic fallback. Sustained loss of an established presentation clock is
  terminal until explicit audio-output epoch replacement is implemented.
* Frame selection uses integer FFmpeg timestamps, a clock-source-neutral
  `MediaClockSnapshot`, and a focused `VideoFrameScheduler`. Presented cubeb
  frames are the master for sources with audio; video-only playback uses the
  monotonic producer, and a monotonic tail continues if audio drains first.
  Leading source-audio gaps become timeline-advancing silence, while a future
  first video frame leaves the video layer empty until due. The presentation
  thread retains early
  frames, publishes due frames, collapses multiple due frames to the newest,
  keeps the drained final frame visible, and stops continuous rendering while
  paused, buffering, or ended.
* Nonzero playback generations reject stale frames, notifications, and
  completions. Ordinary open/decode/render failures become visible session
  errors; unsupported hardware-frame import triggers one observable software
  restart at the current logical position before becoming an error.
* On Windows, supported streams prefer hardware decoding on the graphics
  domain's video-capable D3D11 device. FFmpeg-owned NV12, P010, P012, and P016
  texture-array slices can be mapped directly into libplacebo plane views.
  The retained `AVFrame` reserves the decoder surface; the decode and render
  paths share explicit GPU-phase execution serialization and D3D11 multithread
  protection. Unsupported or failed hardware decoding reopens through the
  software decoder and records the reason. Graphics-device recreation
  supersedes every active old-generation pipeline and re-decodes from the
  captured position against the replacement capability. Software frame
  storage remains generation-independent, but the session still restarts so
  future seeks and fallback cannot retain stale graphics capability.
* A stable active-source router switches the presentation engine between the
  Player's decoded source and HDR Lab at a render boundary. The Player fits the
  selected frame's display aspect ratio inside its viewport, renders through
  production libplacebo, and reports decoded, queued, selected, and dropped
  frame counts.
* Pinned RGB, FFV1, and H.264 fixtures cross real FFmpeg demux/decode and
  libplacebo rendering. The H.264 scenario proves an actual D3D11VA frame,
  direct NV12 plane import, zero input CPU transfers, zero input GPU copies,
  zero output copies/transfers, and tolerant agreement with the software-decode
  result.

Physical audio-device replacement, general source buffering, drag-and-drop, and
persistence are not integrated. Embedded subtitle discovery, selection,
FFmpeg decode, libass/bitmap rendering, and final composition are integrated.
CTest/Qt Test coverage exists for pure
presentation-target policy, video-viewport state, the real QML shell's
viewport publication and diagnostic renderer selection, rendered-video surface
validity/invalidation, decoded-frame ownership, the libplacebo and FFmpeg
binary boundaries, and real D3D11 offscreen QRhi/libplacebo
producer/compositor capture. The GPU capture covers SDR
targets at 80, 100, and 203 nits and holds one fixed analytic 1000-nit PQ signal
against one physical 600-nit target at all three reference-white levels. It
proves unchanged surface-relative output while the source fits, highlight
compression when available headroom falls below the source, and one final
Windows scRGB scale. It also covers known pixels from the first
FFmpeg-decoded frame at two SDR-white targets. The real HEVC corpus adds
static-PQ patch values, HLG target response, frame-local HDR10+ scene
progression, and mapped Dolby Vision Profile 8.1 reshape checks. A sustained
headless probe exercises 60 animated 640×360 frames into a 1100×600 target
without viewport-sized CPU generation. A bounded Windows application scenario
opens a pinned audio-first fixture through production FFmpeg and Cubeb, observes two
distinct video content revisions at the swapchain, and requires continued live
presented-audio clock progress. The QML component scenario separately protects
the ready-without-frame viewport invariant. Playback-owned coarse selection
also drains bounded video queues when the window or active page does not
request rendering. Broader whole-application command/error scenarios, SDR/HDR
range and profile coverage, actual display gamut propagation,
P010/P012/P016 capture, cross-platform hardware import, and physical-output
validation do not yet exist.

Playback exposes normalized position/duration and seeking through a
generation-scoped, keyframe-anchored decoder restart shared by user seek,
hardware-import fallback, and graphics-device recovery. Audio decode,
resampling, default-WASAPI output, presented-audio clocking, and synchronized
session cancellation share that generation. Playback uses libplacebo as its
video renderer; the procedural producer remains HDR-Lab-only diagnostic
tooling.

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

* [x] Cancellable continuous local-file open/demux/decode
* [x] Generation invalidation for superseded pipeline work
* [ ] Small media-input request for local or remote locators, safe display
  label, authentication headers, and FFmpeg open options
* [ ] FFmpeg-native HTTP/range/HLS input before application-owned networking
* [ ] Network-source cancellation and timeout model
* [ ] Observable duration/byte-aware encoded-packet read-ahead
* [ ] Custom FFmpeg AVIO byte caching only when native protocol behavior is
  insufficient
* [ ] Unreliable or blocking source isolation model
* [ ] Clear source-stall and recovery states

Documentation: `docs/subsystems/media-io/`

### 3. FFmpeg media integration

* [x] Official minimal FFmpeg dependency and runtime deployment
* [x] Retained decoded-frame ownership/timing/storage contract
* [x] Asynchronous continuous local-file video demux/decode integration
* [x] Initial container open and best-video-stream probing
* [ ] Stream, chapter, and attachment discovery
* [x] Bounded selected-video packet demuxing
* [x] Continuous video decoding
* [x] Initial single-pass selected-audio routing, decoding, and resampling
* [x] Embedded text and bitmap subtitle decoding in the shared media operation
* [x] Initial Windows D3D11VA device capability and decoder negotiation
* [x] Initial selected-video duration and timestamp normalization

Documentation: `docs/subsystems/media/`

### 4. Playback core

* [x] Initial Empty/Opening/Ready/Error session state model
* [x] Bounded packet and frame queues
* [x] Continuous-pipeline cancellation and generation invalidation
* [x] Generation-scoped local-video seeking
* [x] Monotonic no-audio clock and timestamp-driven frame selection
* [x] Clock-source-neutral media snapshot and video scheduler boundary
* [x] Initial presented-audio clock and audio-master video synchronization
* [x] Staggered A/V starts, clean zero-audio seek intervals, final audio
  endpoints, and bounded live-clock-loss handling
* [x] Initial audio-underrun buffering and terminal clock-loss handling
* [ ] General source buffering behavior
* [x] Initial end-of-stream behavior
* [ ] Track selection and switching
* [x] Initial position-preserving hardware-import and graphics recovery
* [ ] Recovery from source, decoder, and audio failures

Documentation: `docs/subsystems/playback/`

### 5. Graphics and display integration

* [x] One-device ownership model for the current presentation domain
* [x] Factory-selected graphics-device domain and backend contract
* [x] Windows D3D11 QRhi integration
* [x] Redirected Qt Quick rendering integration
* [x] Final video, subtitle, and UI compositor
* [x] Windows extended-linear HDR and SDR swapchain presentation
* [ ] macOS EDR and SDR swapchain presentation
* [x] Wayland Linux Vulkan SDR presentation foundation
* [ ] Wayland Linux managed HDR swapchain presentation
* [x] Shared presentation and display-state model
* [x] Initial Windows window-movement and Advanced Color observation
* [x] Initial Windows dynamic Advanced Color notification
* [x] Semantic display-target reconciliation without native-identity-driven
  surface invalidation or swapchain churn
* [ ] Optional Windows raw display capability diagnostics where renderer or
  support tooling has a concrete consumer
* [ ] macOS display adapter
* [x] Initial Wayland startup capability and SDR-surface selection adapter
* [ ] Wayland preferred-target and managed-HDR display adapter
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
* [x] Retained final FFmpeg frame as source-color truth, with only the
  evidenced stream-level HDR10+ fallback
* [x] Deterministic software-backed RGBA32F diagnostic upload path
* [x] FFmpeg software-frame importer and persistent upload reuse
* [x] Shared hardware-frame import result/diagnostic contract
* [x] D3D11VA NV12/P010/P012/P016 direct importer
* [x] Vulkan direct libplacebo output target
* [ ] VAAPI and DRM PRIME hardware-frame importer
* [ ] VideoToolbox/IOSurface importer and macOS Metal/MoltenVK interop
* [x] Offscreen HDR render-target contract and temporary QRhi producer
* [x] Display-target and SDR-white updates
* [x] Analytic reference-white-adaptive SDR/static-PQ display mapping without
  a post-map video scale
* [x] Real FFmpeg-decoded static-PQ fixture and retained-metadata validation
* [ ] Actual display-gamut propagation
* [x] HLG target-response validation through the shared FFmpeg/libplacebo
  path, without a Sunroom-authored HLG pipeline
* [x] Production FFmpeg/libplacebo mapping acceptance, validation, and
  capability diagnostics for SDR, HDR10/PQ, HLG, HDR10+, and Dolby Vision as
  required for V1
* [ ] Quality and energy profiles
* [x] Initial rendering-path and copy/transfer diagnostics

Documentation: `docs/subsystems/video-rendering/`

### 7. Audio

* [x] Audio-output backend direction and pinned cubeb dependency
* [x] Initial real FFmpeg audio decoding and libswresample conversion
* [x] Bounded controlled PCM buffering
* [x] Real-time-safe Windows and Linux cubeb callback and default-route
  lifecycle
* [x] Production session output and presented-audio master clock
* [x] Session-lifetime volume and mute at the audio-output boundary
* [x] Delegate ordinary default-device switching to cubeb's null-device stream
* [ ] Device-error recovery and native route-change validation

Documentation: `docs/subsystems/audio/`

### 8. Subtitles

* [x] Subtitle timeline
* [x] libass integration
* [x] Plain-text subtitle handling
* [x] Embedded fonts and attachments
* [x] Bitmap subtitle rendering
* [x] Authored positioning
* [ ] User-controlled positioning, scale, style, and delay
* [x] Scaling authored subtitle geometry to the video viewport
* [x] Subtitle luminance in HDR
* [ ] Control-overlay avoidance

Documentation: `docs/subsystems/subtitles/`

### 9. User interface

* [x] Thin application shell and page structure
* [x] Player page with truthful continuous-video session states
* [x] Retained HDR Lab diagnostics page
* [x] Generic active video-viewport contract
* [x] Open-file interface
* [ ] Drag-and-drop interface
* [x] Play and pause controls
* [x] Seek bar and timestamps
* [x] Jump backward and forward
* [ ] Audio-track selection
* [x] Subtitle-track selection
* [x] Volume and mute
* [x] Fullscreen
* [x] Continuous video loading and media error presentation
* [x] Initial audio-buffering status presentation
* [ ] General source-buffering presentation
* [x] Initial F11/Escape fullscreen shortcuts beyond Space play/pause
* [ ] Minimal settings surface
* [x] Playback-pipeline diagnostics view

Documentation: `docs/subsystems/ui/`

### 10. Reliability and diagnostics

* [x] Qt category logging with bounded default session files
* [x] Initial generation-scoped open/seek causal trace
* [ ] Structured subsystem errors
* [ ] Pipeline capability and fallback report
* [ ] Queue, buffering, and stall telemetry
* [ ] Decode and render timing
* [x] Initial frame-drop and audio-underrun reporting
* [x] Initial audio-buffering transition logging
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
* [x] Continuous session cancellation and stale-generation tests
* [x] Initial deterministic video playback scenario
* [x] Generation-scoped video-seek and decoded-preroll tests
* [x] Initial one-pass A/V decode, resample, sink, and seek scenario
* [x] Audio-master playback and deterministic A/V seek/drain tests
* [x] Real staggered-start A/V, no-presentation-consumer, hidden sink-failure,
  and clock-loss regression scenarios
* [ ] Real-device output A/V seek, recovery, and physical-sync tests
* [ ] Representative source-metadata and library-mapping tests
* [ ] Renderer image tests
* [x] Initial subtitle decode, rendering, layout, and UI tests
* [x] Windows H.264 D3D11VA decode and zero-copy frame-import integration test
* [ ] Display-change and multi-monitor tests
* [x] Cooperative pipeline and queue cancellation tests
* [ ] Unreliable-source and mounted-filesystem containment tests
* [ ] Cross-platform performance and power measurements
* [ ] Physical HDR and A/V output verification

Documentation: `docs/TESTING.md` and `docs/subsystems/testing/`

### 12. Build and packaging

* [x] Project-local vcpkg manifest, pinned baseline, and Windows dependency
  triplet
* [x] Reproducible D3D11-only libplacebo dependency integration
* [x] Reproducible FFmpeg integration
* [x] Pinned Windows cubeb dependency integration
* [x] Reproducible libass integration
* [x] Root-level Windows/Linux GitHub Actions build, QML-lint, and
  capability-honest CTest workflow configured; first hosted run pending
* [ ] Cross-platform libplacebo dependency configurations
* [ ] Windows packaging
* [ ] macOS packaging
* [ ] Wayland Linux packaging and runtime requirements
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
        0007-bound-continuous-video-and-select-on-presentation-thread.md
        0008-reference-white-adaptive-hdr-display-mapping.md
        0009-generation-scoped-seek-restart.md
        0010-qt-category-logging-and-bounded-session-files.md
        0011-single-pass-media-routing-and-audio-output-boundary.md
        0012-use-final-decoded-frames-as-color-evidence.md
        0013-rely-on-system-display-calibration.md
        0014-prefer-native-metal-presentation-on-macos.md
        0015-wayland-only-linux-desktop.md
        0016-reconcile-output-changes-semantically.md
        0017-require-wayland-color-management-v1.md
        0018-support-unmanaged-srgb-wayland-sdr.md

    research/
        README.md
        2026-07-28-testing-tools-and-boundaries.md
        2026-07-29-libplacebo-windows-dependency-build.md
        2026-07-29-ffmpeg-windows-dependency-and-frame-import.md
        2026-07-29-compressed-sdr-fixture.md
        2026-07-29-ffmpeg-continuous-decode-and-backpressure.md
        2026-07-30-reference-white-adaptive-hdr-mapping.md
        2026-07-30-ffmpeg-keyframe-seek-and-restart.md
        2026-07-30-large-network-matroska-seek-observability.md
        2026-07-31-chatgpt-audio.md
        2026-07-31-ffmpeg-duration-semantics.md
        2026-07-31-cubeb-wasapi-device-recovery.md
        2026-08-01-color.md
        2026-08-01-pinned-color-source-verification.md
        2026-08-01-video-audio-switch.md
        2026-08-01-display-audio-migration-reconciliation.md

    subsystems/
        application/
            README.md

        build/
            README.md

        graphics/
            README.md
            PLAN.md

        video-rendering/
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
