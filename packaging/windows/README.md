# Microsoft Store package

SunPlayer packages the Windows Store payload produced by
`cmake --install`. Qt, CMake, and vcpkg own runtime deployment; Microsoft
`winapp` owns manifest processing, PRI generation, architecture stamping,
packing, and optional signing.

The manifest requires Windows 11 24H2 (build 26100) or newer and declares
`Microsoft.VCLibs.140.00.UWPDesktop` at minimum version `14.0.33728.0`. The
Store installs and services that Visual C++ runtime framework. Qt's VCRedist
deployment is disabled, so neither its installer nor app-local runtime DLLs are
package payload. Windows supplies the D3D11, DXGI, WASAPI, Media Foundation,
Win32, and Schannel system components used by the application.

For local packaging, install the tested CLI once:

```powershell
winget install --exact --id Microsoft.WinAppCli --version 0.6.0
```

The packaging script does not download or install tools. `winapp.yaml` pins
the SDK BuildTools used by the installed CLI. GitHub Actions provisions the
same CLI version with Microsoft's commit-pinned setup action and verifies the
downloaded release files against reviewed SHA-256 digests before invoking the
script.

## Run with development package identity

The Windows-only `sunplayer_run_with_identity` target installs the active
build configuration into an isolated build-tree directory, registers that
tree as a loose development package, and launches SunPlayer:

```powershell
cmake --build build --target sunplayer_run_with_identity
```

This uses `winapp run`, so it requires Windows Developer Mode but does not
require a signing certificate or an elevated terminal. Re-running the target
refreshes the registered `SunPlayerDevelopment` layout while preserving its
application data. The wrapper stages the installed tree, resolved manifest, and
package assets together so `winapp` indexes the same icon candidates as the
MSIX path. Windows displays it as `SunPlayer (Dev)`, and its separate package
identity allows it to coexist with the eventual Store package. The target
returns after launching the application. It exercises package identity and
activation, but it does not construct or install an MSIX and therefore does not
replace the signed package flow below.

The development registration remains after SunPlayer exits. Remove it from
the generated layout directory before installing a signed development MSIX:

```powershell
Push-Location .\build\sunplayer-development\AppX
winapp unregister
Pop-Location
```

## Build the package input

Run from a Visual Studio developer PowerShell and use an absolute install
prefix:

```powershell
cmake --build build --config Release
$install = Join-Path (Resolve-Path .) 'out\windows-install'
cmake --install build --config Release --prefix $install
& "$install\bin\sunplayer.exe" --verify-qml --no-log-file
```

After vcpkg and Qt deployment, installation generates
`share/sunplayer/ThirdPartyNotices.txt` from their local metadata and notice
files. A deployed dependency runtime without one unambiguous metadata owner
fails installation. No third-party license text or component catalog is
checked into SunPlayer.

## Create the unsigned Store MSIX

Reserve SunPlayer in Partner Center first. Copy the package identity name,
publisher distinguished name, and publisher display name exactly from its
Product identity page. Use an approved four-part Store version ending in `.0`.

```powershell
.\packaging\windows\Package-WindowsStore.ps1 `
  -InstallPrefix $install `
  -Output .\out\SunPlayer_1.0.0.0_x64.msix `
  -PackageVersion 1.0.0.0 `
  -IdentityName 'VALUE-FROM-PARTNER-CENTER' `
  -Publisher 'VALUE-FROM-PARTNER-CENTER' `
  -PublisherDisplayName 'VALUE-FROM-PARTNER-CENTER'
```

The wrapper checks the required package files, materializes the identity and
version values into the reviewed manifest, and calls `winapp package`. It does
not deploy dependencies, install tools, unpack the result, or duplicate
Microsoft package validation. The Store package is unsigned; Partner Center
signs it after certification.

## CI and release packages

Pull requests and main pushes package the Release install tree with the unsigned
`SunPlayerDevelopment` identity. Main pushes retain the development bundle and
MSIX for seven days; pull requests retain neither.

After reserving SunPlayer, configure these public GitHub repository variables
from Partner Center's Product identity page:

* `WINDOWS_STORE_IDENTITY_NAME`
* `WINDOWS_STORE_PUBLISHER`
* `WINDOWS_STORE_PUBLISHER_DISPLAY_NAME`

To release, open **Actions → CI / Release → Run workflow** on `main` and choose
`major`, `minor`, or `patch`. The first Store release starts from the checked-in
`0.1.0`, so choose `major` to produce SunPlayer `1.0.0`, package version
`1.0.0.0`, tag `v1.0.0`, and the matching GitHub Release.

The release uses `RelWithDebInfo`. Its `.msixupload` contains the unsigned x64
MSIX and an `.appxsym` containing the complete linker-produced
`sunplayer.pdb`. Download the MSIXUPLOAD from the GitHub Release and submit it
manually to Partner Center. No certificate, GitHub Secret, Store credential, or
automatic Store submission is involved.

