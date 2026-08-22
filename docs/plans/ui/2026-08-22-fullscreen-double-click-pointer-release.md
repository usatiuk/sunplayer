# Fullscreen double-click pointer release

Status: Complete

## Goal

Keep pointer movement, the playback controls, and cursor recovery working after
entering or leaving fullscreen through the Player background double-click on
macOS, without changing the native fullscreen path or adding platform-specific
behavior.

Completion means:

* the background gesture requests fullscreen only after the second click is
  released;
* double-clicking transport controls, panels, menu buttons, or HDR Lab still
  does not request fullscreen;
* F11, Escape, native window controls, playback, and fullscreen restoration are
  unchanged on Windows, macOS, and Wayland.

## Evidence

`PlayerPage` currently handles `MouseArea.doubleClicked`, which Qt emits for the
second press. That handler synchronously calls `PresentationWindow::toggleFullscreen()`;
on macOS this begins an asynchronous native fullscreen Space transition before
the matching release has completed the redirected Quick input sequence.

The reported failure affects both the cursor and playback controls, and a later
click restores both. Entering fullscreen through the macOS traffic-light button
does not reproduce it. This isolates the failure to the redirected background
double-click path rather than native cursor application or the fullscreen
transition alone.

Qt 6.11.1 emits `MouseArea.released` after the final release event has arrived,
then completes the area's normal pointer-grab cleanup after the signal handler
returns. Moving the fullscreen request to that signal prevents the native
transition from starting before the release exists; macOS interactive
confirmation remains necessary because the handler itself runs before final
internal ungrab cleanup.

Primary references:

* [Qt 6 `MouseArea.doubleClicked`](https://doc.qt.io/qt-6/qml-qtquick-mousearea.html#doubleClicked-signal)
* [Qt 6.11.1 `QQuickMouseArea`](https://code.qt.io/cgit/qt/qtdeclarative.git/tree/src/quick/items/qquickmousearea.cpp?h=v6.11.1)

## Design

Keep the full-page, left-button background `MouseArea` because it is the proven
hit-test boundary behind the Player controls. Its `doubleClicked` handler arms
one local boolean. The matching `released` handler clears that value and calls
the existing `windowCommands.toggleFullscreen()` command only when it was
armed; cancellation clears it without calling the command.
Losing session readiness also clears it when the background area becomes
invisible.

Do not add a macOS branch, explicit ungrab, queued callback, timer, C++ state,
or native-window lifecycle machinery. Do not replace the foreground accepting
areas or alter keyboard/native fullscreen entry. A passive page-wide
`TapHandler` is rejected because Qt explicitly allows it to observe input that
another visual layer is already handling; a non-passive policy would take the
exclusive grab that this change is trying to settle before fullscreen entry.

## Verification

Extend the production QML-shell test to deliver the two clicks as four distinct
events with explicit short delays inside Qt's double-click interval. Assert
that the fullscreen command has not run after the second press, then release
and assert exactly one request. Cancel one armed gesture and verify a later
single click does not request fullscreen. The existing next-movement check must
still reveal the controls. Keep the existing assertions that
double-clicks over the transport, seek and volume controls, menu button,
details panel, and HDR Lab do not request fullscreen.

Run the focused QML-shell build and CTest target. Record the macOS interactive
confirmation separately because automated offscreen input proves the shared
gesture contract but not AppKit's native fullscreen Space behavior.

## Delivery evidence

The clean-tree regression first failed on the second press because the existing
handler had already requested fullscreen. After the implementation, the same
test proves no request on that press, exactly one on the matching release,
cancellation followed by an ordinary click does not request fullscreen, the
next movement still reveals the controls, and the existing foreground/HDR Lab
exclusions remain intact.

Validation on Apple M2/macOS 26 with Qt 6.11.1:

* production and test QML lint targets pass;
* the focused `qml-shell` CTest passes;
* the normal Debug `sunplayer` application target builds successfully;
* the changed C++ test range passes `clang-format --dry-run --Werror` (the full
  pre-existing file has unrelated formatting deviations outside this change);
* three independent implementation reviews found no remaining behavior,
  architecture, cross-platform, simplicity, test, documentation, or scope
  issue after one local readiness-reset finding was resolved.

The final direct-`released` Debug build was launched by the user and confirmed
to restore pointer and control activity after the macOS fullscreen transition.
The application was not launched automatically on the user's active desktop.
