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
$staging = Join-Path ([System.IO.Path]::GetTempPath()) ("sunplayer-winapp-run-" + [guid]::NewGuid().ToString("N"))
$stagingAssets = Join-Path $staging "Assets"
$manifestPath = Join-Path $staging "Package.appxmanifest"

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

try {
    Copy-Item -LiteralPath $install -Destination $staging -Recurse
    New-Item -ItemType Directory -Path $stagingAssets | Out-Null
    Get-ChildItem -LiteralPath $assets -File -Filter "*.png" |
        Copy-Item -Destination $stagingAssets
    [System.IO.File]::WriteAllText($manifestPath, $manifest, [System.Text.UTF8Encoding]::new($false))

    $resourcesPri = Join-Path $outputAppx "resources.pri"
    if (Test-Path -LiteralPath $resourcesPri -PathType Leaf) {
        Remove-Item -LiteralPath $resourcesPri -Force
    }

    Push-Location $scriptDirectory
    try {
        & winapp run $staging `
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
} finally {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
}
