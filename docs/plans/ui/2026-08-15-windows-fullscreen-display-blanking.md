# Windows fullscreen display blanking

## Goal

Add one session-only Player menu option that covers every display other than
the active fullscreen presentation display with opaque black. Enable the
option only on Windows for this change, while implementing the companion
windows entirely through public Qt APIs so another platform can opt in after
native validation.

## Grounded constraints

* `PresentationWindow` remains the sole video/UI presentation owner. Blackout
  windows must not create a QRhi, QML scene, video surface, color policy, or
  media-session dependency.
* The option is transient process state. It must not introduce a settings
  store, command-line option, or persisted preference.
* The Windows capability gate belongs at the native window-command boundary;
  unsupported platforms must not show an inert menu command.
* Blackout windows exist only while the option is enabled and the presentation
  window is fullscreen. Leaving fullscreen must destroy them.
* The presentation screen is selected from `PresentationWindow::screen()`.
  Screen hotplug and movement of the presentation window must converge to the
  latest screen list without adding display identities or ordering state.
* Companion windows must not accept focus or use always-on-top behavior. The
  presentation window keeps keyboard ownership, and another application can
  still cover the black windows after an explicit task switch.

## Design

1. Expose `otherDisplayBlankingAvailable` and
   `blankOtherDisplaysInFullscreen` on the existing `WindowCommands` QML
   boundary. Availability is true only for Windows; the enabled value lasts
   only for the current process.
2. Add one checkable `Blank other displays in fullscreen` item to Player's
   overflow menu and hide it when the capability is unavailable.
3. Implement one private `QRasterWindow` subclass that fills itself black.
   Each instance is a frameless, non-focusable Qt tool window associated with
   the presentation window.
4. Rebuild the small companion-window set from the latest
   `QGuiApplication::screens()` whenever fullscreen state, the option,
   presentation screen, or application screen membership changes. Skip the
   presentation screen, set both the target screen and its geometry, then use
   `showFullScreen()`.
5. Keep ownership in `PresentationWindow` as a vector of unique windows. A
   clear/rebuild operation makes exit, hotplug, and teardown canonical without
   a parallel screen registry.

## Verification

* Extend the existing QML shell fake and component scenario to verify
  capability visibility plus checked-state/property wiring in both directions.
* Build the complete existing Windows Debug tree and run application/test QML
  lint.
* Run the focused `qml-shell` component test, then the full registered
  non-device/non-GPU test set.
* Run the registered Windows fullscreen application scenario when the host is
  suitable. It enables the option and checks companion topology and lifecycle
  while retaining its presentation/fullscreen/restoration assertions.
* Record physical two-display verification as required follow-up evidence if
  only one screen is available: automation must not claim native multi-monitor
  coverage from the QML fake.

## Deliberately deferred

* Enabling the capability on macOS or native Wayland before platform-specific
  fullscreen/multi-display validation.
* Persisting the option across processes.
* Dimming levels, colors, patterns, per-screen selection, or monitor power and
  brightness control.
* Always-on-top behavior or preventing another application from covering a
  blackout window after an explicit task switch.

## Later follow-up

The later
[persistent player settings plan](../application/2026-08-16-persistent-player-settings.md)
supersedes this plan's persistence deferral: the existing checked command is
now restored across normal restarts. The original platform capability and
multi-display validation boundaries remain unchanged.

## Research sources

* [QGuiApplication screen enumeration and hotplug signals](https://doc.qt.io/qt-6/qguiapplication.html)
* [QWindow screen assignment and fullscreen state](https://doc.qt.io/qt-6/qwindow.html)
* [QRasterWindow CPU-backed paint boundary](https://doc.qt.io/qt-6/qrasterwindow.html)

## Delivery evidence

Validated on Windows 11 with Qt 6.11.1, Visual Studio 2026's x64 developer
environment, and two active 2560 × 1440 displays on 2026-08-15:

* `cmake --build cmake-build-debug --config Debug --parallel` completed the
  full Debug build after the final review fixes.
* `cmake --build cmake-build-debug --config Debug --target all_qmllint
  --parallel` passed both production and shell-test QML lint targets.
* `ctest --test-dir cmake-build-debug -C Debug --output-on-failure -R
  '^qml-shell$'` passed 1/1. It covers capability-hidden/capability-visible,
  backend-to-control checked state, and control-to-backend checked state.
* `ctest --test-dir cmake-build-debug -C Debug -V -R
  '^application-fullscreen$'` passed 1/1 through the real Windows window,
  D3D11, media, WASAPI, and fullscreen paths. Its output recorded
  `screenCount=2`; the scenario required Windows capability availability,
  matched one distinct non-focusable/non-always-on-top fullscreen companion
  to every non-presentation Qt screen, exercised disable/re-enable while
  fullscreen, and observed cleanup after both fullscreen exits.
* `ctest --test-dir cmake-build-debug -C Debug --output-on-failure -LE
  'device|gpu'` passed 22/22 after the final changes. An earlier repetition
  hit the unrelated timing-sensitive `media-session` assertion
  `video-starts-one-second-late`; the exact test passed immediately on focused
  retry, followed by the clean final 22/22 run. No media code was changed.
* `git diff --check` passes.

The automated evidence does not claim black-pixel appearance, live movement
between displays, hotplug, or direct keyboard-focus observation. Those remain
manual two-display Windows checks; macOS and Wayland capability enablement
remain deferred.
