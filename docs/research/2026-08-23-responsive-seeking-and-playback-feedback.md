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
  generation produces its target frame, making the video area black.
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

`DecodedVideoSource` owns the immutable presented frame through a shared
reference. Retaining that frame during an ordinary seek is lifetime-safe on the
same graphics device; the first selected frame from the new generation replaces
it normally. Graphics-device invalidation and presentation failure must still
clear the frame because their native resource may be unusable.

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

* An ordinary user seek retains the last presented frame and keeps the video
  viewport active until the target generation replaces it.
  `seeking` remains true until that replacement frame is selected (or the
  generation cleanly ends/fails), rather than clearing at stream discovery.
* A seek with no prior frame may still show the normal black background.
  A clean current-generation seek that admits no video frame also clears the
  retained old frame and completes seeking instead of leaving stale video and
  feedback latched.
* Play/pause remains available during a seek and changes the play intent used
  by the replacement generation. If its audio epoch is already open, the
  current generation is started or paused under the existing audio lifecycle
  lock; an obsolete epoch is never touched.
* Relative seek, the seek buttons, and the scrubber remain available during a
  slow seek. Volume, mute, menus, and window commands remain ordinary controls.
* Pending/active seek uses one compact overlay with consistent typography. It
  reports the desired destination; the spinner runs while a replacement frame
  remains pending, including when a future decoded frame is not yet due.
* Initial opening remains a distinct cancellable state because no prior media
  frame exists.
* Initial Ready-without-video uses the same compact activity treatment.
* Buffering retains the frame and shows only a subtle spinner. Buffering no
  longer summons or pins the transport island.
* A presentation failure from the retained frame during a seek clears that
  frame and enters the existing typed fallback or terminal-error path. It is
  not ignored merely because the replacement generation is still Opening.

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
* Preserve `DecodedVideoSource::currentFrame()` only for a user seek restart.
  Continue clearing it for new media, cancellation, errors, track/fallback
  restart, and graphics-device invalidation.
* Keep the viewport and black-background decision based on retained frame
  availability during seeking.
* Replace the large seeking layout with compact consistent activity feedback;
  add the standalone buffering spinner and stop pinning transport for buffering.
* Update the accepted seek/UI documentation rather than adding a new state
  hierarchy.
* Keep `m_seeking` true through Ready-without-replacement-frame and clear it in
  the existing current-generation selection/end path. Accept retained-frame
  presentation failures during that state.

## Acceptance tests

* Three rapid relative commands produce no immediate seek and one final seek to
  the fixed-origin accumulated target; opposite directions cancel and
  start/end clamping remains correct. Tests flush the product dispatch function
  deterministically instead of sleeping around the 180 ms threshold.
* A new relative burst and a scrubber request can supersede an active slow seek;
  pressing the scrubber cancels an undelivered relative timer before it can fire.
* Extend the existing supersession test to prove the exact retained frame
  survives both a blocked seek and its replacement, then is replaced only by
  the current generation. Existing cancellation/failure/recovery tests retain
  their negative clearing oracles.
* A future target keeps `seeking` and the retained frame until its first frame
  becomes selectable. A clean zero-admission seek and a seek-time presentation
  failure clear it.
* Pause and resume intent can change after the replacement audio epoch opens;
  the sink and eventual Ready state honor the latest intent.
* QML keeps seek/play controls enabled, exposes the pending destination, keeps
  the viewport visible with a retained frame, and does not pin transport merely
  because audio is buffering.
* A pending timer is cleared by page/session deactivation and cannot dispatch
  later. Native Space is accepted during seeking; held-arrow repeat is accepted
  into the same relative batch.
* Preparing, seeking, and buffering activity indicators use the intended subtle
  presentation without changing opening/error behavior.

## Validation

On 2026-08-23, the affected Debug targets built successfully, both QML lint
targets passed, and all 36 registered Debug CTests passed on Windows. The full
run included the real QRhi compositor and FFmpeg/D3D11VA first-frame tests.
Focused seek, shortcut, and QML-shell tests also passed after the final
edge-case corrections.

Independent final reviews found no remaining P0/P1 issue after verifying
scrubber ownership of pending input, fixed-origin boundary reversal, clean
zero-frame seek completion, and hardware-import fallback during an active
seek.

## Non-goals

Do not replace generation-scoped restart, keep FFmpeg contexts alive, add seek
threads, predict thumbnails, add configurable debounce timing, or construct a
generic asynchronous-command framework. Network seek latency and persistent
demux-context optimization remain separate measured work.
