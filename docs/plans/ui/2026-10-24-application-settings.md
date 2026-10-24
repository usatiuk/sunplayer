# Shared Application settings and Jellyfin visibility

## Scope

Provide one Application tab on every platform with a persistent Hide Jellyfin
checkbox (default off). Linux embeds its existing updater controls in that tab.
Keep updater policy, credentials, browser session and active playback unchanged.
Hiding removes entry/forward/return navigation and leaves an open browser page
for Player; showing restores access. No feature teardown or new settings service.

## Implementation and verification

- ApplicationSettings validates/persists the preference; PresentationWindow owns
  the live UI value and supplies it through existing WindowCommands injection.
- SettingsDialog owns the shared page and one narrow platform-section insertion
  point, replacing its updater-only generic page extension.
- AppShell reconciles navigation against availability; JellyfinBrowser remains
  unaware of application visibility.
- Cover persistence/invalid values, shared-tab editing/platform-section reuse,
  and real QML navigation/entry visibility without interrupting playback.
- Build scoped targets, run settings/QML tests and QML lint using the existing
  CLion directory/Developer PowerShell. Preserve previous local UI edits.

## Delivery evidence

- Built `sunplayer`, `sunplayer_settings_dialog_tests`,
  `sunplayer_application_settings_tests` and `sunplayer_qml_shell_tests` in
  `cmake-build-debug` with VS Developer PowerShell outside the sandbox.
- All five affected CTests passed: application-settings, settings-dialog,
  qml-shell, qml-shell-fusion and qml-shell-windows (72.64 seconds).
- `all_qmllint` passed with the existing Qt renamed-type warnings; log retained
  in `cmake-build-debug/application-settings-qmllint.log`. `git diff --check` passed.
- Three independent read-only reviews covered correctness/lifecycle,
  simplicity/module boundaries and tests/evidence/scope; no actionable findings.
- Linux updater policy was not executed on this Windows host. The shared-tab
  insertion is exercised structurally with a QWidget, and the existing Linux
  updater widget and its policy tests are unchanged.
- Application/UI/Jellyfin subsystem docs, testing coverage and PLAN updated.
  Existing Advanced disclosure/HDR10+ default changes preserved. The settings
  dialog opens at 760 × 680 logical pixels. Commit requested; no push authorized.
