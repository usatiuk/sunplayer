# Microsoft Store package

SunPlayer packages the self-contained Windows tree produced by
`cmake --install`. Qt, CMake, and vcpkg own runtime deployment; Microsoft
`winapp` owns manifest processing, PRI generation, architecture stamping,
packing, and optional signing.

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
application data. Windows displays it as `SunPlayer (Dev)`, and its separate
package identity allows it to coexist with the eventual Store package. The
target returns after launching the application. It exercises package identity
and activation, but it does not construct or install an MSIX and therefore
does not replace the signed package flow below.

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

The wrapper only materializes those four values into the reviewed manifest and
calls `winapp package`. It does not deploy dependencies, install tools, unpack
the result, or duplicate Microsoft package validation. The Store package is
unsigned; Partner Center signs it after certification.

## CI package artifact

Every Windows CI run packages the verified Release install tree with the
unsigned `SunPlayerDevelopment` identity, so pull requests exercise the complete
packaging boundary. Trusted main pushes and manual workflow runs additionally
upload the MSIX for seven days. No certificates or secrets are involved. This
is development evidence, not a Store-submittable package; the three identity
values and version must be replaced with the reserved Partner Center values for
a Store release.

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
launch the MSIX. When finished, remove the package, delete the exact trusted
certificate by the exported `.cer` thumbprint, and delete the PFX/CER files.
Generated packages and development certificates are ignored by Git.

```powershell
winapp cert install .\out\SunPlayerDevelopment.pfx --password $password
Add-AppxPackage .\out\SunPlayerDevelopment_1.0.0.0_x64.msix
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
and packaged activation remain release validation. Generate reviewed icon
variants from `branding\SunPlayer.svg` with:

```powershell
Push-Location .\packaging\windows
winapp manifest update-assets .\branding\SunPlayer.svg `
  --manifest .\Package.appxmanifest.in
Pop-Location
```
