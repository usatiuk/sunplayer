# AppImage startup preferences and CSD idle controls

## Accepted scope

- Fix stationary hover notifications keeping CSD playback controls/cursor visible
  in windowed and maximized modes. Reuse PlayerPage's movement filter.
- Only updater-enabled Linux AppImages ask once whether to check at startup.
  Persist Yes/No in Qt's application settings; missing means unanswered. Defer
  the question until media is Empty; closing it leaves the choice unanswered
  until next launch. Never check before consent.
- Add Application to the existing Settings dialog only with that capability.
  One immediate checkbox; enabling checks immediately. No scheduling or install
  automation. Existing startup checks stay asynchronous during playback.
- Empty-screen notice: Update available, Update and restart, Dismiss. Dismissal
  lasts this process. No ignored-version database or additional preferences.
- Linux owns update preferences and widgets. Shared settings dialog only accepts
  a QWidget tab; no fake updater implementation or shared update policy.

## Validation

The stationary-pointer regression fails before the fix in the real QML shell.
Cover windowed, maximized and fullscreen hiding plus movement revealing controls
and cursor. Run Basic and Fusion. Cover updater consent persistence, deferred
prompt, checkbox behavior and dismissal using explicit temporary settings and
controlled helper processes. Keep existing staged replacement/cancellation tests.
Use the project ship-change independent review before shipping. User authorized
commit, push and a patch release after completion.

## Delivery evidence

- Linux Debug application build and both QML lint targets pass.
- Six focused CTests pass: application-settings, settings-dialog, qml-shell,
  qml-shell-fusion, appimage-update-ui, appimage-updater. The consent suite also
  verifies no helper invocation while unanswered or with a saved No, using a
  helper marker and a bounded event-loop observation; both updater tests pass
  after this final coverage addition.
- Three independent read-only reviews cover correctness/lifetime, simplicity
  and boundaries, and tests/docs/scope. Their settings-isolation finding was
  fixed by passing the actual application's backing filename into the Linux
  UI; preference keys and policy remain Linux-owned. The user confirmed cursor
  hiding works on their desktop.
- Commit/push and patch release pending hosted build validation. Physical-
  desktop update/relaunch remains the existing manual coverage gap.
