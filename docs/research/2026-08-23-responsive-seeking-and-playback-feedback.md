# Responsive seeking and playback feedback

> Status: implemented and validated, 2026-08-23.

## Problem

Before this change, SunPlayer turned an ordinary seek into a modal-looking
state:

* Left/Right, the seek buttons, the scrubber, and play/pause are disabled while
  a seek is running.
* Every relative command starts a decode-generation replacement immediately,
  so several quick presses repeatedly reopen and cancel FFmpeg work.
* `MediaSession` clears the currently presented frame before the replacement
  generation produces its target frame, so the video area is temporarily
  black.
* Seeking and preparing use separate full-page layouts with different type
  sizes, while audio buffering pins the complete transport island onscreen.

This is interaction and presentation policy, not a demux-seek correctness
failure.

## Existing boundaries

ADR 0009 already supplies the difficult part: a new generation cancels the
active operation, the worker retains only the latest pending open request, and
old-generation callbacks and frames cannot become current. A seek issued while
another seek is slow is therefore already safe and latest-wins.

The old UI hid that capability with `session.seeking` guards. The session also
retains seekability, duration, selected tracks, and user play intent across
same-media generation replacement. No second seek worker, in-place FFmpeg flush
protocol, or generalized state machine is needed.

`DecodedVideoSource` owns the presented frame through a shared reference, but
that reference is not the whole presentation state. The decoded producer also
caches the mapped source frame, libplacebo mapping, and reusable native import
resources. `clearFrame()` is the established invalidation boundary: it drops
the source frame and advances the producer-configuration revision so the
presentation engine recreates that producer. Skipping it carried the whole
old presentation/import state across a new decoder generation. Retaining
selected tracks, timeline state, and user intent across the restart remains
safe; retaining the decoded producer state does not.

## Interaction model

Relative keyboard and button commands use one short trailing coalescing window:

```text
Right at 10:00.000  -> desired 10:10, start timer
Right at 10:00.080  -> desired 10:20, restart timer
Right at 10:00.150  -> desired 10:30, restart timer
quiet at 10:00.330  -> dispatch one seek to 10:30
```

The pending destination is visible immediately. Each burst retains one fixed
origin and one signed accumulated delta; the displayed and dispatched target
is the clamped sum. Opposite directions therefore cancel even while the live
playback clock advances, and boundary clamping does not distort the burst's
net direction.

The timer batches input; it does not wait for decode completion. If another
burst occurs during a slow seek, its final destination dispatches after the
same short quiet period and supersedes the active generation. Explicit scrubber
press cancels a pending relative burst; release identifies one destination and
dispatches immediately.
Native key-repeat events join the same batch, so holding Left/Right remains
useful without creating one decoder restart per repeat.

The transient pending relative target belongs in `PlayerPage`, alongside the
existing pressed-slider preview. Canonical position, duration, active-seek
state, and playback intent remain in `MediaSession`.

## Presentation and control model

* An ordinary user seek clears the old decoder-backed frame before the decoder
  generation is replaced. The viewport remains active on its normal black
  background while `seeking` stays true until a replacement frame is selected
  (or the generation cleanly ends/fails).
* A clean current-generation seek that admits no video frame completes seeking
  without leaving stale video or feedback latched.
* Play/pause remains available during a seek and changes the play intent used
  by the replacement generation. If its audio epoch is already open, the
  current generation is started or paused under the existing audio lifecycle
  lock; an obsolete epoch is never touched.
* Relative seek, the seek buttons, and the scrubber remain available during a
  slow seek. Volume and mute remain visible based on the selected audio track,
  not the transient lifetime of the rebuilt audio output. Menus and window
  commands remain ordinary controls.
* Pending/active seek and Ready-without-video use one subtle backed overlay: a
  visible spinner above readable, consistently sized text. Seeking reports the
  desired destination and keeps spinning while a replacement frame remains
  pending, including when a future decoded frame is not yet due.
* Initial opening remains a distinct cancellable state because no prior media
  frame exists.
* Initial Ready-without-video uses the same compact activity treatment.
* Buffering retains the frame and shows only a subtle spinner. Buffering no
  longer summons or pins the transport island.
