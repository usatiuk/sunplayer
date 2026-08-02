# Cross-platform fullscreen playback

Status: Complete

## Goal

Add ordinary desktop fullscreen behavior to Sunroom without bypassing Qt or
creating platform-specific window-management paths.

Completion means:

* `F11` toggles fullscreen while Sunroom is active.
* A left-button double-click on the Player video background toggles fullscreen.
* Double-clicking transport controls, sliders, menus, dialogs, or diagnostic UI
  does not toggle fullscreen.
* `Escape` exits fullscreen when no popup or dialog owns Escape. An open
  popup/dialog closes first without also leaving fullscreen.
* Exiting fullscreen restores the previous normal or maximized state.
* Playback, HDR presentation, cursor hiding, and the floating transport continue
  through the transition.

The immediate delivery and required acceptance target is Windows because the
current production graphics factory rejects every non-Windows backend. The
window-state design is deliberately Qt-native and contains no Windows-specific
fullscreen behavior; macOS and Wayland acceptance becomes required when their
production presentation backends are introduced.

## Grounded current behavior

* `PresentationWindow` is the sole real top-level `QWindow` and owns the QRhi
  presentation surface. The `QQuickWindow` is hidden and rendered through
  `QQuickRenderControl`; changing that hidden window's state would be wrong.
* `PresentationWindow` already forwards mouse press/release/move, wheel, and key
  events to the redirected Quick scene. It does not yet forward
  `MouseButtonDblClick`.
* Qt 6.11.1 documents that an item which does not accept a mouse button passes
  the event to the next item in the item hierarchy. A production-shell input
  test confirmed that non-interactive content inside a higher-stacked panel can
  therefore reach a lower background `MouseArea`; visual stacking alone is not
  the input contract.
* `PlayerPage` already owns video-background interaction and the transport's
  pointer-idle policy. `AppShell` owns page routing; neither owns native window
  resources.
* Resize, exposure, device-pixel-ratio, and platform-surface events already
  invalidate or rebuild the required presentation resources. Fullscreen does
  not need another graphics lifecycle.
* Qt 6.11.1 defines `QWindow::showFullScreen()` and `showNormal()` in terms of
  `Qt::WindowFullScreen` / `Qt::WindowNoState` plus visibility, and reports the
  effective state through `windowStateChanged` and `visibilityChanged`.
  `showNormal()` does not restore a previous maximized state by itself.
* Wayland fullscreen is a compositor request followed by an asynchronous
  configure; the compositor chooses the output when none is explicitly
  requested. macOS uses the native fullscreen/Spaces experience. Modern
  Windows benefits from compositor-managed borderless/flip-model presentation;
  exclusive DXGI fullscreen is neither required nor desired.

Primary references:

