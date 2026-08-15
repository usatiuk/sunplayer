# Embedded subtitle playback

Status: Complete

## Goal

Ship selectable embedded text and bitmap subtitles through SunPlayer's real
single-pass playback and presentation paths.

Completion means:

* Player exposes Off and every supported embedded subtitle track.
* Selecting a track displays subsequently decoded cues against the same clock
  used for video scheduling.
* ASS/SSA styling, positioning, shaping, and embedded fonts render through
  libass.
* FFmpeg-decoded PGS/DVD/DVB-style bitmap compositions retain their pixels,
  authored canvas placement, replace behavior, and clear behavior.
* Pause, seek, replay, close, rapid selection, and generation replacement cannot
  display stale subtitles.
* Subtitle white maps to `0.8` of working/reference white in SDR and HDR output.
* Audio/video playback remains valid when a subtitle stream is absent, malformed,
  unsupported, or disabled.
* The source is never opened concurrently or parsed a second time for subtitles.

## Grounded starting behavior

The detailed evidence and library contracts are recorded in
[Embedded subtitle pipeline research](../../research/2026-08-01-embedded-subtitle-pipeline.md).

The relevant production facts at plan approval were:

* `decodeMediaFrames()` owns one `AVFormatContext`, selects one video and
  optional audio stream, and routes their packets through one aggregate budget.
* A generation-scoped restart already implements open, seek, replay, hardware
  fallback, and graphics recovery. It is the simplest correct track-switch
  boundary today.
* `MediaSession` already publishes the canonical presented-audio or monotonic
  media position. Subtitle timing must consume that value rather than maintain
  another timer.
* The final QRhi shader blended display-targeted video and redirected QML. It
  had no subtitle input.
* The pinned FFmpeg build has common text and bitmap subtitle decoders. The
  official vcpkg libass port existed but was not in the manifest.

## Fixed design

### One media operation and selection

Add a third `Subtitle` destination to `FfmpegPacketRouter`. Stream discovery
returns all embedded subtitle descriptors and the selected stream index.
Unselected subtitle and attachment streams are not packet-routed.

Default selection is Off. `MediaSession::selectSubtitleStream()` captures the
current canonical position and submits the normal same-media restart with the
requested stream index. Switching Off does the same, ensuring the no-longer-
selected worker and queued state disappear with the old generation. This costs
one intentional seek/reopen per user selection but never creates two live media
readers and does not introduce a new in-place flush protocol.

Selection is session state, not generation state. It survives seek, replay,
hardware fallback, and graphics recovery restarts for the same media. Opening a
different source or cancelling playback resets it to Off.

SunPlayer performs no subtitle-specific historical scan or preroll. After the
ordinary playback seek, the selected decoder consumes packets that naturally
arrive. A text cue or bitmap composition that began earlier may be absent until
the next update. That eventually-correct behavior is preferable to rereading a
large local or network file.

Track identity is the probed stream index within the current media. Descriptors
contain a stable display label plus language, title, codec, disposition flags,
and support status. Labels prefer title and localized language metadata, with a
deterministic `Subtitle N` fallback. Default, forced, hearing-impaired/SDH, and
commentary/description facts are shown when present. Explicit output-device-
style identity machinery is unnecessary.

### Decode boundary

Use the media-owned `EmbeddedMediaStreamDescriptor` for embedded track
discovery, then introduce the subtitle-specific immutable values under
`src/subtitles/`:

```text
SubtitleStreamConfiguration
    playback generation
    selected stream index
    codec private/header bytes
    source storage/canvas size
    eligible embedded font attachments

SubtitleEvent
    normalized start in integer microseconds
    optional finite end
    payload:
        AssTextEvent
        BitmapComposition
        Clear
```

`BitmapComposition` owns one or more positioned straight-RGBA regions copied
from FFmpeg's PAL8 indices and RGB32 palette before `avsubtitle_free()`. It also
owns the authored canvas size. Empty stateful bitmap output becomes `Clear`;
`UINT32_MAX` remains an open-ended composition rather than an enormous finite
duration.

