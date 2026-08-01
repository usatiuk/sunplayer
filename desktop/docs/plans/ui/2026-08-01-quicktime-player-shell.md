# QuickTime-inspired player shell

Status: Complete

## Goal

Replace the current permanent navigation, status, and timeline bars with a
quiet video-first Player page. During playback the movie fills the page and
the only normal chrome is a compact floating transport island that appears on
pointer activity and fades away when idle.

Completion means the production Player remains fully usable for open,
play/pause, seek, volume, mute, close, and HDR Lab access while no diagnostics
or navigation permanently reduce the video viewport.

## Grounded current state

* `PlayerPage.qml` currently reserves space above and below the video for a
  permanent session-status card and timeline card.
* `AppShell.qml` permanently reserves a 56-pixel navigation bar for Player and
  HDR Lab.
* Existing session commands and diagnostic properties already provide the
  behavior needed by the redesigned controls; no playback-core API is needed.
* The current QML shell integration test exercises the real page and session
  contract, but its viewport geometry and permanent-bar assumptions must be
  replaced.

## Chosen layout and behavior

* The active video viewport fills the entire Player page. Letterboxing remains
  the presentation layer's responsibility; QML does not draw a decorative
  frame around the movie.
* A rounded, dark translucent island floats at the bottom center with a compact
  maximum width and safe outer margin. It uses simple opacity animation,
  radius, and a subtle border; native blur, vibrancy, and platform-specific
  material APIs are out of scope.
* The island has two rows:
  * top: current time, seek timeline, and total duration;
  * bottom: mute/volume, seek backward 10 seconds, play/pause or replay, seek
    forward 10 seconds, and an overflow menu.
* While the timeline is pressed or dragged, the current-time label previews
  the slider target rather than continuing to show the playback clock. After
  release, the session's existing seek target and seeking state remain the
  source of truth; the page does not retain a second pending-seek model.
* Pointer movement reveals the island and restarts a short idle timer. It stays
  visible while playback is paused or ended, seeking is active, a control is
  being dragged, or its menu is open. A stationary pointer over the island
  does not pin it onscreen; it fades out during uninterrupted playback.
* Once that fade completes over a ready video frame, the pointer cursor hides
  with the chrome. It returns on movement and remains visible for empty,
  opening, preparing, paused, buffering, seeking, menu, slider, and statistics
  interaction states.
* The overflow menu contains Open another, Show playback statistics, HDR Lab,
  and Close video. The statistics action toggles one compact floating panel in
  the upper-right using the diagnostic values already exposed by
  `MediaSession`.
* Remove the permanent AppShell navigation. The empty Player state offers Open
  video plus a secondary HDR Lab action; HDR Lab provides a clear return-to-
  Player action. Opening, preparing, seeking, and error states remain centered
  overlays and do not create another surrounding layout.
* Space toggles play/pause when the Player page is active and no popup owns
  keyboard interaction. Moving or customizing the island is deferred.

## Implementation slices

1. Refactor AppShell navigation into explicit Player/HDR-Lab actions and make
   the Player viewport fill the shell.
2. Replace the permanent status/timeline bars with a transport island directly
   in `PlayerPage`; it has no second consumer that would justify another QML
   component boundary.
3. Add the optional playback-statistics overlay, pointer-driven reveal/fade
   behavior, and the shared overflow menu.
4. Remove superseded QML layout cruft and update the UI subsystem description
   to match the shipped shell.

## Validation

* Update the existing non-presenting QML shell integration test to verify the
  full-page viewport, Player/HDR-Lab navigation, menu actions, statistics
  toggle, play/pause, relative seek clamping, timeline seek, volume, mute,
  open, close, and resize behavior. Timeline coverage also verifies that the
  current-time label follows the scrubber preview while pressed.
* Exercise reveal behavior with synthetic pointer movement. Because the first
  interactive pass found that hover accidentally pinned the island, retain one
  bounded event-loop regression proving a stationary pointer over the controls
  still allows the real idle timer to fade them.
* Run QML lint, build the application and UI test target, then run the
  registered UI test through CTest.
* Manually confirm that the island is readable over bright and dark video,
  does not cover the center of the picture, and remains usable in a small
  resizable window.

## Non-goals

* Movable or user-configurable controls.
* Fullscreen, picture-in-picture, tracks, subtitles, thumbnails, or chapter UI.
* A reusable theme framework, platform-native material bridge, or copied
  QuickTime artwork.
* Moving playback diagnostics into the canonical session state; panel
  visibility is page-local UI state.

## Commit boundary

Ship the AppShell cleanup, transport island, statistics overlay, updated QML
integration coverage, and synchronized UI documentation as one coherent UI
change.

## Implementation checkpoint

The planned shell is implemented in the working tree. Player now owns a
full-page viewport, two-row transient transport, scrub-target time preview,
ten-second relative seeks, Space play/pause, overflow navigation, and optional
statistics panel. AppShell retains only page routing and HDR Lab's return
button. The one-use transport stayed inside `PlayerPage` rather than adding an
unnecessary component boundary.

## Validation performed

* The `sunroom_qmllint` target passed.
* CLion regenerated the final QML resources and rebuilt the production
  application after the icon-centering, cursor-policy, and license-notice
  polish. The user exercised that executable interactively.
* The focused `qml-shell` CTest passed after exercising pointer reveal, the
  real idle timeout with a stationary pointer over the island, packaged Lucide
  SVG loading, Space play/pause, scrub preview, relative and timeline seeks,
  full-page geometry, statistics, volume/mute, and page routing.
* The final focused QML pass also verifies equal transport-button centerlines,
  a visible cursor before the first frame, cursor hiding with idle playback,
  cursor restoration on movement, and the packaged QML module.
* The focused real-D3D11 compositor capture passed with pure-black uncovered
  and hidden-video output in both byte and extended-linear paths.
* All 24 registered CTest targets passed after the final icon, cursor, and
  seek-boundary regression changes.
* A three-lens read-only implementation review covered behavior/correctness,
  architecture/failure risk, and tests/docs/workflow. Its missing seek-clamp
  coverage was fixed. First-frame chrome timing was rejected as acceptable
  idle behavior, and focus-pinning was rejected because mouse-focused Play
  could otherwise keep the island visible indefinitely.
* `git diff --check` passed.

## Completion outcome

The interactive visual pass found and resolved four polish defects: the
stationary pointer pinned the island, text glyphs were unsuitable transport
icons, Qt stretched low-resolution icon rasters across the button, and mixed
button heights were top-aligned. The final controls use a pinned, licensed
Lucide SVG subset rasterized above display size, custom rounded
`AbstractButton` controls, vertically centered transport geometry, and one
shared idle policy for chrome and cursor visibility. The user confirmed the
resulting icon layout and cursor hiding in the production application. No GUI
application was launched by automation.
