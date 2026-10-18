# Packaged Windows settings in LocalFolder

## Evidence and scope

Installed AppSilo logs show QSettings NativeFormat AccessError on the first
boolean write, while PasswordVault writes succeed. The user confirmed ordinary
settings also fail installed and persist with the same unpackaged binaries.
This isolates the failure to the packaged environment; the precise native
registry denial is not established. Microsoft issue #53 documents an older,
fixed AppSilo registry defect, not proof of the current root cause.

## Implementation plan

Keep QSettings serialization and all preference consumers. On Windows only,
detect package identity with GetCurrentPackageFullName. Packaged builds use
settings.ini under Windows.Storage.ApplicationData.Current.LocalFolder;
unpackaged builds retain default native QSettings. Resolve this in a Windows
implementation of the existing ApplicationSettings constructor, with bounded
WinRT initialization and fail-fast native path-resolution errors. Do not hide a
broken package behind a registry fallback. Linux/macOS are unchanged.

Explicit-path ApplicationSettings remains the temporary test/smoke store.
Expose that existing intent independently of format so Jellyfin uses its normal
credential entry and restoration with production INI, while tests keep unique
entries and no automatic restore. No new dependency, capability, migration,
dual write, settings framework, or credential backend change.

## Validation and review

Review plan and implementation for correctness/platform boundaries, simplicity,
and tests/docs. Run affected Linux Debug tests/build/lint; hosted CI exclusively
builds Windows. Existing temporary-INI settings regressions exercise persisted
values, independent writers, and errors; add coverage for explicit temporary
intent. Installed LocalFolder access and remembered-login restoration require
user acceptance of the produced MSIX. Commit/push main, resolve CI failures,
and provide the MSIX artifact link. No local Windows/macOS build or execution.

## Sources

- https://learn.microsoft.com/en-us/windows/apps/design/app-settings/store-and-retrieve-app-data
- https://learn.microsoft.com/en-us/uwp/api/windows.storage.applicationdata.localfolder
- https://github.com/microsoft/win32-app-isolation/issues/53


## Plan review

Three independent reviewers found no blockers. Required checks incorporated:
precise package-query classification, local WinRT initialization before native
media/display owners exist, native-object teardown before apartment release,
and both Jellyfin credential identity/restoration decoupled from file format.
The installed acceptance sequence includes ordinary preference restart,
remembered login across two restarts, and logout followed by restart.


## Implementation validation

Linux Debug build and all_qmllint passed. Five affected CTest suites passed
5/5 in 44.72 seconds: application-settings, Jellyfin, Basic/Fusion QML shell,
and packaged QML module. Explicit-path regression checks temporary-store intent
and INI format alongside existing round-trip/independent-writer/error coverage.
The actual packaged SDK path and production token restoration are intentionally
not exercised by tests that must avoid the user's native credentials.

Three independent implementation reviews covered correctness/native lifetime,
simplicity/platform scope, and tests/docs/build wiring. Fixed the SDK include
order finding (qt_windows.h before appmodel.h); no other blockers remained.
No Windows/macOS build or execution occurred locally. Hosted CI and produced
MSIX installed preference/remembered-login acceptance remain pending.