Palette expansion honors `AVSubtitleRect::nb_colors`, native-endian
`0xAARRGGBB` palette words, bitmap stride, region dimensions, and palette-index
bounds. Invalid or overflowed dimensions, timestamps, strides, region counts,
or byte sizes skip that output with a nonfatal diagnostic.

The subtitle worker owns its `AVCodecContext`, uses
`avcodec_decode_subtitle2()`, and normalizes
`AVSubtitle::pts` plus display offsets against the operation's shared origin.
Native ASS and FFmpeg-converted text use authoritative `rect.ass` output and the
decoder's subtitle header. A rare authoritative `rect.text` without ASS output
is escaped into one ordinary default-style ASS event rather than rendered with
Qt text.

Font attachments are copied from the same probed format context when their
codec/MIME type is a supported font. They are never reopened from the source.
Malformed or excess attachments are ignored; libass may use its normal fallback
fonts.

A subtitle decoder failure is nonfatal to audio/video. The subtitle worker
reports the error once and continues draining/discarding its routed packets
until the operation ends so aggregate backpressure cannot strand the other
streams.

### Timeline and ownership

`MediaSession` accepts configuration/events only for its current playback
generation. A small thread-safe subtitle source owns the current configuration,
ordered events, content revision, and error. Generation replacement atomically
clears the prior source before the new operation can publish.

The existing video backpressure normally limits how far the shared demuxer can
read. Still, decoded bitmap bytes, retained event count and payload bytes,
attachment/font bytes, raster dimensions, and GPU upload size receive explicit
bounded budgets. One generation retains its accepted events until the next
ordinary playback restart. Event, payload, bitmap, or render-budget exhaustion
disables the selected subtitle path with a diagnostic instead of consuming
unbounded memory or failing audio/video playback. The font budget is best
effort: excess attachments are skipped. Unsupported or failed subtitle setup
drops that stream's packets without routing them into a queue that could block
demux.

No subtitle event mutates session position. Presentation resolves text and
bitmap state from the same `MediaClockSnapshot` used by the video scheduler.
Pause therefore freezes subtitles naturally; seek/replay/selection replace the
generation and construct fresh state.

### Text and bitmap rendering

Add a presentation-owned subtitle renderer:

* one libass library, renderer, track, and registered-font bundle per selected
  subtitle generation so attachments cannot accumulate across media;
* codec private data processed before events;
* eligible embedded fonts registered before renderer font configuration;
* system provider autodetection for fallback fonts;
* output frame size set to the displayed video rectangle;
* storage size derived from decoded video geometry; separate non-square-pixel
  ASS script geometry is deferred;
* no user style override in the first milestone, preserving authored ASS;
* ordinary FFmpeg-converted text receives a simple white outlined default style.

Decoded subtitle configuration/events remain CPU-owned and independent of the
graphics device. Device loss drops only raster/upload resources and can rebuild
them from current-generation semantic state.

libass images and bitmap compositions are rasterized into one transparent
premultiplied RGBA8 subtitle surface covering the presentation target. The CPU
surface is regenerated and uploaded only when event content, media-time-visible
state, video geometry, output size, or future subtitle settings change. Empty
content binds a transparent fallback texture.

`ass_render_frame` is sampled whenever canonical media time advances, even when
the decoded event set is unchanged, because ASS animations and karaoke can
change within one cue. libass's change signal avoids unnecessary uploads.

Bitmap authored coordinates scale from their canvas into the fitted video
rectangle. Aspect fitting, not the full application window, is their coordinate
reference. Bitmap pixels are not recolored. Unknown canvas dimensions fall back
to decoded video storage size.

### Composition and color

Extend the final compositor to bind video, subtitle, and QML textures and blend
in this order:

```text
display-targeted linear video
    -> SDR/sRGB subtitle surface, decoded to linear and alpha-composited
    -> SDR/sRGB QML surface, decoded to linear and alpha-composited
    -> one platform presentation conversion
```

