# Microsoft Store MSIX

SunPlayer's Windows Store pipeline turns the self-contained tree produced by
`cmake --install` into one validated MSIX. Qt owns deployment of Qt and vcpkg
runtime dependencies; `Pack-WindowsStore.ps1` owns the package boundary.

The current packaging toolchain is intentionally pinned:

- Microsoft `winapp` CLI 0.6.0;
- `Microsoft.Windows.SDK.BuildTools` 10.0.28000.2526;
- the x64 Visual C++ CRT selected by CMake for the compiler that built the app.

`winapp` is still a public-preview tool. Version changes are deliberate source
changes followed by the same loose-layout and package checks documented here.

## Prerequisites

Use a Release install tree that already passes its direct QML smoke:

```powershell
cmake --build build --config Release --target sunplayer
$installPrefix = Join-Path (Resolve-Path .) 'out\windows-install'
cmake --install build --config Release --prefix $installPrefix
& "$installPrefix\bin\sunplayer.exe" --verify-qml --no-log-file
```

Run packaging commands from an x64 Visual Studio developer PowerShell. Install
the pinned CLI once with either:

```powershell
winget install --exact --id Microsoft.WinAppCli --version 0.6.0
```

or pass `-BootstrapWinApp`; the script then downloads the official 0.6.0 MSIX,
verifies its checked-in SHA-256, and installs it for the current user.

The output directory must not exist. The script works in a sibling temporary
directory, validates the result, and only then publishes the completed output;
a failed run leaves no partial release artifact unless `-KeepWorkspace` is
explicitly requested for diagnosis. It never mutates the CMake install tree.
CMake installs the matching app-local CRT and Qt skips its redistributable
installer. The final inventory rejects redistributable installers, symbols,
certificates, Qt QML debugging plugins, and the intentionally omitted D3D/DXC
compiler DLLs.

## Fast packaged smoke

This creates a temporary loose package identity, launches
`--verify-qml --no-log-file`, waits for the process, unregisters the identity,
and removes the disposable payload after success:

```powershell
.\packaging\windows\Pack-WindowsStore.ps1 `
  -Mode Loose `
  -InstallPrefix .\out\windows-install `
  -OutputDirectory .\out\msix-loose
```

The only retained output is the materialized manifest under `evidence\`.
Pass `-ApplicationArguments` to run an existing production scenario under the
temporary package identity, for example:

```powershell
.\packaging\windows\Pack-WindowsStore.ps1 `
  -Mode Loose `
  -InstallPrefix .\out\windows-install `
  -OutputDirectory .\out\msix-fullscreen-smoke `
  -ApplicationArguments @( `
    '--fullscreen-smoke', `
    '--no-log-file', `
    (Resolve-Path .\tests\fixtures\media\sdr-bt709-ffv1-video-late-flac.mkv) `
  )
```

## Signed development package

Generate a short-lived local certificate and signed MSIX:

```powershell
$secureDevPassword = Read-Host "Temporary PFX password" -AsSecureString
$devPassword = [System.Net.NetworkCredential]::new('', $secureDevPassword).Password
.\packaging\windows\Pack-WindowsStore.ps1 `
  -Mode Development `
  -InstallPrefix .\out\windows-install `
  -OutputDirectory .\out\msix-development `
  -GenerateDevelopmentCertificate `
  -CertificatePassword $devPassword
```

Trusting a development certificate changes the machine trust store and requires
an elevated terminal. It is for local testing only:

```powershell
winapp cert install .\out\msix-development\SunPlayerDevelopment.pfx `
  --password $devPassword
Add-AppxPackage .\out\msix-development\SunPlayer_1.0.0.0_x64.msix

winapp tool signtool verify /pa /v `
  .\out\msix-development\SunPlayer_1.0.0.0_x64.msix

$package = Get-AppxPackage -Name SunPlayerDevelopment
Start-Process "shell:AppsFolder\$($package.PackageFamilyName)!SunPlayer"
```

After the install/launch smoke, remove the development package. The temporary
certificate is easy to remove independently: resolve the exact thumbprint from
the exported public `.cer`, then delete only that entry from TrustedPeople:

```powershell
Get-AppxPackage -Name SunPlayerDevelopment | Remove-AppxPackage

$thumbprint = (Get-PfxCertificate `
  .\out\msix-development\SunPlayerDevelopment.cer).Thumbprint
Remove-Item "Cert:\LocalMachine\TrustedPeople\$thumbprint"
Remove-Item -LiteralPath `
  .\out\msix-development\SunPlayerDevelopment.pfx, `
  .\out\msix-development\SunPlayerDevelopment.cer
Clear-Variable devPassword, secureDevPassword
```

Generated `.pfx`, `.cer`, and package files are ignored by Git. Never commit a
private key. Deleting the `.pfx` and `.cer` files does not remove an already
trusted certificate; the exact trust-store removal above does.

Before a Partner Center product exists, the unsigned package mechanics can be
rechecked with the clearly non-production development identity:

```powershell
.\packaging\windows\Pack-WindowsStore.ps1 `
  -Mode UnsignedDevelopment `
  -InstallPrefix .\out\windows-install `
  -OutputDirectory .\out\msix-unsigned-development
```

## Unsigned Store package

Reserve the app in Partner Center first. Copy these three values verbatim from
the product identity page into the reviewed `StoreIdentity.json` file:

- package identity name;
- publisher distinguished name;
- publisher display name.

Set `project(SunPlayer VERSION ...)` to the approved public version. Store mode
derives `major.minor.patch.0` from that version and rejects a zero major or any
component outside `0..65535`. It also checks the executable's CMake-derived
Windows ProductVersion, so a stale install tree cannot be packaged under a newer
version.
Then run:

```powershell
.\packaging\windows\Pack-WindowsStore.ps1 `
  -Mode Store `
  -InstallPrefix .\out\windows-install `
  -OutputDirectory .\out\msix-store
```

Store mode rejects incomplete source identity, command-line identity/version
overrides, and all certificate options. The result is intentionally unsigned
because the Store signs the submitted package. Upload the `.msix` manually in
Partner Center and retain the accompanying manifest, toolchain, file inventory,
and SHA-256 evidence.

The GitHub `CI` workflow exposes the same operation behind the
`store_package` manual-dispatch input. A Store packaging run still executes the
normal Linux and Windows tests, builds the existing Release install artifact,
and then uploads the unsigned MSIX plus evidence. It has no Partner Center
credentials and does not submit or publish anything.

## Manifest and assets

`Package.appxmanifest.in` fixes the stable application ID `SunPlayer`, desktop
full-trust entry point, `en-US` resource language, Windows build 17763 minimum,
build 26200 maximum-tested value, and the sole `runFullTrust` capability. It
does not claim AppContainer isolation and does not request broad filesystem or
network capabilities.

The checked-in raster assets are generated from `branding\SunPlayer.svg`:

```powershell
Push-Location .\packaging\windows
winapp manifest update-assets .\branding\SunPlayer.svg `
  --manifest .\Package.appxmanifest.in
Pop-Location
```

Review the resulting scale and target-size images before committing them. The
generated ICO and CMake-derived Windows `VERSIONINFO` are compiled into
`sunplayer.exe` by `SunPlayer.rc.in`. Packaging
generates `resources.pri` so Windows can resolve qualified scale and target-size
assets. `winapp` 0.6.0 also includes its relative-only `pri.resfiles` and
`priconfig.xml` inputs in the MSIX; the validator checks those references and
records all generated PRI files in the package inventory.
