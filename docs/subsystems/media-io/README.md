# Media input and source buffering

## Current status

The production session currently accepts only a local `file:` URL and passes
its filesystem path to FFmpeg. One playback generation owns one
`AVFormatContext`; audio and video are never opened or parsed independently.
The FFmpeg interrupt callback observes cancellation, but a mounted-filesystem
read blocked inside the operating-system kernel may remain uninterruptible.

After demuxing, selected audio and video packets share a bounded router with a
default limit of 8,192 packets and 512 MiB. This is generous forward buffering,
but it is not a source-byte cache and cannot hide a read that remains blocked
after the reserve drains. Decoded video remains deliberately limited to three
frames because hardware frames reserve decoder surfaces; it must not be
enlarged to solve source jitter. The production PCM queue can retain up to 30
seconds of decoded audio while the encoded router holds the interleaved video
packets needed to read that far ahead.

## Accepted direction

Add remote sources incrementally and preserve the single-pass media operation:

1. Replace the filesystem-only request field with a small media-input value
   containing a locator, safe display label, and narrowly typed FFmpeg open
   options such as authentication headers and timeouts. Credentials must not
   be embedded in diagnostics, recent-file state, or ordinary URL logging.
2. First let FFmpeg own local-file, HTTP, HTTP range, and HLS protocol behavior.
   A resource is seekable only when the resulting FFmpeg `AVIOContext` reports
   normal seeking; an HTTP URL is not assumed seekable merely because it uses
   HTTP.
3. Make encoded-packet buffering observable and tune it from real sources.
   Prefer a bounded byte budget plus useful media-duration watermarks over a
   large decoded-frame queue. Enter a clear Buffering/source-stalled state when
   the available horizon drains instead of freezing unrelated UI work.
4. Add an application-owned `AVIOContext` only when measurements demonstrate a
   need for controlled byte read-ahead, caching, retry, token refresh, or
   stronger cancellation than FFmpeg's native protocols provide.

The future custom-AVIO path is an implementation detail at the existing FFmpeg
open boundary:

```text
local, HTTP, or service-backed reader
    -> bounded memory/disk byte cache
    -> FFmpeg AVIO read/seek callbacks
    -> one AVFormatContext
    -> existing packet router and audio/video decoders
```

It does not create a second demux operation or alter decoded-frame, audio,
clock, renderer, or compositor contracts. Start with one concrete native-URL
implementation; do not introduce a generic source-plugin framework before a
second implementation supplies real lifecycle and policy differences.

## Seeking and recovery

FFmpeg maps a timestamp seek through the container index to the underlying
source. A stable HTTP resource with byte-range support can therefore seek by
issuing a new ranged request. HLS and server-generated streams may seek by
playlist segment or by replacing the server stream at a requested timestamp
instead; that behavior remains source-specific around the same normalized
playback request.

SunPlayer currently reopens and reprobes the source for generation-scoped seek,
hardware fallback, and graphics recovery. This keeps invalidation simple but
can be expensive for remote media. Optimize ordinary remote seeking only after
measurement, either with an ownership-safe in-place demux seek or a
service-aware stream replacement. Hardware or graphics recovery must not
silently duplicate network readers.

## Possible Jellyfin integration

A Jellyfin client is exploratory product scope rather than a V1 commitment.
The server-specific layer would own authentication, library browsing, playback
capability negotiation, progress reporting, and transcode-session lifetime.
It would select a direct-play, remuxed, or transcoded locator and pass one
sanitized media-input request into the normal SunPlayer pipeline.

FFmpeg should initially handle the selected HTTP file or HLS stream. Custom
AVIO is not required merely because the URL came from Jellyfin. It becomes
justified only by measured needs such as application-owned caching, expiring
credentials, or source recovery that FFmpeg's protocol layer cannot express
cleanly. Track and subtitle selection are separate existing player gaps and
must be implemented before claiming a complete Jellyfin playback experience.

## Verification priorities

* Verify that the pinned, packaged FFmpeg build exposes the required HTTP,
  HTTPS/TLS, range, and HLS protocol/demux support before advertising it.
* A throttled, bursty seekable HTTP fixture through the real one-open A/V path.
* A slow network-share scenario that proves bounded forward progress,
  Buffering entry/recovery, cancellation, and responsive UI state.
* Seek and reopen accounting showing that one generation has one active source
  reader and that credentials never appear in logs.
* Duration/byte occupancy and source-read timing diagnostics before choosing
  default cache sizes.
* A custom-AVIO test only when that implementation exists; do not emulate a
  network stack in unit tests preemptively.
