# Persistent player settings

Status: Complete

## Goal

Persist the small set of existing player preferences that should survive a
normal restart through one shared Qt implementation on Windows, macOS, and
native Wayland. A cold start must restore valid preferences before QML or media
playback can observe defaults, while missing, malformed, or unwritable settings
must leave the player usable with safe defaults.

For this first slice, persist:

* Playback volume on every supported platform.
* The opt-in `Blank other displays in fullscreen` preference wherever that
  capability is available. It remains unavailable and unapplied on platforms
  where `PresentationWindow` does not expose the capability.

This deliberately changes fullscreen display blanking from session-only to a
user preference. It does not add a separate settings page; the existing volume
slider and checked Player menu action remain the user-facing controls.

## Grounded current state

* `main.cpp` sets the application name to `SunPlayer`, but no organization or
  domain identity exists for a default `QSettings` store.
* `PresentationWindow` is the current composition root. It constructs
  `MediaSession`, `PresentationSettings`, and the graphics/QML engine.
* `MediaSession` is the canonical runtime owner of volume. It defaults to
  `1.0`, normalizes user changes to `[0, 1]`, applies gain at the audio-output
  boundary, and emits `volumeChanged`.
* `PresentationWindow` is the canonical runtime and capability owner for
  fullscreen display blanking. It defaults to disabled and currently asserts
  if an unsupported platform is asked to enable it.
* HDR Lab presentation and diagnostic controls are intentionally not ordinary
  player preferences. The current shared target-headroom control can also
  affect Player presentation, so persisting it would amplify an existing
  ownership problem.
* The real application smoke scenarios currently construct the production
  window directly. The fullscreen scenario toggles display blanking, so adding
  persistence without isolation would modify a developer's real preference.

Qt's `QSettings` completely owns the production storage selection and platform
access. SunPlayer does not implement or select among registry, CFPreferences,
or XDG code paths. It supplies stable application identity and typed keys, then
default-constructs `QSettings`; Qt uses the registry on Windows,
CFPreferences on macOS, and an XDG-hosted textual configuration file on Unix.
This is one cross-platform API and behavior, not a portable file, cloud
synchronization, or cross-device roaming contract.

## Chosen design

### Stable storage identity and backend

* Set `organizationName` to `usatiuk` and retain `applicationName` as
  `SunPlayer` before constructing settings. The organization name comes from
  the repository owner and forms a durable unversioned storage namespace. Do
  not invent an organization domain. The established organization-name/no-
  domain identity is immutable unless a later change includes an explicit
  settings migration.
* Default-construct production `QSettings` after setting application identity.
  Its defaults are `QSettings::NativeFormat` and `QSettings::UserScope`, and
  default construction is the path that correctly applies Qt's macOS identity
  rules as well as the Windows and Unix names. Do not add platform conditionals
  or directly open a registry key, plist, or configuration path.
* Disable QSettings' organization-wide and system-wide fallback lookup. The
  player has no managed-policy layer, and an absent per-user application key
  should mean the product default.
* Use stable, case-consistent keys:
  * `playback/volume`
  * `fullscreen/blankOtherDisplays`
* Do not add a global schema version. Missing keys supply defaults, and a
  future incompatible key change can receive a focused migration when it
  exists.

### Windows packaging

The same default `QSettings` code applies to an unpackaged executable and a
future Microsoft Store MSIX; Windows owns any applicable per-package HKCU
virtualization. The packaging slice must keep package identity stable and
validate packaged restart, upgrade, and uninstall behavior. SunPlayer has no
supported public unpackaged Windows distribution, so this creates no migration
contract for developer registry state. Revisit that only if a public
unpackaged distribution ships before the Store package. Do not request
`unvirtualizedResources` merely for these private preferences.

### Ownership and lifecycle

Add one small C++ `ApplicationSettings` adapter owned for the lifetime of
`main()`. It owns the `QSettings` backend, typed load/store helpers, validation,
and final synchronization, but no duplicate observable preference values.
Production constructs its native backend; tests and noninteractive application
scenarios may inject an explicit temporary INI file.

Pass the adapter by reference into `PresentationWindow`. During window
initialization:

1. Construct the existing runtime owners.
2. Read and validate the stored values.
3. Apply volume to `MediaSession` and apply display blanking only when the
   capability is available.
4. Connect `MediaSession::volumeChanged` and
   `PresentationWindow::blankOtherDisplaysInFullscreenChanged` to per-key
   writes.
5. Construct `RhiPresentationEngine` and its redirected QML scene.

