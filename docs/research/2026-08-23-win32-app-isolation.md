# Win32 App Isolation for the Windows package

Date: 2026-08-23

## Question

Can the existing Qt/FFmpeg SunPlayer executable run as a recent-Windows
`appSilo` application without FullTrust, broad capabilities, a helper process,
or changes to FFmpeg I/O?

## Current platform facts

* Microsoft describes Win32 App Isolation as an AppContainer-based boundary for
  existing Win32 applications on Windows 11 24H2 (build 26100) and newer. The
  feature is still marked Preview.
* Microsoft's authored manifest example uses a legacy
  `Windows.FullTrustApplication` entry point plus `uap18` overrides for
  `Isolated.App`, `appContainer`, and `appSilo`. It says to remove
  `runFullTrust` unless an extension needs it.
* Current local tooling does not accept that exact combination. Windows SDK
  BuildTools 10.0.28000.2526 returned `0x80080204`, saying the legacy entry
  point requires `runFullTrust`.
* A stricter application declaration with no legacy entry point and no
  capabilities was accepted and launched on Windows 11 25H2 build 26200:

  ```xml
  <Application
    Id="SunPlayer"
    Executable="$targetnametoken$.exe"
    uap18:EntryPoint="Isolated.App"
    uap18:TrustLevel="appContainer"
    uap18:RuntimeBehavior="appSilo">
  ```

* `TokenIsAppContainer` and `TokenIsAppSilo` are separate Windows token facts.
  The latter is necessary to distinguish appSilo from an ordinary
  AppContainer launch.
* appSilo grants implicit access to files selected through a Windows file
  dialog, supplied through file-type activation, or dropped by the user.

Primary sources:

* <https://learn.microsoft.com/en-us/windows/win32/secauthz/app-isolation-overview>
* <https://learn.microsoft.com/en-us/windows/win32/secauthz/app-isolation-packaging-with-vs>
* <https://learn.microsoft.com/en-us/windows/win32/secauthz/app-isolation-app-consent>
* <https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_information_class>

## Feasibility prototype

The prototype was deliberately uncommitted and was reverted after the test.
It made four temporary changes:

1. Used the strict `uap18` application declaration above with
   `MaxVersionTested="10.0.26226.0"`, no base entry point, and an empty
   `Capabilities` element.
2. Reported `TokenIsAppContainer` and `TokenIsAppSilo` in copied diagnostics.
3. Routed Windows Open media through a minimal `IFileOpenDialog` call instead
   of Qt Quick's native `FileDialog` helper.
4. Routed external HTTPS links through
   `Windows.System.Launcher::LaunchUriAsync` instead of
   `QDesktopServices::openUrl` / `ShellExecute`.

Observed result on Windows 11 25H2:

* package registration and launch succeeded;
* diagnostics reported `Windows AppContainer token: yes` and
  `Windows appSilo token: yes`;
* Open media returned a nonempty path and unchanged FFmpeg opened it;
* HDR HEVC playback used D3D11VA, audio used WASAPI, and a video frame was
  presented;
* Source code opened in the system browser through the WinRT launcher.

This proves feasibility on the tested machine. It does not prove Store
certification or every serviced 24H2 build.

### Plain AppContainer comparison

The same installed executable and direct `IFileOpenDialog` implementation were
also launched in a disposable package using `packagedClassicApp` with an
`appContainer` trust level, no appSilo runtime, no FullTrust declaration, and
an empty `Capabilities` element. The dialog opened, but it could not provide
normal filesystem contents to browse, so no file could be selected through it.
Drag/drop still opened an explicitly supplied file, and external HTTPS
activation through `Windows.System.Launcher` still worked. This isolates the
successful picker result above to appSilo's Win32 shell and file-dialog
compatibility brokering rather than to the direct picker alone. It also shows
why drag/drop is not an adequate proxy for picker compatibility: it bypasses
filesystem browsing and supplies one user-chosen item directly.

The comparison package was only a runtime experiment. It is not a supported
package variant and is not part of the production manifest.

## Why Qt needs two narrow Windows exceptions

The earlier appSilo experiment established that Qt Quick's native file picker
and `QDesktopServices::openUrl` did not work, while drag/drop did. Microsoft
also has an open Win32 App Isolation picker issue:
<https://github.com/microsoft/win32-app-isolation/issues/88>.

The direct standard `IFileOpenDialog` path worked in the strict prototype and
preserved the intended consent grant. It is preferable to adding Windows App
SDK solely for one picker. `Microsoft.Windows.Storage.Pickers` remains a valid
fallback if the direct path later fails on a supported Windows build, but its
CMake consumption and framework deployment would add real packaging surface.

`Windows.System.Launcher::LaunchUriAsync` is an OS WinRT API available to
desktop applications and returns whether Windows launched the URI handler. It
requires neither Windows App SDK nor a network capability for the HTTPS links
SunPlayer delegates to the user's browser:
<https://learn.microsoft.com/en-us/uwp/api/windows.system.launcher.launchuriasync>.

## Accepted constraints

* Windows package: strict appSilo only; no `Windows.FullTrustApplication` and no
  `runFullTrust`.
* Unpackaged developer builds remain supported.
* Isolation state does not gate startup or playback. SunPlayer records the two
  token facts in copied diagnostics. For the specifically broken
  AppContainer-without-appSilo state, it shows a small non-modal Player-screen
  warning and emits a technical log entry; it never deliberately exits because
  of a token result.
* No broad filesystem, publisher-directory, prompt-for-access, or network
  capability is added speculatively.
* No `StorageFile`, custom FFmpeg `AVIOContext`, copied media, helper process,
  or alternate package is needed.
* The Windows-specific picker, URI launcher, and token queries belong in one
  small platform module rather than in QML or media code.

## Remaining release gates

* Repeat local and UNC/NAS file selection in the finished package.
* Verify Open with and drag/drop consent independently.
* Verify both external links, WASAPI, D3D11VA, seeking, SDR, and HDR.
* Validate a signed MSIX and upload an `.msixupload` to a Partner Center draft.
  appSilo's Preview status makes Store acceptance an external fact to test,
  not something the repository can assert.
