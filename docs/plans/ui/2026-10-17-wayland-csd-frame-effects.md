# Linux Wayland CSD shadows

Date: 2026-10-17
Status: Implemented; final validation recorded below.

## Accepted scope

One Linux-only passive shadow subsurface below the unchanged opaque Qt-owned
CSD window. Square corners; outline visibility is the only QML change. No
changes to video, subtitles, shared HDR composition, shared graphics contracts,
Windows or macOS. SSD does not create
the helper. See [ADR 0034](../../decisions/0034-isolate-wayland-csd-shadows.md).

| State | Shadow | Existing inner outline |
|---|---|---|
| Floating CSD | Visible | Visible |
| Maximized CSD | Hidden | Hidden; titlebar and controls remain |
| Any tiled CSD | Hidden | Hidden; titlebar and controls remain |
| Fullscreen | Hidden | Hidden, along with the titlebar and resize zones |
| SSD | Compositor owns | Compositor owns |

The user deferred rounded corners and requested a restrained desktop-like
shadow. Two centered black layers follow libadwaita's active window recipe:
15% opacity / 14 DIP blur / 5 DIP spread, and 10% / 5 DIP / 2 DIP. Do not add
another 1px edge or activation-dependent theme machinery. Gaussian rectangle
coverage gives softer corners than the earlier nearest-distance approximation.

## Implementation

* `LinuxWaylandWindowContext` owns `WaylandWindowShadow` only on its existing
  CSD path and releases it before destroying the native parent.
* The helper binds the standard subcompositor through Qt's existing registry.
  It never owns another toplevel, swapchain, graphics device or configure loop.
* One ARGB8888 SHM child is synchronized, placed below the parent with a
  24-DIP negative offset, and given empty input/opaque regions. Qt's explicit
  xdg geometry continues to describe only the body. No custom margins or
  coordinate remapping are necessary.
* A small event filter samples Qt's applied fullscreen/maximized/tiled state
  before Paint/Expose/UpdateRequest can present the parent. Only the child is
  committed; Qt's next parent presentation latches the change.
* Two immutable buffers are cached. If both are busy during resize, hide
  the stale shadow and retry the latest geometry when a buffer is released.
* Missing subcompositor is reported as an unavailable optional capability.
  Storage/mapping failure disables the shadow for that geometry, with retry
  only after changed geometry/scale or surface recreation. Playback survives.
  Backing pages are reserved before mapping so exhausted tmpfs does not turn
  painting into SIGBUS. Revisiting a failed size after another size retries.
* Native-surface replacement destroys child resources and rebuilds them for
  the new parent. Integer-ceiling buffer scale handles fractional parent DPR
  without adding a viewporter dependency.

## Validation

The `wayland-window-shadow` integration test launches an actual Qt Wayland
client against Qt's compositor implementation. It captures committed SHM pixels
and checks soft, symmetric corner coverage, input exclusion, stacking, unchanged
xdg body geometry, fullscreen/maximized suppression, each same-size tiled edge,
restoration, resize, scale changes, both buffers held across resize/fullscreen,
release-triggered convergence, surface recreation and complete client teardown.
It also makes the client's runtime storage unavailable, then restores it and
revisits the failed size after a successful different size, verifying recovery.

Existing Basic/Fusion shell tests pass and already assert that the complete
1px inner outline disappears in fullscreen. A follow-up also hides it while
maximized or tiled, without changing titlebar/control visibility. The native
helper forwards its existing applied tiled-state observation to the chrome
controller. Tests cover restoration to floating and all four tiled edges.

The full Debug build and both QML lint targets pass. The focused native shadow
test passes in 1.4 seconds; both existing Basic/Fusion shell tests pass. The
full 37-test Linux run passes 36 tests and has one media-session seek timing
failure (`interFrameSeekPublishesRequestedFrame`); that test passes when rerun
alone. No media code changed. Production audio-first playback also passes
under isolated Weston/pixman with lavapipe. The Linux fullscreen smoke remains
non-gating; the shadow test independently covers all decoration state changes.
With the normal video-late fixture, the Debug fullscreen smoke reaches stage 7
but times out restoring maximized state (`windowState=0`). A separate binary
built from the same Debug objects with only shadow construction disabled
reproduces the same stage/state timeout, so it is not introduced by this helper.
The pre-change Release binary completes that smoke. The initial short-fixture
attempt timed out earlier at stage 0 and is not used as restoration evidence.

User-observed native appearance was accepted after the softer libadwaita-style
falloff replaced the heavy distance-based corners. This is appearance feedback,
not calibrated HDR evidence.
Physical HDR desktop blending, fractional/mixed-monitor visual quality and live
pointer/snap interaction remain manual checks; the wire/pixel test is not a
claim of physical display validation.

## Maximized outline correction

The reported persistent outline was in maximized mode, which the original
policy deliberately kept outlined; fullscreen was the wrong reproduction state.
The corrected policy keeps the outline only on floating windows. Verification
uses the initial screen with no media, preserving its controls.

Follow-up validation: the full Debug build, both QML lint targets, Basic/Fusion
shell tests, native shadow test and shortcut test pass. A temporary diagnostic
binary captured the final player swapchain on isolated GNOME 50.1 with lavapipe:
the floating frame has 3,716 outline-color perimeter pixels; maximized and
fullscreen frames have zero. The maximized initial screen retains its titlebar,
window buttons, Open video button and overflow action. These are player-buffer
captures on software rendering, not physical-display/compositor-output captures.

## Review and reasoning

Read-only independent reviews covered protocol/lifetime correctness,
Qt/native-state ordering, and simplicity/test/scope. Review fixed external
allocation failure handling and added explicit withheld-buffer-release coverage.
The [research](../../research/2026-10-17-wayland-csd-shadows-and-rounding.md)
retains the alternatives. A same-surface shadow was rejected because RGB10A2's
four alpha levels do not justify changing HDR transport for cosmetic pixels.