Subtitle white is linear working value `0.8`. The subtitle layer is not passed
through libplacebo, a PQ transfer, or another tone mapper. The first milestone
treats ASS and bitmap RGB as SDR sRGB content; exact VSFilter color-mangling and
subtitle/video-colorspace compatibility are documented gaps, not hidden shader
heuristics.

The layer is enabled only while the Player's decoded-video source is active.
Navigating to HDR Lab or another route makes it transparent without destroying
current subtitle state. Rasterization or upload failure also produces a
transparent layer and a diagnostic; it cannot fail video presentation.

### Player UI

Add a `Subtitles` submenu to the existing transport overflow menu:

```text
Subtitles
    Off
    English
    English - Signs & Songs
    Czech - SDH
```

Exactly one entry is checked. The submenu remains present with a disabled
`No subtitles available` row when no tracks exist. Selection calls the session
command; QML does not own a duplicate selected-track value. The menu remains
part of the transport's existing visibility/cursor pinning behavior.

The initial UI intentionally has no color, font, position, scale, delay, forced-
only, or external-file controls. The immutable decoded-event boundary lets
future presentation settings rerender without re-decoding or changing timing.

## Implementation slices

1. Add the vcpkg libass manifest dependency and CMake target wiring. Do not run
   a separate configure; let the user's existing CLion configure consume the
   manifest update.
2. Add subtitle descriptors/events and extend real stream discovery, packet
   routing, attachment capture, FFmpeg decode, normalization, and nonfatal
   failure behavior.
3. Add `MediaSession` selection/state, current-generation source ownership,
   restart-at-current-position behavior, and typed diagnostics.
4. Add the persistent libass/bitmap renderer, texture lifetime, and final
   compositor layer.
5. Add the Player submenu and synchronize subsystem/root documentation.
6. Add real fixtures and behavioral coverage, validate the complete build, and
   review the result across behavior, architecture/simplicity, and evidence.

The slices form one coherent milestone. Use separate commits only if dependency
acceptance or a material decoder/rendering defect makes an intermediate
checkpoint useful.

## Behavioral validation

### Real fixture corpus

Create a small checked-in Matroska fixture through deterministic generation
commands containing the existing real video/audio shape plus:

* two text tracks with language/title/disposition differences;
* native ASS positioning/style and an attached redistributable test font;
* a plain text format converted by FFmpeg to ASS output;
* a bitmap track with multiple positioned regions, an open-ended display, and
  an explicit clear composition.

The bitmap elementary stream is produced by a small checked-in byte-level PGS
fixture generator under `tests/fixtures/media/`. The generator documents every
PCS/WDS/PDS/ODS/END segment, timestamp, palette entry, canvas, object position,
replacement, and clear; the generated bytes and SHA-256 are reviewed like
source. This is both deterministic and independent of FFmpeg's decoder, because
the pinned FFmpeg build intentionally has no PGS encoder.

Record source commands, licenses/provenance, SHA-256 hashes, timestamps, canvas
geometry, and expected visible states in fixture manifests. If one container
cannot represent all cases cleanly, use one text/attachment fixture and one
bitmap fixture; both must still traverse production FFmpeg demux/decode.

### High-value tests

* Real one-pass decode discovers the embedded tracks and routes the selected
  subtitle stream alongside audio/video.
* Native ASS and FFmpeg-converted SubRip preserve header data, normalized
  timing, non-ASCII text, authored events, and embedded font bytes.
* PGS output copies multiple regions, palette color/alpha, canvas placement,
  open-ended state, replacement, and clear events before FFmpeg storage is
  freed.
* `MediaSession` tests cover Off/track selection, restart at the canonical
  paused position, seek, and generation rejection.
* The production renderer test uses real libass and the checked-in test font,
  proves route off/on rerender at one paused time, and captures bitmap A -> B ->
  clear behavior.
* QRhi capture proves video -> subtitle -> UI order and subtitle alpha blending.
* QML integration proves dynamic Off/track entries and checked selection through
  the production track model.

These production-boundary tests are preferred over private parser unit tests.
Small state tests remain appropriate where an analytical oracle is stronger
than a golden image.