* [Qt 6.11.1 `QWindow`](https://doc.qt.io/qt-6/qwindow.html)
* [Qt 6.11.1 `TapHandler`](https://doc.qt.io/qt-6/qml-qtquick-taphandler.html)
* [Qt 6.11.1 Qt Quick Test input delivery](https://doc.qt.io/qt-6/qml-qttest-testcase.html)
* [Wayland `xdg_toplevel.set_fullscreen`](https://wayland.app/protocols/xdg-shell)
* [Apple fullscreen guidance](https://developer.apple.com/documentation/metal/managing-your-game-window-for-metal-in-macos)
* [Microsoft flip-model guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)

## Chosen design

### Native ownership

`PresentationWindow` remains the fullscreen authority. Add two narrow commands:

```cpp
Q_INVOKABLE void toggleFullscreen();
Q_INVOKABLE void exitFullscreen();
```

The commands call only Qt's `showFullScreen()`, `showNormal()`, or
`showMaximized()`. They do not set window geometry, window flags, native style
bits, monitor IDs, DXGI exclusive state, AppKit presentation options, or
Wayland protocol objects.

Track only whether the last non-fullscreen state was maximized. Entering
fullscreen records that value. Leaving through Sunroom restores maximized or
normal accordingly. Changes initiated by the window system remain authoritative;
update the remembered non-fullscreen state from Qt's state notification rather
than maintaining a second desired-state machine.

The command is eventually consistent with the window manager: request the Qt
state and let normal resize/expose/state notifications report and reconcile the
result. Do not block, poll, animate, or assume the platform transition completes
synchronously.

### QML command seam

Pass the real presentation window into the redirected scene as a narrowly named
`windowCommands` initial property. QML uses the fullscreen toggle command plus
the narrow cursor and transient-Escape policy properties; it does not set
`visibility`, geometry, flags, screen, or other inherited `QWindow` properties.

This avoids a one-feature controller class and avoids signal plumbing through
`QuickUiLayer` and `RhiPresentationEngine`. If future window preferences require
observable UI state or persistence, extract a dedicated application-owned
window model then; fullscreen alone does not justify it.

The real `PresentationWindow` owns F11 and Escape because it is the active
native key-event target. F11 toggles fullscreen and Escape leaves fullscreen;
both ignore auto-repeat, and leaving outside fullscreen is harmless.

The implementation experiment showed that a QML `Shortcut` attached to the
hidden render-control `QQuickWindow` is not a reliable window shortcut for the
visible native window. `VideoPage` therefore exposes one small
`windowShortcutsBlocked` value, false by default, and `AppShell` binds it onto
the native command object. `PlayerPage` sets it while its menu or file dialog
is open. A blocked Escape continues through the redirected Quick input path so
the transient UI consumes it; otherwise the native window consumes Escape and
exits fullscreen. Do not build a general action registry or focus model for
this narrow rule.

The same evidence applies to the existing Space shortcut. The native window
handles non-repeating Space only when Player is the active route, the session
is ready, no seek is active, and no popup/dialog blocks window shortcuts.
Play/pause buttons keep their existing QML command path.

### Double-click behavior

Add a bottom-stacked, left-button `MouseArea` (or an equivalent hit-tested QML
background item proven by the focused test) across the Player video area. Its
double-click handler requests `toggleFullscreen()` only while the Player video
viewport is active. Controls and overlays remain above it and therefore receive
their own input without triggering fullscreen.

Give each floating interactive panel one accepting area behind its child
controls so blank panel space and non-interactive labels cannot reach the video
background. Do not wrap every child in another blocker: buttons and sliders
remain above the panel-level area and keep their normal behavior. The
production QML integration test is the regression contract.

Do not use a page-wide passive `TapHandler`: its documented passive-grab behavior
can observe taps delivered to existing controls, which would make a double-click
on a slider or button toggle fullscreen too.

Add `PresentationWindow::mouseDoubleClickEvent()` using the same coordinate
mapping as existing press/release forwarding. Preserve the original event's
timestamp, source, and pointing device when mapping it into the hidden Quick
window; dropping those facts can break Qt Quick's multi-click interpretation.
That completes the redirected input contract; the native window does not
perform QML hit testing itself.

Double-click is mouse-only in this slice. Touch double-tap can be added when the
currently deferred touch forwarding is implemented, rather than pretending the
mouse bridge supplies complete touch support.

### Presentation behavior

Fullscreen is compositor-managed, not exclusive display ownership:

* Windows stays in the DWM/Advanced Color pipeline; do not call
  `SetFullscreenState`, change display modes, or bypass desktop color management.
* macOS uses Qt's native AppKit fullscreen transition and Space; do not create a
  borderless replacement window or customize the native animation.
* Wayland lets Qt request fullscreen for the current toplevel and lets the
  compositor select/configure the output; do not assign geometry or call native
  protocol objects directly.
* X11 and XWayland remain unsupported by project policy.

A fullscreen resize follows the existing QRhi resize path. Do not recreate the
graphics device or media session. Recreate presentation resources only if the
existing Qt/QRhi surface lifecycle reports that they actually changed.

The current Player idle policy is unchanged: pointer movement reveals the
transport, and the cursor hides with it during uninterrupted playback. QML
publishes that desired visibility, but the real `PresentationWindow` applies
the native cursor and reapplies it after a window-state transition; the hidden
redirected `QQuickWindow` is not a reliable cursor owner across fullscreen.
Entering fullscreen does not permanently pin or force-hide the controls.

## Implementation slices

1. Add the two fullscreen commands and normal/maximized restoration behavior to
   `PresentationWindow`; forward native double-click events to the hidden Quick
   window.
2. Pass `windowCommands` through `QuickUiLayer` initial properties and the
   `Main.qml` / `AppShell.qml` required-property chain.
3. Handle non-repeating F11/Escape at the native window, publish the narrow QML
   popup/dialog blocking fact, and add the hit-tested Player-background
   double-click gesture.
4. Add focused behavioral coverage, perform real platform acceptance, and
   synchronize application/UI/testing documentation.

This is one coherent UI/application change and can ship as one commit unless a
platform defect requires a separate corrective slice.

## Validation

### Automated behavior

Extend the existing QML shell integration test with a small test command object
backed by the test `QQuickWindow`:

* With the transport menu open, QML publishes the native-shortcut blocking fact
  and Escape closes the menu before clearing that fact.
* A background double-click requests one toggle.
* Double-clicking play/pause, seek, volume, the overflow menu, and the HDR Lab
  page does not request fullscreen.
* Existing single-click controls and slider drags remain functional.

Add a bounded, noninteractive actual-application fullscreen scenario, following
the existing playback-smoke pattern, which observes the real
`PresentationWindow::windowStateChanged` boundary:

* native F11, Escape, and Space routing, including F11 auto-repeat suppression,
  popup-owned Escape, windowed Escape as a no-op, and Space pause/resume;
* native redirected background double-click routing;
* normal -> fullscreen -> normal;
* maximized -> fullscreen -> maximized;
* repeated exit while windowed is a no-op;
* a native/external non-fullscreen state update becomes the next restore state;
* resize/exposure continues far enough to present successfully after each
  transition.

Keep this scenario Windows-only while Windows is the only production graphics
backend. It is not a platform emulator and does not assert event order or fixed
timings: it waits with a bounded deadline for the observable state and
presentation result. QML command/hit-testing behavior remains covered by the
existing shell integration test with a tiny fake `windowCommands` object.

Run QML lint, build the application and affected test targets, then run the
registered tests through CTest under the repository's noninteractive build
rules.

### Pending platform acceptance

The following Windows hardware/manual checks were not performed by the
noninteractive implementation run and remain acceptance gaps:

* Toggle with F11 and Player-background double-click; leave with F11 and Escape.
* Verify double-clicking every interactive overlay does not toggle.
* Enter from both normal and maximized states and verify restoration.
* Move the window to another display, then enter fullscreen and confirm the
  platform chooses the current display without Sunroom repositioning it.
* Toggle during playing, paused, buffering, and while controls are hidden.
* Confirm video keeps presenting, aspect fitting and UI scale update, and the
  transport/cursor idle policy still works.
* Confirm HDR/reference-white diagnostics remain correct and no exclusive-mode
  or second color-management path appears.

Repeat the same acceptance when the native macOS and Wayland presentation
backends land. Wayland acceptance must allow convergence after the compositor's
configure and must not impose Windows-like placement or synchronous-state
assumptions. A macOS transition must use the platform's native fullscreen Space
and may likewise complete asynchronously.

## Documentation impact

When implemented:

* Update `docs/subsystems/application/README.md` with native fullscreen
  ownership, restoration, and redirected double-click input.
* Update `docs/subsystems/ui/README.md` with shortcuts and background gesture.
* Update `docs/subsystems/testing/README.md` and `docs/TESTING.md` with automated
  and manual fullscreen coverage.
* Remove fullscreen and the implemented F11/Escape behavior from the root
  `PLAN.md` missing-shell and keyboard-shortcut checklists.

No ADR is needed unless implementation evidence forces a platform-specific
exception to the Qt-native policy.

## Non-goals

* Exclusive fullscreen, display-mode switching, refresh-rate switching, or HDR
  bypass paths.
* Choosing a fullscreen monitor in application settings.
* Borderless-window emulation, custom title bars, or platform-native animation
  hooks.
* Persistent window geometry/state settings.
* Touch double-tap before complete touch forwarding.
* A reusable command bus, global action registry, or generic window-controller
  framework.

## Delivery evidence

Automated validation completed on Windows Debug:

* `sunroom`, `sunroom_qml_shell_tests`, and `all_qmllint` built successfully.
* Production QML lint is clean. Test lint retains the separately deferred HDR
  Lab `videoColorPolicy` fake-type warning recorded in `docs/DEFERRED.md`.
* All 25 registered CTests pass.
* The QML shell test covers background and overlay hit testing, popup Escape
  ownership, cursor intent, and retained transport behavior.
* The actual-application fullscreen test drives native F11, blocked and
  unblocked Escape, Space pause/resume, and redirected background double-click;
  it verifies normal/maximized restoration, idle cursor hiding through the
  first fullscreen transition, and continued video presentation.

Final independent review used architecture, behavior, and evidence lenses. It
found the hidden-window Space shortcut, missing native blocked-Escape test, and
two overstated bookkeeping claims; all were corrected before delivery. The
final architecture, behavior, and evidence rechecks were clean.
