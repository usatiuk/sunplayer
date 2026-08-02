# Embedded subtitle pipeline research

Date: 2026-08-01

## Question

How should Sunroom add selectable text and bitmap subtitles without opening the
media twice, inventing a second clock, or coupling subtitle decoding to QML?

## Current-system evidence

* One playback generation owns one `AVFormatContext`, selected audio/video
  packet queues under one aggregate budget, independent decoder workers, and a
  shared normalized timeline.
* `MediaSession` already publishes the canonical media position: presented
  audio when available, otherwise the existing monotonic fallback.
* The final shader currently blends a display-targeted video texture and the
  redirected Qt Quick texture. Subtitle composition does not exist.
* The installed FFmpeg 8.1.2 build includes the common text and bitmap subtitle
  decoders, including ASS/SSA, SubRip, WebVTT, mov_text, PGS, DVD, and DVB.
* libass is described by the project architecture but is not yet a manifest
  dependency. The official vcpkg port is cross-platform and supplies FreeType,
  FriBidi, and HarfBuzz, with Fontconfig on Linux.

## Library contracts

FFmpeg's public subtitle API returns `AVSubtitleRect` values of three relevant
kinds:

* `SUBTITLE_ASS`: an authoritative ASS/SSA event line.
* `SUBTITLE_TEXT`: authoritative plain UTF-8 text.
* `SUBTITLE_BITMAP`: indexed bitmap data, an RGB32 palette whose valid size is
  `nb_colors`, authored rectangle coordinates, and optional forced-event flags.

`AVSubtitle::pts` is expressed in `AV_TIME_BASE`; `start_display_time` and
`end_display_time` are millisecond offsets from that timestamp. The decoded
subtitle and all rectangles must be copied or consumed before
`avsubtitle_free()`.

Bitmap streams are stateful. FFmpeg's PGS decoder uses `UINT32_MAX` when a
composition has no explicit end and emits an empty composition to clear the
previous display state. A correct shared boundary therefore cannot require a
finite end time for every subtitle.

libass owns text shaping and ASS layout. Its intended streaming path is:

```text
ass_new_track
ass_process_codec_private
ass_add_font for eligible attachments
ass_process_chunk for decoded events
ass_render_frame at the current media time
```

The renderer returns an ordered list of positioned alpha masks and colors. It
requires the output frame size, source storage size, pixel aspect, and font
provider to be configured before rendering. A seek or selected-track change can
replace the track instead of manually editing its event list.

Primary references:

* [FFmpeg 8.1.2 subtitle API](https://ffmpeg.org/doxygen/8.0/structAVSubtitle.html)
* [libass public API](https://github.com/libass/libass/blob/master/libass/ass.h)
* [vcpkg libass 0.17.5 port](https://vcpkg.io/en/package/libass.html)
* [mpv text subtitle implementation](https://github.com/mpv-player/mpv/blob/master/sub/sd_ass.c)
* [mpv subtitle behavior and limitations](https://mpv.io/manual/master/#subtitles)

## Production-practice result

Mature players keep text and image subtitle rendering distinct while presenting
them through one overlay contract:

* mpv converts non-ASS text to ASS events, feeds codec private data and embedded
  fonts into libass, and asks libass for images at the current playback time.
* Image subtitles stay images. User font/color styling does not generally apply
  to PGS/DVD/DVB because their appearance is authored into the pixels.
* Track selection and seeks flush or replace subtitle state. They do not create
  a subtitle-specific playback clock.
* Reconstructing a subtitle that began before a seek is format- and container-
  dependent. It is acceptable for a player to remain empty until a later cue or
  composition update instead of scanning historical media.
* ASS colors have historical compatibility complications. The conservative V1
  rule is to treat the composed subtitle surface as SDR sRGB/reference-white
  content, matching ordinary UI, and document that exact VSFilter color
  mangling is not yet a compatibility claim.

## Real-file Matroska compression finding

Manual acceptance against the UHD Dredd Matroska exposed a dependency feature
rather than a subtitle scheduling defect. Sunroom discovered and selected the
PGS stream, and the shared packet router delivered its packets, but the packet
payload entering `avcodec_decode_subtitle2()` began with zlib bytes (`78 DA`)
and produced no subtitle. The configured FFmpeg dependency reported
`--disable-zlib`; a system FFmpeg build with zlib decoded the same stream.

Matroska can apply zlib `ContentCompression` to a track before codec payloads
are stored. FFmpeg's Matroska demuxer only expands that encoding when built
with zlib support. The production fix is therefore to request the official
vcpkg FFmpeg port's `zlib` feature, not to add a private inflater, second probe,
packet loop, or subtitle queue policy.

The regression corpus includes a deterministic copy of the existing PGS
fixture muxed with zlib track compression by MKVToolNix 100.0. Its manifest
records the source fixture, exact command, tool archive hash, output hash, and
expected track content-encoding algorithm. The normal production decoder test
runs the same discovery, one-operation packet routing, FFmpeg decoding, and
bitmap assertions for both compressed and uncompressed containers. Reverting
the FFmpeg zlib feature makes the compressed row produce no subtitle events.

## Chosen direction

1. Add subtitle as a third selected stream in the existing packet router. Keep
   one aggregate packet budget and one demux operation.
2. Discover every embedded subtitle track during the existing probe. Default to
   Off; selecting a stream performs the existing generation-scoped restart at
   the current canonical position.
3. Copy decoded output into immutable Sunroom-owned data before freeing
   `AVSubtitle`:

   ```text
   SubtitleEvent
       normalized start
       optional finite end
       TextEvent (ASS event bytes)
       BitmapComposition (authored canvas + positioned RGBA regions)
       Clear
   ```

4. Retain codec private data, source storage geometry, language/title/default/
   forced/accessibility flags, and eligible font attachments from the same
   format context.
5. Let one presentation-owned renderer consume the selected generation's
   configuration and events at `MediaSession` time. libass handles text; bitmap
   compositions preserve their authored placement.
6. Upload one transparent subtitle texture only when its content or layout
   changes. Blend video, then subtitles, then QML UI in the existing final pass.
7. Treat subtitle decode/render failure as a subtitle failure, not a reason to
   stop valid audio/video playback. Continue draining or discarding selected
   subtitle packets so the aggregate router cannot deadlock.
8. Do not add subtitle-specific seeking, a historical scan, or a second reader.
   After the normal playback seek, consume subtitle packets that naturally
   arrive and become correct at the next cue or composition update.

## Scope decisions

The first production milestone includes embedded ASS/SSA, FFmpeg-converted text
formats, and FFmpeg-decoded bitmap formats; track Off/selection; embedded font
attachments; seeking; paused rendering; authored placement; and HDR-coherent
reference-white composition.

It deliberately defers external sidecar discovery, downloads, OCR, secondary
simultaneous tracks, live captions, forced-only filtering, and user style UI.
The decoded event boundary keeps later scale, vertical offset, opacity, and text
style preferences out of the media decoder. Bitmap recoloring is not planned.
An unusually long cue that began before a seek may be absent until its next
update. This keeps seeking bounded and preserves the single-reader design.
