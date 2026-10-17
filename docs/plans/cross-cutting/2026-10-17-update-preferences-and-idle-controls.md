# AppImage startup preferences and CSD idle controls

Status: Shipped in [v1.4.1](https://github.com/usatiuk/sunplayer/releases/tag/v1.4.1).

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
- Implementation commits `70ed52d` and `5bde4a6` are pushed. The latter ensures
  the notice heading remains readable with light system palettes; the actual
  QML notice and native consent dialog were visually checked with controlled
  update availability. Release version commit: `c08aec1`.
- [Release workflow 39044140879](https://github.com/usatiuk/sunplayer/actions/runs/39044140879)
  passed: Windows's 31 deterministic tests and Store package, plus signed Linux
  AppImage validation/playback smoke. Regular Linux CI passed all 40 tests and
  unsigned AppImage smoke passed. Regular Windows CI's first attempt had the
  known silent `ffmpeg-media-decoder` failure; its failed job was rerun unchanged
  and passed tests and packaging. [Regular CI 39044136431](https://github.com/usatiuk/sunplayer/actions/runs/39044136431)
  is successful on attempt 2; no test or retry policy changed.
- Published all four assets: AppImage, checksum, zsync and MSIXUPLOAD. The
  checksum matches the uploaded AppImage SHA-256
  `d69eabf8c514173627159b2886f26a98d0ae13be148b2dec692e2ffedbc46dcb`.
  Physical-desktop update/relaunch remains the existing manual coverage gap.
