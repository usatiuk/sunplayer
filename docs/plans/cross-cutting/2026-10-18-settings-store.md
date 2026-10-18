# Consolidate settings behind one storage boundary

## Problem and accepted scope

The installed AppSilo package cannot persist through QSettings native registry.
The attempted LocalFolder INI replacement also fails: the supplied Process
Monitor trace proves successful lock/temp creation, write and flush followed
by ACCESS DENIED on the final atomic rename. Remove that unsuccessful path,
not supplement it with mkdir, disabled atomicity, retries or another fallback.
The user accepts Windows ApplicationData.LocalSettings for packaged builds.

## Design

Introduce a small platform-agnostic SettingsStore interface for the existing
consumers: contains, value(default), setValue, remove(key and descendants), sync,
status, and a diagnostic backend name. Keys are application-owned strings;
values used by current consumers are booleans, numbers and strings. Status is
sticky and distinguishes access from format failures. No backend type/path or
native headers enter consumers. No cache, schema registry or generic provider
registry is needed.

One ApplicationSettings-owned store outlives PresentationWindow and is borrowed
by JellyfinBrowser. ApplicationSettings remains responsible for preference keys,
validation and applying defaults; JellyfinBrowser owns its metadata keys and
credential lifecycle. Storage owns serialization, persistence and failure
reporting. Both consumers use the same instance rather than reconstructing a
second QSettings from a path/format.

QSettings implementation retains normal native behavior on Linux/macOS and
unpackaged Windows, plus explicit temporary INI stores for tests/smokes. Disable
fallbacks as before. The Windows-only factory distinguishes package identity;
packaged builds use ApplicationData.Current.LocalSettings.Values. Box/unbox only
supported scalar types, preserve absent/default and group-removal semantics,
and report operation/HRESULT without account values. Native settings writes
are immediate; sync needs no separate flush. Keep one balanced runtime owner
alive until native objects are released. Unexpected package activation defects
fail fast; runtime native I/O failures become observable store errors.

Temporary-store intent remains explicit, independent of backend. Production
Jellyfin keeps its stable PasswordVault key and automatic restore. Temporary
browsers skip restore and use a unique test credential key. Tokens remain in
PasswordVault/QtKeychain; no passwords or access tokens enter SettingsStore.

Delete WindowsApplicationSettings.cpp and the format/path consumer API. Keep
both deferred QtKeychain build cleanups and existing low-volume diagnostics.
No migration, dependency, permission, native Qt patch, or platform behavior
change beyond packaged Windows settings.

## Validation and review

Three independent plan/implementation lenses: correctness/platform ownership,
simplicity, and tests/evidence/docs. Exercise shared store semantics through
real temporary QSettings, preference round trips and independent writers,
Jellyfin metadata/recreation and existing UI/smoke integration. Linux Debug
build and affected CTest/QML lint locally; Windows compile/tests/package only
in hosted CI. Native LocalSettings cannot be activated in an unpackaged hosted
process; installed acceptance remains ordinary preference restart, Remember me
across two restarts, then logout/restart. Do not claim CI proves AppSilo access.
Commit/push main and deliver the MSIX artifact after CI.

## Evidence

- [Trace investigation](../../research/2026-10-18-appsilo-settings-rename.md)
- https://learn.microsoft.com/en-us/uwp/api/windows.storage.applicationdata.localsettings
- https://learn.microsoft.com/en-us/windows/apps/design/app-settings/store-and-retrieve-app-data


## Plan review and existing third consumer

Three independent plan reviewers found no blockers. Incorporate strict scalar
conversion, exact/slash-descendant removal with a snapshot before mutation,
proper native-member/apartment teardown, and explicit temporary credential
intent. Existing AppImageUpdateUi also reconstructs settings from the app path;
it now borrows the same store to remove that path API completely. Its Linux
keys, native storage and consent/check behavior remain unchanged and its
existing integration tests are part of validation.

## Implementation evidence

The LocalFolder constructor is deleted. SettingsStore has independent real
QSettings and Windows LocalSettings implementations; ApplicationSettings owns
one instance and Jellyfin/AppImage borrow references to it. WinRT apartment
ownership is shared with PasswordVault through a small native-only RAII helper,
removing duplicated initialization without changing credential behavior.

Linux Debug build and all_qmllint passed. Six affected CTest suites passed 6/6
in 44.89 seconds: application-settings, Jellyfin, Basic/Fusion QML shell,
packaged-qml-module and appimage-update-ui. New public-boundary regressions
cover one shared preference/metadata store, recreation, scalar values, missing
defaults, and group removal preserving similarly named siblings. No fake
native store or test-only backend registry was added.

Three independent implementation reviews covered correctness/native lifetime,
simplicity, and tests/evidence. No material findings remained. Linux/macOS and
unpackaged Windows keep QSettings; native LocalSettings compilation and package
production await hosted CI. Its installed save/restart/restore/logout acceptance
is not claimed by these tests. Both deferred QtKeychain cleanup items and
low-volume persistence logs remain intact.


## Hosted compile follow-up

Initial commit `f6e70cb` reached hosted Windows compilation but failed because
an unqualified IInspectable using-declaration collided with the Windows SDK's
global COM IInspectable. Qualify the C++/WinRT return type directly and remove
the alias; no runtime behavior or dependency changes. Native compilation
remains delegated exclusively to hosted CI.


The follow-up `500c2d1` compiled successfully on Windows. All changed-feature
suites passed there, including application-settings, Jellyfin, Basic/Fusion/native
Windows QML, and PasswordVault. The first attempt then failed solely in the
previously observed ffmpeg-media-decoder test with no diagnostic output.
Reran the failed Windows job once without code or test modifications; Linux and
AppImage already passed. No flaky marker, test suppression or automatic-retry
policy was introduced.

## Delivery evidence

Implementation `f6e70cb` plus the compile-only correction `500c2d1` passed
[CI run 39154852490](https://github.com/usatiuk/sunplayer/actions/runs/39154852490).
Linux and AppImage passed on the first attempt; the failed Windows job was
rerun once without source/test changes and passed all 34 selected CTests,
release build, deployment and MSIX packaging. The initial blank-output decoder
failure remains separate from this settings implementation; tests were not
suppressed or marked flaky.

[Download the tested MSIX](https://github.com/usatiuk/sunplayer/actions/runs/39154852490/artifacts/12146928668).
This artifact is the code-bearing `500c2d1` build. Later documentation-only
bookkeeping does not require another package for user acceptance. No native
Windows/macOS build ran locally. Temporary PML parsers were removed; the user's
original trace was left untouched. Installed preference restart, remembered
login across two restarts and logout/restart remain acceptance checks.


## Installed acceptance and productization cleanup

On 2026-10-18 the user confirmed the installed Windows build works. This closes
the reported login/settings persistence blocker; it does not separately attest
each item in the detailed restart/logout checklist. Keep that matrix as manual
coverage rather than an unresolved implementation blocker.

The user requested a multiagent cleanup/productization cycle. Three independent
read-only reviews found no actionable runtime cleanup or new regression. The
store is shared, the failed LocalFolder path is gone, and the native apartment
helper has two real consumers. Consolidate current docs, retire the resolved
DEFERRED entry, and preserve historical plans/research. Keep explicitly useful
logs and both deferred QtKeychain build cleanups. No extra storage code, tests,
retry/migration/fallback machinery or native platform execution is justified.

The separately requested dark-control improvement is tracked in the
[UI plan](../ui/2026-10-18-dark-appearance.md).
