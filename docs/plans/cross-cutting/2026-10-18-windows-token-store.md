# Windows token persistence

## Scope and ownership

Replace QtKeychain's Windows Generic Credential backend with Windows
PasswordVault, retaining the existing TokenStore read/write/remove callbacks.
CMake selects one implementation: PasswordVault on Windows, QtKeychain on
Linux/macOS. No runtime selection, new dependency, permission, account model,
plaintext fallback, or settings-storage rewrite. Preserve both credential and
metadata settings errors rather than overwrite the first with the second.

Windows operations remain queued and QObject-bound. Each operation initializes
the Windows Runtime independently, releases native objects before runtime
teardown, then completes with Qt values. Existing apartments are supported.
Add replaces the same resource/entry. Only ERROR_NOT_FOUND is successful
absence during removal. No secret, raw server address, or native error message
is logged; errors retain a diagnostic HRESULT.

## Evidence and implementation checks

Microsoft tracker [#94](https://github.com/microsoft/win32-app-isolation/issues/94)
reports Generic Credential API access denied under isolated MSIX. This matches
QtKeychain's Windows backend; it does not prove every observed save failure.
The older registry issue #53 was fixed and does not justify changing working
application settings. PasswordVault is documented for AppContainer and desktop
apps; actual AppSilo package acceptance remains required.

Sources: [PasswordVault](https://learn.microsoft.com/en-us/uwp/api/windows.security.credentials.passwordvault),
[Microsoft C++/WinRT sample](https://github.com/microsoft/Windows-universal-samples/tree/main/Samples/PasswordVault/cppwinrt),
[RoInitialize](https://learn.microsoft.com/en-us/windows/win32/api/roapi/nf-roapi-roinitialize).

Three independent plan reviews covered APIs/lifetime, absence/errors, and
simplicity/cache/testing. They confirmed queued completion, local WinRT
ownership, overwrite semantics, explicit password retrieval, and idempotent
removal. No plan blocker remains.

Keep the Windows vcpkg triplet unchanged to retain existing per-port cache ABI
identities; its unused QtKeychain clause can be removed with the next deliberate
dependency-cache refresh. Restrict the manifest dependency to macOS.

## Validation and delivery

Run Linux Debug build, affected tests, and QML lint. Add a Windows-only real
PasswordVault integration test with a unique test entry: missing read,
write/read/overwrite, idempotent removal, and cancellation on owner destruction.
Cleanup touches only that test entry. Hosted CI is the only Windows build host;
do not build or run Windows/macOS locally. Review implementation independently,
commit/push main, inspect CI and fix failures until green. Deliver the Windows
MSIX artifact link for the user's installed-package save/restart/logout test.

Implementation and delivery evidence will be recorded below.

Local implementation evidence: Linux Debug build and all_qmllint passed. The
jellyfin, qml-shell Basic/Fusion, and packaged-qml-module suites passed 4/4 in
44.99 s. The Windows source/test are selected only on Windows and were not
compiled here. Three independent API/lifetime, error, and simplicity/evidence implementation
reviews found no material issue. No runtime backend
selection, extra permission, or platform SDK dependency was introduced.

## Delivery evidence

Implementation commit `2f2ec028fadd543ab1fa7f871c3254cb3b9ea745` passed
[hosted CI](https://github.com/usatiuk/sunplayer/actions/runs/39140370120):
Windows, Linux, and AppImage all succeeded. Windows passed all 34 selected
CTests, including `windows-token-store` against real PasswordVault (0.06 s),
and produced the developer bundle and unsigned MSIX. Both native SDK builds
and packaging completed without a follow-up code fix.

The Windows vcpkg cache restored all 24 required packages in 2.9 s; keeping the
triplet unchanged preserved those cached ABIs. No new third-party dependency
was added. The metadata-settings failure is not claimed fixed by a backend
round trip; combined failures now remain visible for installed acceptance.

[Download the tested Windows MSIX artifact](https://github.com/usatiuk/sunplayer/actions/runs/39140370120/artifacts/12142125131).
Use this artifact for the user's installed AppSilo sign-in with Remember me,
restart/restore, and sign-out/deletion check. Hosted desktop success does not
replace that package acceptance. No local Windows/macOS execution occurred.
