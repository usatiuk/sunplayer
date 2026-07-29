# 0006: Open media asynchronously behind a stable active-video source

* Status: Accepted
* Date: 2026-07-29
* Extended by: [0007](0007-bound-continuous-video-and-select-on-presentation-thread.md)

## Context

The first FFmpeg integration decoded one frame synchronously in a test. A real
Player page must not block the GUI while opening or probing a file, must reject
results from superseded opens, and must turn ordinary media failures into
session errors rather than process termination.

The presentation engine also owns graphics resources for one
`RenderedVideoSource` reference. Replacing that reference directly when the UI
switches between Player and HDR Lab would complicate signal lifetime, producer
teardown, and render-boundary ordering.

## Decision

`MediaSession` is the GUI-thread owner of the initial playback state:
`Empty`, `Opening`, `Ready`, or `Error`. Its first implementation owns one
persistent worker that runs a cooperatively cancellable operation to open a
local file, discover the selected video stream, and publish one immutable
`DecodedVideoFrame`.

Each open receives a nonzero playback generation. A completion may mutate the
session only when its generation is still current and the session remains
`Opening`. Cancellation is not an error. FFmpeg receives the cancellation
token through an `AVFormatContext` interrupt callback plus explicit decode-loop
checks. Only immutable retained frame state crosses back to the GUI thread.
Cancel or replacement requests stop without joining from the GUI thread.
At most the newest pending open is retained; it begins after any current
operation returns. Final session destruction requests stop and joins the
worker so no completion can outlive the session.

`ActiveVideoSource` remains bound to the presentation engine and delegates to
the session's decoded source or HDR Lab's diagnostic source. It owns monotonic
content and producer-configuration revisions, ignores inactive-source updates,
and forces producer replacement when the active route changes.

QML synchronization and active-source selection complete before the engine
refreshes the producer for a frame. This prevents one frame from combining the
new page viewport with the old page's producer.

Decoded-media import or render unavailability is reported through the active
source to `MediaSession::Error`. The Player viewport becomes hidden and the
process remains alive. Unsupported HDR Lab paths and genuine deployment or
graphics invariants are not silently replaced with a fake player renderer.

## Consequences

* File opening and decoding do not block the GUI thread.
* The continuous decoder publishes selected frames through the same
  presentation mailbox without changing the compositor or source router.
* Clearing a decoded source advances its producer configuration so a hidden
  producer releases any retained mapped `AVFrame`.
* Cancel and replacement return immediately, but an operation that ignores
  cancellation delays the latest pending open. Final shutdown must still join
  it. Kernel-level mounted-filesystem hangs may require helper-process
  containment.
* ADR 0007 evolves the first session into bounded continuous video playback.
  Audio clocking, seeking, track selection, and unified buffering remain
  outside this decision.

## Alternatives considered

### Decode from QML or the presentation window

Rejected because canonical session state, cancellation, and media ownership do
not belong to a page or native event adapter.

### Give the presentation engine a mutable raw source pointer

Rejected because connection lifetime, producer replacement, and page/render
ordering become implicit. The stable router makes those transitions explicit.

### Keep media-path failures fatal

Rejected because arbitrary user media is untrusted capability input. An
unsupported format or failed import is an actionable session error, not an
application invariant violation.

### Build continuous playback immediately

Rejected for this slice. A truthful paused first frame proves file opening,
session state, source switching, YUV rendering, and UI integration before
queues, scheduling, and audio are introduced.
