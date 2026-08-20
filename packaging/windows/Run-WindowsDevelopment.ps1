[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$InstallPrefix,
    [Parameter(Mandatory = $true)] [string]$OutputAppxDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$install = [System.IO.Path]::GetFullPath($InstallPrefix)
$outputAppx = [System.IO.Path]::GetFullPath($OutputAppxDirectory)
$scriptDirectory = $PSScriptRoot
$template = Join-Path $scriptDirectory "Package.appxmanifest.in"
$assets = Join-Path $scriptDirectory "Assets"
$manifestDirectory = Join-Path (Split-Path $outputAppx -Parent) "manifest"
$manifestAssets = Join-Path $manifestDirectory "Assets"

if (-not (Test-Path -LiteralPath (Join-Path $install "bin\sunplayer.exe") -PathType Leaf)) {
    throw "InstallPrefix must contain bin\sunplayer.exe."
}

$installWithSeparator = $install.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
$outputAppxWithSeparator = $outputAppx.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if ($outputAppx.Equals($install, [System.StringComparison]::OrdinalIgnoreCase) -or
        $outputAppx.StartsWith($installWithSeparator, [System.StringComparison]::OrdinalIgnoreCase) -or
        $install.StartsWith($outputAppxWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "InstallPrefix and OutputAppxDirectory must not overlap."
}

$manifest = (Get-Content -LiteralPath $template -Raw).
    Replace("@IDENTITY_NAME@", "SunPlayerDevelopment").
    Replace("@PUBLISHER@", "CN=SunPlayerDevelopment").
    Replace("@PUBLISHER_DISPLAY_NAME@", "SunPlayer Development").
    Replace("@DISPLAY_NAME@", "SunPlayer (Dev)").
    Replace("@PACKAGE_VERSION@", "1.0.0.0")

New-Item -ItemType Directory -Force -Path $manifestAssets | Out-Null
Copy-Item -Path (Join-Path $assets "*") -Destination $manifestAssets -Recurse -Force
$manifestPath = Join-Path $manifestDirectory "Package.appxmanifest"
[System.IO.File]::WriteAllText($manifestPath, $manifest, [System.Text.UTF8Encoding]::new($false))

Push-Location $scriptDirectory
try {
    & winapp run $install `
        --manifest $manifestPath `
        --output-appx-directory $outputAppx `
        --executable "bin\sunplayer.exe" `
        --detach
    if ($LASTEXITCODE -ne 0) {
        throw "winapp run failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}