## Local signed package

For a local install test, use the development identity consistently in the
manifest and certificate:

```powershell
$securePassword = Read-Host 'Temporary PFX password' -AsSecureString
$password = [System.Net.NetworkCredential]::new('', $securePassword).Password

winapp cert generate `
  --publisher 'CN=SunPlayerDevelopment' `
  --output .\out\SunPlayerDevelopment.pfx `
  --password $password `
  --valid-days 30 `
  --export-cer

.\packaging\windows\Package-WindowsStore.ps1 `
  -InstallPrefix $install `
  -Output .\out\SunPlayerDevelopment_1.0.0.0_x64.msix `
  -PackageVersion 1.0.0.0 `
  -IdentityName SunPlayerDevelopment `
  -Publisher 'CN=SunPlayerDevelopment' `
  -PublisherDisplayName 'SunPlayer Development' `
  -CertificatePath .\out\SunPlayerDevelopment.pfx `
  -CertificatePassword $password
```

From an elevated terminal, trust the temporary certificate, then install and
launch the MSIX. Store installation resolves the declared VCLibs framework,
but `Add-AppxPackage` does not download it for a local sideload. Verify the x64
Desktop framework first; on a clean validation machine, supply the Retail x64
framework `.appx` from the matching Windows SDK through `-DependencyPath`.
When finished, remove the package, delete the exact trusted certificate by the
exported `.cer` thumbprint, and delete the PFX/CER files. Generated packages and
development certificates are ignored by Git.

```powershell
winapp cert install .\out\SunPlayerDevelopment.pfx --password $password
$vclibs = Get-AppxPackage -Name Microsoft.VCLibs.140.00.UWPDesktop |
  Where-Object Architecture -eq X64 |
  Where-Object { $_.Version -ge [version]'14.0.33728.0' }
if ($vclibs) {
  Add-AppxPackage .\out\SunPlayerDevelopment_1.0.0.0_x64.msix
} else {
  $vclibsAppx = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs.Desktop\14.0\Appx\Retail\x64\Microsoft.VCLibs.x64.14.00.Desktop.appx'
  Add-AppxPackage .\out\SunPlayerDevelopment_1.0.0.0_x64.msix `
    -DependencyPath $vclibsAppx
}
$package = Get-AppxPackage -Name SunPlayerDevelopment
Start-Process "shell:AppsFolder\$($package.PackageFamilyName)!SunPlayer"
```

Before release, use an associated local file's **Open with** menu to select
SunPlayer and verify a path containing spaces and non-ASCII characters. Also
check one UNC path and a multi-selection: `MultiSelectModel="Single"` means
Windows activates only the first selected file, not that SunPlayer is a
single-instance application. Do not change the machine's default app as part
of this check. Representative playback should include the seven advertised
demuxer families, including a real 192-byte MTS/M2TS transport stream; package
construction proves schema validity, not every codec/profile combination.

After the launch/playback smoke, clean up from the elevated terminal:

```powershell
Get-AppxPackage -Name SunPlayerDevelopment | Remove-AppxPackage
$thumbprint = (Get-PfxCertificate `
  .\out\SunPlayerDevelopment.cer).Thumbprint
Remove-Item "Cert:\LocalMachine\TrustedPeople\$thumbprint"
Remove-Item .\out\SunPlayerDevelopment.pfx, `
  .\out\SunPlayerDevelopment.cer
Clear-Variable password, securePassword
```

## Maintained scope

The checked-in manifest is an x64 packaged-classic desktop application using
`Windows.FullTrustApplication` and only the required `runFullTrust` capability.
It declares one common-video-container file-type association for `.avi`,
`.flv`, `.m2ts`, `.m4v`, `.mkv`, `.mov`, `.mp4`, `.mpeg`, `.mpg`, `.mts`,
`.ts`, `.webm`, and `.wmv`, with
`MultiSelectModel="Single"`. Package installation makes SunPlayer available to
Windows Open with/default-app selection without taking over the user's default;
Windows launches the full-trust executable with the selected local path through
the same positional-argument boundary as direct startup. The manifest
intentionally has no AppContainer variant, custom verb or activation handler,
execution alias, background task, or Store-submission automation. Additional
specialist, raw-stream, playlist, image, and audio-only extensions are not
claimed merely because FFmpeg can demux them. The FFmpeg dependency test pins
availability of every advertised container family; representative playback
and packaged activation remain release validation. The shared authored icon is
also the Qt runtime icon. Its checked-in Windows package assets and executable
`.ico` are generated manually from that source, including the required default,
dark-unplated, and light-unplated AppList variants. The generator also supplies
the themed assets for the Windows 11 Store package:

```powershell
Push-Location .\packaging\windows
winapp manifest update-assets ..\..\src\app\icons\SunPlayer.png `
  --light-image ..\..\src\app\icons\SunPlayer.png `
  --manifest .\Package.appxmanifest.in
Pop-Location
```