`MediaSession` and `PresentationWindow` remain the only live sources of truth,
and QML keeps binding directly to them. The adapter never becomes a QML
singleton or a second property model. Loading before engine creation also
prevents graphics-device recovery and QML-engine recreation from reloading or
overwriting runtime preferences.

Write only a key whose canonical value changed. Let QSettings batch the
physical write rather than synchronizing every volume-slider movement. Connect
to `QCoreApplication::aboutToQuit` and call `sync()` there while the window,
settings object, and logger are alive; do not rely on code after `app.exec()`,
which is not guaranteed to run during every platform shutdown. No
cross-process watcher or live reconciliation is required while SunPlayer has
no single-instance or settings-editing coordination model.

### Validation and recovery

Treat every persisted value as untrusted at the adapter boundary:

* A missing value leaves the runtime owner's existing default unchanged.
* Volume must convert to a finite number in the inclusive range `[0, 1]`.
  Reject malformed, non-finite, or out-of-range values and retain `1.0` rather
  than relying on incidental QVariant conversion or clamping corrupt data.
* Display blanking accepts only recognized boolean or `0`/`1`
  representations. An invalid value retains the disabled default.
* A stored enabled blanking value is ignored, not forwarded, when the platform
  capability is unavailable.
* Load into validated candidates and inspect `QSettings::status()` before
  applying any of them. A file-level format or access error rejects the entire
  stored candidate set for that process, preventing partially parsed values
  from mixing with defaults. Invalid individual keys in an otherwise healthy
  store fall back independently. Log one bounded application warning without
  logging stored values, and preserve unknown keys.
* Treat a non-`NoError` QSettings status as a lifetime-sticky persistence fault;
  Qt exposes no reset operation. Access or synchronization failures do not
  become playback failures: keep the canonical in-memory preferences and do
  not claim that a later write was successfully persisted. Continue recording
  explicit user changes per key so Qt may recover if the external condition
  clears, but do not emit repeated warnings or build a retry state machine.
* Preserve QSettings' atomic synchronization requirement; never disable it to
  make an unwritable store appear successful.

### Automation isolation

`--playback-smoke`, `--fullscreen-smoke`, and
`--verify-initial-background` must use a temporary explicit INI backend rather
than the user's native store. `main()` must retain the `QTemporaryDir` until
after `ApplicationSettings` performs its final synchronization and is
destroyed; settings are destroyed first and the directory second. The
scenarios may read and write their isolated settings, but must neither inherit
user preferences nor leave changes behind.
`--verify-qml` exits before constructing application settings and remains
unchanged.

## Deliberately deferred

* Persisting mute. Relaunching silently is a separate user-behavior decision;
  volume restoration does not require it.
* Window position, size, maximized/fullscreen state, or display identity.
  Cross-platform restoration needs screen-topology reconciliation and Wayland
  position semantics that are not required for the first settings slice.
* Current media, playback position, playing/paused state, recent files, track
  selections, or per-media preferences.
* HDR Lab target controls, pattern settings, renderer selection, current page,
  playback-details visibility, transient menus, and dialogs.
* A dedicated settings page, reset/import/export UI, custom JSON or INI
  production format, secrets, large data, cloud sync, or a settings-service
  registry.
* User-facing persistence-error presentation. Until the application has a
  general structured-error surface, the bounded recoverable failure remains
  available in the application log.
* Custom behavior for MSIX registry virtualization, cross-installation
  synchronization, or survival after complete package uninstall. The Store
  packaging slice owns manifest identity, update, and uninstall-lifecycle
  validation.

## Implementation slices

1. **Storage boundary**
   * Add `ApplicationSettings` with the native production constructor and an
     explicit-file INI constructor for deterministic tests/scenarios.
   * Establish the stable application identity, default-construct the
     production backend, disable fallback lookup, add typed per-key
     validation, and log its lifetime-sticky recoverable fault once.
2. **Runtime wiring**
   * Make `main.cpp` own the adapter and pass it to both platform forms of the
     `PresentationWindow` constructor.
   * Restore valid values before engine/QML construction, then mirror only
     canonical changes back to the adapter.
   * Synchronize from `aboutToQuit` and inspect status during orderly shutdown.
3. **Scenario isolation**
   * Select a temporary INI backend for every real-window verification/smoke
     mode so automation cannot depend on or mutate the native user profile.
4. **Tests and documentation**
   * Add focused store tests and extend the appropriate application scenario
     assertions.
   * Update current subsystem truth and the root roadmap after implementation.

## Acceptance and validation

Automated coverage must prove:

* An empty temporary store retains volume `1.0` and disabled blanking.
* Valid values survive adapter destruction/recreation through an explicit
  `QTemporaryDir` INI file.
