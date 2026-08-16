# Microsoft Store package

SunPlayer packages the self-contained Windows tree produced by
`cmake --install`. Qt, CMake, and vcpkg own runtime deployment; Microsoft
`winapp` owns manifest processing, PRI generation, architecture stamping,
packing, and optional signing.

Install the tested CLI once:

```powershell
winget install --exact --id Microsoft.WinAppCli --version 0.6.0
```

The repository does not download or install packaging tools. `winapp.yaml`
pins the SDK BuildTools used by the installed CLI.

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
It intentionally has no AppContainer variant, file associations, execution
alias, background task, or Store-submission automation. Generate reviewed icon
variants from `branding\SunPlayer.svg` with:

```powershell
Push-Location .\packaging\windows
winapp manifest update-assets .\branding\SunPlayer.svg `
  --manifest .\Package.appxmanifest.in
Pop-Location
```