* A presentation failure during a seek enters the existing typed fallback or
  terminal-error path. It is not ignored merely because the replacement
  generation is still Opening.

## Implementation checklist

* Add one page-local relative-seek burst origin/net delta and a 180 ms
  single-shot timer. Route native Left/Right (including repeat) and both
  transport buttons through it; keep slider release immediate and cancel any
  pending relative target first.
* Clear the pending burst when Player becomes hidden, the session becomes
  inactive/non-seek Opening, seekability disappears, or an explicit absolute
  seek is dispatched. A timer must not cross media/track/session replacement.
* Remove `session.seeking` input guards from relative seek, scrubber, and
  play/pause paths. Let Space remain available for a seeking Player session.
* Let `MediaSession::play()` and `pause()` update intent while an ordinary seek
  is opening and reconcile only the matching replacement audio epoch under its
  existing lifecycle lock. Read the atomic intent once when opening an epoch.
* Clear `DecodedVideoSource::currentFrame()` at every decoder-generation
  restart, including an ordinary user seek, before the old decoder context can
  be destroyed.
* Keep the viewport active during seeking even though no frame is drawable.
* Replace the large seeking layout with compact consistent activity feedback;
  add the standalone buffering spinner and stop pinning transport for buffering.
* Update the accepted seek/UI documentation rather than adding a new state
  hierarchy.
* Keep `m_seeking` true through Ready-without-replacement-frame and clear it in
  the existing current-generation selection/end path. Accept presentation
  failures during that state.

## Acceptance tests

* Three rapid relative commands produce no immediate seek and one final seek to
  the fixed-origin accumulated target; opposite directions cancel and
  start/end clamping remains correct. Tests flush the product dispatch function
  deterministically instead of sleeping around the 180 ms threshold.
* A new relative burst and a scrubber request can supersede an active slow seek;
  pressing the scrubber cancels an undelivered relative timer before it can fire.
* Extend the existing supersession test to prove the old frame is cleared when
  the first seek starts, remains absent through a replacement seek, and only a
  current-generation frame can become visible.
* A future target keeps `seeking` true without a frame until its first frame
  becomes selectable. A clean zero-admission seek and a seek-time presentation
  failure finish through their existing paths.
* Pause and resume intent can change after the replacement audio epoch opens;
  the sink and eventual Ready state honor the latest intent.
* QML keeps seek/play and selected-track volume controls available, exposes the
  pending destination, keeps the empty viewport active, and does not pin
  transport merely because audio is buffering.
* A pending timer is cleared by page/session deactivation and cannot dispatch
  later. Native Space is accepted during seeking; held-arrow repeat is accepted
  into the same relative batch.
* Preparing, seeking, and buffering activity indicators use the intended subtle
  presentation without changing opening/error behavior.

## Validation

The first implementation retained the old presented frame during seek. Real
hardware playback then exposed moving green/purple corruption after even one
seek. A hardware-only A/B on the same Debug executable and affected Arrival
file isolated the regression to commit `2259453`: restoring the pre-change
frame clear removed the corruption, while the safe D3D11VA copy path from
`801012c` remained enabled. This disproved an exploratory open-GOP/preroll
hypothesis and required no FFmpeg seek-policy, Dolby Vision, color-pipeline, or
software-decode change.

The corrected implementation restores that frame-and-producer invalidation at
the generation boundary. The A/B establishes this architectural lifetime error;
it does not distinguish which cached D3D11 or driver object produced the visible
garbage. Focused media-session and QML-shell tests pass, QML lint is clean, and
the rebuilt Debug player was manually validated on the affected hardware decode
path with both an ordinary seek and repeated relative seeks. All 36 registered
Debug CTests then completed without failure, including QRhi composition and the
real D3D11VA safe-import and P010 cases.

## Non-goals

Do not replace generation-scoped restart, keep FFmpeg contexts alive, add seek
threads, predict thumbnails, add configurable debounce timing, or construct a
generic asynchronous-command framework. Network seek latency and persistent
demux-context optimization remain separate measured work.
