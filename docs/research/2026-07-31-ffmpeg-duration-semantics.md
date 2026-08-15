# FFmpeg duration semantics on a normalized timeline

## Question

The synchronized fixture stores selected packets from 5 to 8 seconds. FFmpeg
8.1.2 reports `AVFormatContext.start_time = 5 s`, `duration = 8 s`,
`duration_estimation_method = AVFMT_DURATION_FROM_STREAM`, and no selected
`AVStream.duration`. Should SunPlayer report 3 or 8 seconds after normalizing the
selected-media origin to zero?

## Local source evidence

The exact vcpkg-pinned FFmpeg 8.1.2 source was inspected.

* Generic `libavformat/demux.c::update_stream_timings()` calculates stream end
  as `start + duration` and derives a context duration as `end - start`. It only
  replaces an already populated context duration when that field is unknown.
* `estimate_timings_from_pts()` likewise stores per-stream spans after
  subtracting the stream start. `AVFMT_DURATION_FROM_PTS` therefore does not
  mean the public context field is an absolute endpoint.
* The Matroska muxer instead accumulates its Segment Duration as
  `max(packet_timestamp + packet_duration)`. With the fixture's five-second
  timestamp offset it writes eight seconds.
* The Matroska demuxer copies that Segment Duration directly into
  `AVFormatContext.duration`. The generic `has_duration()` path then labels the
  estimate `AVFMT_DURATION_FROM_STREAM`, even though both selected stream
  durations remain unknown.

The three public fields do not disambiguate these cases. A conforming demuxer
can report start 5 and a true ten-second duration for packets spanning 5 to 15;
subtracting start would incorrectly truncate playback to five seconds.

## Accepted consequence

SunPlayer treats FFmpeg's open-time duration as provisional and never subtracts
`start_time` from it. The one-pass decoder observes normalized selected-stream
endpoints while it is already decoding:

```text
video end = normalized PTS + authoritative frame duration
audio end = normalized PCM start + exact converted sample count / rate
```

After every selected primary stream reaches EOF with an observed endpoint, the
maximum endpoint replaces the provisional value and the duration is marked
final. If any selected endpoint is unavailable, the header estimate stays
provisional. This requires neither a second source open nor a tail probe.

The fixture therefore exposes eight seconds during open/decode and finalizes
to three seconds at EOF. A genuine stream spanning 5 to 15 finalizes to ten
seconds. Unselected subtitle or data tails cannot inflate the result.

An exact pre-play duration would require an explicit index or tail-discovery
operation. SunPlayer does not add that extra I/O to ordinary playback.