### Build and execution

After CLion has configured the manifest change:

* Build `sunplayer`, affected integration targets, QML lint, and the complete
  configured build through CLion's bundled CMake.
* Run registered tests only through CTest under the repository's required
  Visual Studio developer environment and staged runtime DLL policy.
* Do not launch the interactive application during automated validation.

## Documentation impact

On completion:

* Replace the planned status in `docs/subsystems/subtitles/README.md` with the
  accepted implementation and limits.
* Update media, playback, graphics/presentation, UI, build, testing, and root
  roadmap documents.
* Add a decision record only if implementation evidence changes the existing
  one-demux, generation-restart, or final-compositor decisions. The subtitle
  details themselves can remain in the subsystem document and this plan.

## Non-goals

* Opening or probing the same source through a parallel subtitle operation.
* External sidecar discovery/download and Jellyfin subtitle APIs.
* OCR or translating bitmap subtitles.
* Two simultaneous subtitle tracks.
* Live broadcast/teletext page-selection policy.
* User style/settings UI in the first milestone.
* Recoloring bitmap subtitles.
* Exact VSFilter color-mangling compatibility.
* X11 or XWayland support.
* A generic stream, renderer, overlay, or plugin framework.
* Immediate reconstruction of cues that began before a seek.

## Review and completion record

Before implementation, independent reviewers will examine:

* user-visible behavior, timing, selection, and failure recovery;
* architecture, ownership, simplicity, and single-pass I/O;
* FFmpeg/libass API evidence, fixture quality, and acceptance claims.

After implementation, the same lenses will review the actual diff and validation
evidence. Record substantive findings, fixes, final test results, remaining gaps,
and resulting commit subjects here before changing status to `Complete`.

Plan review on 2026-08-01 covered architecture/simplicity, user-visible
behavior, and evidence quality. It resulted in: session-scoped track selection;
generation-scoped libass/font lifetime; bounded resource contracts; animated
ASS clock sampling; route and failure gating; explicit bitmap replacement and
cleanup cases; palette/timestamp validation; a reviewed byte-level PGS fixture
generator; and render/upload fault injection. Historical subtitle reconstruction
after seek was deliberately rejected in favor of bounded, eventually-correct
playback. No plan blockers remain.

Implementation review covered behavior/correctness, architecture/simplicity,
and evidence/docs. The resulting fixes made the compositor tolerate a missing
subtitle texture, latched renderer failures to one playback generation, made
libass lazy for bitmap-only tracks, coalesced cue-driven presentation wakes,
localized track languages, preserved caller RC flags, bounded live-log
flushing, strengthened viewport/fixture evidence, and added a real
subtitle-output-failure regression that proves audio and video still drain.
A second review of those fixes consolidated the video/subtitle fallback into
one explicitly transparent compositor texture, preserved the first useful
subtitle failure reason, reset presentation-error deduplication after recovery,
and added renderer evidence for animated ASS sampling and clearing previously
visible content after failure. No additional behavior or ownership defect was
found.

Final validation on 2026-08-02:

* the complete configured Debug build succeeded;
* all 28 registered CTest scenarios passed;
* ordinary and Matroska-zlib-compressed PGS fixtures crossed the same
  production FFmpeg operation and bitmap assertions;
* an injected subtitle output rejection produced a nonfatal subtitle error
  while real audio and video reached EOS;
* the install tree contained the executable, libass, FreeType, FriBidi,
  HarfBuzz, zlib, cubeb, FFmpeg, libplacebo, and their required runtime
  dependencies;
* manual playback of a real UHD Matroska with a zlib-compressed embedded PGS
  track displayed subtitles after enabling `ffmpeg[zlib]`.

Remaining gaps are intentional scope, not blockers: external/server
subtitles, user style/position/delay settings, historical cue reconstruction
after seeks, exact VSFilter color behavior, non-square-pixel ASS script
geometry, and exhaustive synthetic allocation/corruption fault injection.
The resulting commit subject is `Add embedded subtitle playback`.
