# Subtitle subsystem

## Status

Embedded subtitle playback is implemented and tracked by
[Embedded subtitle playback](../../plans/subtitles/2026-08-01-embedded-subtitle-playback.md).
The Player exposes Off and the embedded tracks discovered by the existing
single FFmpeg operation. Selected ASS/text subtitles render through libass;
FFmpeg-decoded bitmap subtitles render from owned RGBA regions.
The same production sources compile on Linux against system libass 0.17.4 and
on macOS against the pinned vcpkg dependency. Platform-neutral subtitle state,
FFmpeg decode, and real libass embedded-font tests pass on both. On Apple
M2/macOS 26, the production Metal subtitle renderer additionally passes ASS
animation/font/route restoration, bitmap replacement/clear, viewport
placement, and generation-failure recovery. A Linux QRhi subtitle-surface
capture still needs to be added before claiming GPU rendering equivalence on
that backend.

Global subtitle appearance preferences are also implemented. A native Settings
dialog applies them live to the current cue, persists validated values, and
restores authored behavior by default. Text styling uses libass selective
dialogue overrides; size and bottom-to-top position are independently
optional. Bitmap subtitles keep authored pixels while whole-composition scale,
vertical position, and compositor opacity remain available.

## Responsibility

The subtitle subsystem owns:

* embedded subtitle-track descriptions and selection state;
* FFmpeg subtitle decoder output copied into immutable SunPlayer-owned events;
* text/ASS configuration, event delivery, embedded fonts, and libass lifetime;
* bitmap composition state and authored canvas placement;
* resolving subtitle state against the canonical playback clock;
* producing a transparent SDR/reference-white subtitle surface;
* reporting subtitle-specific diagnostics and nonfatal failures.

It does not own demux I/O, playback time, video tone mapping, physical display
calibration, QML layout, or a general media-source/plugin framework.

## Intended boundaries

```text
single FFmpeg demux operation
        |
        +-- selected video packets -> video decoder
        +-- selected audio packets -> audio decoder
        +-- selected subtitle packets -> FFmpeg subtitle decoder
                                          |
                                          v
                                immutable subtitle events
                                          |
canonical MediaSession clock ------------+
                                          v
                         text: libass / bitmap: authored regions
                                          |
                                          v
                              transparent subtitle surface
                                          |
                                          v
                              video -> subtitle -> QML UI
```

Track discovery describes all embedded subtitle streams. Off is a real
selection. Changing the selected stream uses the same generation-scoped restart
as seeking and starts at the current canonical media position; it never opens a
parallel reader for subtitles.

The selected track belongs to the media session, not to one decoder generation,
so seek, replay, hardware fallback, and graphics recovery retain it. New media
and cancellation reset selection to Off. SunPlayer does not scan backward just to
reconstruct a cue that began before a seek; the layer becomes correct when the
next subtitle cue or bitmap composition arrives.

One generation retains accepted events in an amortized copy-on-write sequence.
Event count and retained payload bytes, bitmap regions and bytes, embedded-font
bytes, raster dimensions, and GPU upload size are bounded. Exceeding an event,
payload, bitmap, or render budget disables that subtitle generation with a
nonfatal diagnostic; excess embedded fonts are skipped and libass may use its
normal fallback fonts. Decoder or renderer failure makes the subtitle layer
transparent without stopping audio or video. Returning to the Player route
rerasterizes the current paused subtitle rather than relying on an earlier GPU
surface.

Text appearance and authored events stay separate. `SubtitleSettings` advances
a raster revision for style, size, and position edits, so retained text events
and unchanged bitmap compositions rerender at the current paused or playing
time without changing decoded timing. Overall opacity is compositor-only and
does not invalidate the CPU raster or GPU texture identity.

Subtitle RGB is scaled to `0.8` in linear light while its authored alpha and
edge coverage remain unchanged. Ordinary UI and SDR reference white remain
`1.0`. Subtitles are composed after libplacebo has display-mapped video and
before the one platform presentation conversion. The platform remains
responsible for final display calibration.

## Current limits

* Embedded tracks only; external sidecars, downloads, and server subtitle APIs
  are deferred.
* One selected subtitle track at a time.
* No user font, subtitle delay, forced-only, or control-overlay avoidance
  controls. Text/background/edge color, component opacity, size, vertical
  position, and overall opacity are implemented.
* ASS rendering uses decoded video storage geometry. Non-square-pixel ASS script
  geometry is not yet separately communicated to libass.
* Bitmap pixels keep their authored colors and relative region geometry; a
  viewer-selected whole-layer scale or vertical position can transform the
  final composition. Exact VSFilter color compatibility is not attempted.
* A seek does not reread prior subtitle history to reconstruct a cue that began
  before the seek.

## Validation

Checked-in Matroska fixtures cross the production FFmpeg operation with native
ASS, SubRip converted by FFmpeg, an embedded test font, and PGS bitmap
compositions. The PGS coverage runs against both ordinary packets and a real
Matroska track using zlib `ContentCompression`, protecting the FFmpeg feature
required by real-world UHD files. Manual acceptance confirmed visible embedded
PGS subtitles on an UHD Matroska that had previously delivered compressed
bytes to an FFmpeg build without zlib.
Tests cover discovery and selected-stream routing, normalized timing,
embedded-font delivery, multi-region bitmap color and placement,
open-ended/replace/clear behavior, nonfatal subtitle-output rejection while
audio/video drain, generation-scoped selection and seek, the production
libass/bitmap renderer, failure latching, scaled/offset video-viewport
placement, live paused-cue color/scale/position invalidation, route restoration
at a paused clock, compositor-level opacity and final video-subtitle-UI layer
order, persistence/reset, and the dynamic QML track/settings menu.
