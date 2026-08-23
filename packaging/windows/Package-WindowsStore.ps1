[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$InstallPrefix,
    [Parameter(Mandatory = $true)] [string]$Output,
    [Parameter(Mandatory = $true)] [string]$PackageVersion,
    [Parameter(Mandatory = $true)] [string]$IdentityName,
    [Parameter(Mandatory = $true)] [string]$Publisher,
    [Parameter(Mandatory = $true)] [string]$PublisherDisplayName,
    [string]$DisplayName = "SunPlayer",
    [string]$CertificatePath,
    [string]$CertificatePassword
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$install = [System.IO.Path]::GetFullPath($InstallPrefix)
$outputPath = [System.IO.Path]::GetFullPath($Output)
$scriptDirectory = $PSScriptRoot
$template = Join-Path $scriptDirectory "Package.appxmanifest.in"
$assets = Join-Path $scriptDirectory "Assets"
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("sunplayer-msix-" + [guid]::NewGuid().ToString("N"))
$certificate = $null

foreach ($required in @(
    'bin\sunplayer.exe',
    'share\sunplayer\LICENSE',
    'share\sunplayer\PRIVACY.md',
    'share\sunplayer\ThirdPartyNotices.txt')) {
    $requiredPath = Join-Path $install $required
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf) -or
        (Get-Item -LiteralPath $requiredPath).Length -eq 0) {
        throw "InstallPrefix is missing required package file: $required"
    }
}
if ([string]::IsNullOrWhiteSpace($CertificatePath)) {
    if (-not [string]::IsNullOrWhiteSpace($CertificatePassword)) {
        throw "CertificatePassword requires CertificatePath."
    }
} else {
    $certificate = [System.IO.Path]::GetFullPath($CertificatePath)
    $installPrefixWithSeparator = $install.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    if ($certificate.StartsWith($installPrefixWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "CertificatePath must be outside InstallPrefix."
    }
}

$manifest = (Get-Content -LiteralPath $template -Raw).
    Replace("@IDENTITY_NAME@", [System.Security.SecurityElement]::Escape($IdentityName)).
    Replace("@PUBLISHER@", [System.Security.SecurityElement]::Escape($Publisher)).
    Replace("@PUBLISHER_DISPLAY_NAME@", [System.Security.SecurityElement]::Escape($PublisherDisplayName)).
    Replace("@DISPLAY_NAME@", [System.Security.SecurityElement]::Escape($DisplayName)).
    Replace("@PACKAGE_VERSION@", [System.Security.SecurityElement]::Escape($PackageVersion))

try {
    New-Item -ItemType Directory -Path $temporary | Out-Null
    Copy-Item -LiteralPath $assets -Destination (Join-Path $temporary "Assets") -Recurse
    $manifestPath = Join-Path $temporary "Package.appxmanifest"
    [System.IO.File]::WriteAllText($manifestPath, $manifest, [System.Text.UTF8Encoding]::new($false))

    $arguments = @(
        "package", $install,
        "--manifest", $manifestPath,
        "--executable", "bin\sunplayer.exe",
        "--output", $outputPath
    )
    if ($null -ne $certificate) {
        $arguments += @("--cert", $certificate)
        if (-not [string]::IsNullOrWhiteSpace($CertificatePassword)) {
            $arguments += @("--cert-password", $CertificatePassword)
        }
    }

    Push-Location $scriptDirectory
    try {
        & winapp @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "winapp package failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
} finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
}