* Malformed types, unrecognized booleans, NaN/infinite/out-of-range volume,
  and corrupt INI input recover to safe defaults without aborting.
* A deterministic uncreatable INI target reports `AccessError`, logs one
  bounded warning, leaves canonical runtime state usable, and does not turn
  into a playback or shutdown failure. Create the target beneath a regular
  file rather than depending on platform-specific permission behavior.
* Unknown keys survive writes, and changing one preference does not rewrite a
  stale full snapshot over another key.
* An unavailable blanking capability neither applies nor overwrites a stored
  enabled value.
* The runtime wiring restores values before QML observes them and writes later
  canonical changes.
* Smoke/verification modes never touch the production settings backend.
* The temporary scenario directory outlives the settings backend and its final
  synchronization.
* Existing `MediaSession` volume/gain tests and QML volume/blanking bindings
  continue to pass.

Implementation validation should then run the focused settings, media-session,
and QML shell tests; the non-device/non-GPU suite; the existing fullscreen
application scenario on Windows; and the supported cross-platform CI matrix.
Because build and test commands are prohibited in the workspace sandbox, all
such validation must use the documented native developer environments outside
the sandbox.

Manual cold-start checks on Windows, macOS, and native Wayland should change
volume, exit normally, relaunch, and observe restoration before playback.
Windows should additionally verify display blanking across a restart and a
real fullscreen transition. These checks validate SunPlayer's native settings
identity and lifecycle without re-testing Qt's registry, CFPreferences, or XDG
implementation details.

Before the first Store submission, extend the installed-package check beyond
the now-validated stable-identity install/update/cold-launch/uninstall lifecycle:
change both persisted values through the packaged application, relaunch, update,
and record the clean uninstall/reinstall result. That is packaging evidence,
not a second settings implementation.

## Documentation impact

The implementation synchronizes the application, UI, audio, build, and testing
subsystem documentation; removes volume persistence from `docs/DEFERRED.md`;
and links the earlier fullscreen-blanking plan to this follow-up. Root
`PLAN.md` marks the initial persistence slice complete while UI `Minimal
settings surface` remains unchecked because no dedicated settings UI was
added.

## Commit boundaries

1. Add and validate the settings adapter plus deterministic unit coverage.
2. Wire volume and fullscreen blanking persistence, isolate application
   scenarios, and extend integration coverage.
3. Synchronize subsystem, roadmap, deferred-work, testing, and completed-plan
   evidence.

## Research sources

* [Qt QSettings](https://doc.qt.io/qt-6/qsettings.html)
* [Qt application identity](https://doc.qt.io/qt-6/qcoreapplication.html)
* [Microsoft: packaged desktop application behavior](https://learn.microsoft.com/windows/msix/desktop/desktop-to-uwp-behind-the-scenes)
* [Microsoft: MSIX registry virtualization controls](https://learn.microsoft.com/windows/msix/desktop/flexible-virtualization)

## Delivery evidence

Completed on Windows 11 with Qt 6.11.1 and Visual Studio 2026's x64 developer
environment, using the existing CLion-configured Debug tree:

* The final complete Debug build passed after CLion reloaded the CMake source
  changes; no competing manual configure was started.
* The focused `application-settings` test passed. Its explicit temporary INI
  files cover defaults, round trips, independent per-key writes, unknown-key
  preservation, strict malformed-value recovery, corrupt-file rejection,
  deterministic access failure, and one bounded structured warning.
* `ctest --test-dir cmake-build-debug --output-on-failure -LE
  'device|gpu'` passed 23/23, including `media-session`, `qml-shell`, and the
  packaged QML module.
* The real `application-fullscreen` scenario passed 1/1 with WASAPI and two
  active displays. It now pre-seeds its retained temporary settings store,
  verifies volume and capability-gated blanking restoration, drives the
  QML-facing volume property, exercises blanking changes, and re-reads the
  isolated store to verify write-through and per-key preservation.
* `clang-format --dry-run --Werror` on the changed C++ files and
  `git diff --check` passed.

Independent correctness, architecture/lifecycle, and simplicity reviews found
the missing production-wiring scenario coverage, a test-only raw backend-status
API, the need to make the no-domain storage identity migration-bound, stale test
counts, and imprecise composition-root documentation. All were corrected and
the affected build and tests repeated.

New native macOS and Wayland executions and manual cold-start checks remain
platform evidence to collect; the implementation contains no alternate backend
or platform storage branch. The active MSIX plan still owns packaged settings
mutation/relaunch and clean uninstall/reinstall evidence before Store release.
