# Media input and source buffering

## Current behavior

`MediaInput` carries a local-file or HTTP(S) locator and a safe display name.
The internal locator may contain credentials; public metadata returns a local
file URL or the generic `media:remote`. The normal QML/file-open action remains
local-file-only; the Jellyfin adapter supplies remote inputs through the typed
C++ boundary. Every seek, track change, retry, replay, hardware fallback and
graphics recovery retains the input rather than reconstructing it from the
public URL. Optional initial position uses the same normalized seek path.

One playback generation owns one `AVFormatContext`; selected audio/video and
subtitles share that demux operation. FFmpeg owns HTTP/range/TLS. Local opens
allow only `file`; remote opens allow `http,https,tcp,tls`, require certificate
verification, and set a 15-second read timeout. Cancellation uses the existing
interrupt callback. FFmpeg diagnostics redact remote URLs and authorization at
the library callback, including worker-thread messages. Remote custom auth
headers are not supplied, preventing them from crossing redirect origins.

Seekability comes from the opened FFmpeg resource, not its URL scheme. Seeking
reopens/reprobes and seeks to container keyframe preroll against the discovered
shared timeline origin. Nonseekable resume fails explicitly; Retry from start
uses zero position. One logical item survives seek/recovery decoder generations.

The packet router retains the existing 8,192-packet/512-MiB budget. PCM can
retain 30 seconds; decoded video remains limited to three frames because GPU
frames reserve decoder surfaces. Audio underrun uses the existing Buffering
interruption. Remote video-only starvation freezes its monotonic clock when the
last available frame ends and resumes when data returns or decode drains.
There is no extra source-byte cache or reader.

## Limits and deferred direction

A mounted-filesystem read blocked in the kernel can remain uninterruptible.
Reopen-based remote seeking may be expensive. Custom AVIO, byte read-ahead,
disk cache, reconnect/credential-refresh retries, HLS and duration-aware packet
budget changes require demonstrated needs and separate scope. They must fit the
existing one-demux operation rather than introduce a parallel media pipeline.

[Jellyfin](../jellyfin/README.md) owns login, library navigation, Direct Play
resolution and reports outside playback. Original-file Direct Play is the only
accepted service behavior; remux/transcoding/external subtitles remain deferred.

## Verification

`jellyfin` uses real FFmpeg against loopback HTTP/range edges for exact initial
resume, cooperative stalled-open cancellation, video-only starvation/recovery,
credential isolation across redirects, failed-open log redaction, and rejection
of a loopback untrusted TLS server. `ffmpeg-dependency` requires HTTP/HTTPS/TCP/TLS
protocols in the installed dependency. Existing local media/session tests remain.
Real-server, native Windows/macOS TLS and slow-share acceptance remain open;
see [testing](../../TESTING.md) and the
[delivery plan](../../plans/cross-cutting/2026-10-18-jellyfin-client.md).
