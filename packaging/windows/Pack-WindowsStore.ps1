[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Loose", "Development", "UnsignedDevelopment", "Store")]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$InstallPrefix,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [string]$PackageVersion,
    [string]$IdentityName,
    [string]$Publisher,
    [string]$PublisherDisplayName,
    [string]$CertificatePath,
    [string]$CertificatePassword,
    [string[]]$ApplicationArguments = @("--verify-qml", "--no-log-file"),
    [switch]$GenerateDevelopmentCertificate,
    [switch]$BootstrapWinApp,
    [switch]$KeepWorkspace
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$requiredWinAppVersion = "0.6.0"
$winAppInstallerUri = "https://github.com/microsoft/winappcli/releases/download/v$requiredWinAppVersion/winappcli_x64.msix"
$winAppInstallerSha256 = "DC5D323F6D1601EF3342420746F0163651176F4CC183690F0354546A36648EEC"
$applicationId = "SunPlayer"
$relativeExecutable = "bin\sunplayer.exe"

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [switch]$Quiet
    )

    if ($Quiet) {
        $output = & $FilePath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            throw "Command failed with exit code ${exitCode}: $FilePath`n$($output -join [Environment]::NewLine)"
        }
    } else {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
        }
    }
}

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
}

function Test-PathWithin {
    param(
        [Parameter(Mandatory = $true)][string]$Candidate,
        [Parameter(Mandatory = $true)][string]$Parent
    )

    $prefix = $Parent.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    return $Candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Assert-PackageVersion {
    param([Parameter(Mandatory = $true)][string]$Version)

    if ($Version -notmatch '^([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)$') {
        throw "PackageVersion must contain four numeric components: Major.Minor.Build.Revision."
    }

    $parts = @()
    foreach ($component in $Version.Split('.')) {
        [uint32]$value = 0
        if (-not [uint32]::TryParse($component, [ref]$value) -or $value -gt 65535) {
            throw "Every PackageVersion component must be in the range 0..65535."
        }
        $parts += $value
    }
    if ($parts[0] -eq 0) {
        throw "The Store requires a non-zero major PackageVersion component."
    }
    if ($parts[3] -ne 0) {
        throw "The Store reserves the fourth PackageVersion component; it must be zero."
    }
}

function Get-StorePackageVersion {
    param([Parameter(Mandatory = $true)][string]$CMakePath)

    $cmake = Get-Content -LiteralPath $CMakePath -Raw
    $match = [regex]::Match($cmake, '(?is)project\s*\(\s*SunPlayer\s+VERSION\s+([0-9]+)\.([0-9]+)\.([0-9]+)')
    if (-not $match.Success) {
        throw "Could not derive the Store package version from project(SunPlayer VERSION ...) in $CMakePath."
    }
    return '{0}.{1}.{2}.0' -f $match.Groups[1].Value, $match.Groups[2].Value, $match.Groups[3].Value
}

function Assert-InstalledApplicationVersion {
    param(
        [Parameter(Mandatory = $true)][string]$ExecutablePath,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion
    )

    $productVersion = (Get-Item -LiteralPath $ExecutablePath).VersionInfo.ProductVersion
    if ($productVersion -ne $ExpectedVersion) {
        throw "The installed executable version does not match CMake project version $ExpectedVersion. Rebuild and reinstall before Store packaging."
    }
}

function Get-StoreIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)

    $identity = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    foreach ($property in @("identityName", "publisher", "publisherDisplayName")) {
        if ($null -eq $identity.PSObject.Properties[$property] -or [string]::IsNullOrWhiteSpace($identity.$property)) {
            throw "Reserve SunPlayer in Partner Center and commit '$property' to $Path before creating a Store package."
        }
    }
    return $identity
}

function Get-BuildToolsVersion {
    param([Parameter(Mandatory = $true)][string]$Path)

    $content = Get-Content -LiteralPath $Path -Raw
    $nameMatches = [regex]::Matches($content, '(?m)^\s*-\s*name:\s*Microsoft\.Windows\.SDK\.BuildTools\s*$')
    $versionMatches = [regex]::Matches($content, '(?m)^\s*version:\s*([0-9]+(?:\.[0-9]+){3})\s*$')
    if ($nameMatches.Count -ne 1 -or $versionMatches.Count -ne 1) {
        throw "winapp.yaml must contain exactly one pinned Microsoft.Windows.SDK.BuildTools package."
    }
    return $versionMatches[0].Groups[1].Value
}

function Get-WinAppCommand {
    param(
        [Parameter(Mandatory = $true)][string]$DownloadDirectory,
        [Parameter(Mandatory = $true)][bool]$AllowBootstrap
    )

    $command = Get-Command winapp -ErrorAction SilentlyContinue
    if ($null -eq $command -and $AllowBootstrap) {
        $installer = Join-Path $DownloadDirectory "winappcli_x64.msix"
        Invoke-WebRequest -Uri $winAppInstallerUri -OutFile $installer -UseBasicParsing
        $actualHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
        if ($actualHash -ne $winAppInstallerSha256) {
            throw "Downloaded winapp installer hash mismatch. Expected $winAppInstallerSha256, got $actualHash."
        }
        Add-AppxPackage -Path $installer
        $command = Get-Command winapp -ErrorAction SilentlyContinue
    }

    if ($null -eq $command) {
        throw "winapp $requiredWinAppVersion is required. Install Microsoft.WinAppCli or pass -BootstrapWinApp."
    }

    $actualVersion = (& $command.Source --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualVersion -ne $requiredWinAppVersion) {
        throw "winapp $requiredWinAppVersion is required; found '$actualVersion'."
    }

    return $command.Source
}

function Assert-X64PortableExecutable {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Not a PE executable: $Path"
        }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -gt ($stream.Length - 6)) {
            throw "Invalid PE header offset in $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Invalid PE signature in $Path"
        }
        $machine = $reader.ReadUInt16()
        if ($machine -ne 0x8664) {
            throw ('SunPlayer Store packaging currently supports only x64 PE images; machine is 0x{0:X4}.' -f $machine)
        }
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Write-MaterializedManifest {
    param(
        [Parameter(Mandatory = $true)][string]$TemplatePath,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$PublisherDn,
        [Parameter(Mandatory = $true)][string]$DisplayPublisher,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $content = Get-Content -LiteralPath $TemplatePath -Raw
    $content = $content.Replace("@IDENTITY_NAME@", [System.Security.SecurityElement]::Escape($Name))
    $content = $content.Replace("@PUBLISHER@", [System.Security.SecurityElement]::Escape($PublisherDn))
    $content = $content.Replace("@PUBLISHER_DISPLAY_NAME@", [System.Security.SecurityElement]::Escape($DisplayPublisher))
    $content = $content.Replace("@PACKAGE_VERSION@", [System.Security.SecurityElement]::Escape($Version))
    if ($content -match '@[A-Z_]+@') {
        throw "The materialized manifest still contains an unresolved token."
    }
    [System.IO.File]::WriteAllText($DestinationPath, $content, [System.Text.UTF8Encoding]::new($false))
}

function Assert-ManifestContract {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$ExpectedManifestPath,
        [Parameter(Mandatory = $true)][string]$ExpectedExecutable,
        [switch]$RequireX64Architecture
    )

    [xml]$manifest = Get-Content -LiteralPath $ManifestPath -Raw
    [xml]$expectedManifest = Get-Content -LiteralPath $ExpectedManifestPath -Raw
    $namespace = [System.Xml.XmlNamespaceManager]::new($manifest.NameTable)
    $namespace.AddNamespace("f", "http://schemas.microsoft.com/appx/manifest/foundation/windows10")
    $namespace.AddNamespace("uap", "http://schemas.microsoft.com/appx/manifest/uap/windows10")
    $namespace.AddNamespace("rescap", "http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities")
    $expectedNamespace = [System.Xml.XmlNamespaceManager]::new($expectedManifest.NameTable)
    $expectedNamespace.AddNamespace("f", "http://schemas.microsoft.com/appx/manifest/foundation/windows10")
    $expectedNamespace.AddNamespace("uap", "http://schemas.microsoft.com/appx/manifest/uap/windows10")
    $expectedNamespace.AddNamespace("rescap", "http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities")

    $identity = $manifest.SelectSingleNode("/f:Package/f:Identity", $namespace)
    $expectedIdentity = $expectedManifest.SelectSingleNode("/f:Package/f:Identity", $expectedNamespace)
    $application = $manifest.SelectSingleNode("/f:Package/f:Applications/f:Application", $namespace)
    $expectedApplication = $expectedManifest.SelectSingleNode("/f:Package/f:Applications/f:Application", $expectedNamespace)
    $target = $manifest.SelectSingleNode("/f:Package/f:Dependencies/f:TargetDeviceFamily", $namespace)
    $expectedTarget = $expectedManifest.SelectSingleNode("/f:Package/f:Dependencies/f:TargetDeviceFamily", $expectedNamespace)
    $languages = @($manifest.SelectNodes("/f:Package/f:Resources/f:Resource", $namespace) | ForEach-Object { $_.Language })
    $expectedLanguages = @($expectedManifest.SelectNodes("/f:Package/f:Resources/f:Resource", $expectedNamespace) | ForEach-Object { $_.Language })
    $capabilities = @($manifest.SelectNodes("/f:Package/f:Capabilities/*", $namespace))
    $visualElements = $manifest.SelectSingleNode("/f:Package/f:Applications/f:Application/uap:VisualElements", $namespace)
    $expectedVisualElements = $expectedManifest.SelectSingleNode("/f:Package/f:Applications/f:Application/uap:VisualElements", $expectedNamespace)
    $defaultTile = $manifest.SelectSingleNode("/f:Package/f:Applications/f:Application/uap:VisualElements/uap:DefaultTile", $namespace)
    $expectedDefaultTile = $expectedManifest.SelectSingleNode("/f:Package/f:Applications/f:Application/uap:VisualElements/uap:DefaultTile", $expectedNamespace)

    if ($null -eq $identity -or $null -eq $expectedIdentity -or
        $identity.Name -ne $expectedIdentity.Name -or
        $identity.Publisher -ne $expectedIdentity.Publisher -or
        $identity.Version -ne $expectedIdentity.Version) {
        throw "The package identity does not match the reviewed manifest."
    }
    if ($RequireX64Architecture -and $identity.ProcessorArchitecture -ne "x64") {
        throw "The packaged manifest ProcessorArchitecture must be x64."
    }
    if ($null -eq $application -or $null -eq $expectedApplication -or
        $expectedApplication.Id -ne $applicationId -or
        $application.Id -ne $expectedApplication.Id -or
        $application.Executable -ne $ExpectedExecutable -or
        $application.EntryPoint -ne $expectedApplication.EntryPoint -or
        $application.EntryPoint -ne "Windows.FullTrustApplication") {
        throw "The package application contract is invalid."
    }
    if ($null -eq $target -or $null -eq $expectedTarget -or
        $target.Name -ne $expectedTarget.Name -or
        $target.MinVersion -ne $expectedTarget.MinVersion -or
        $target.MaxVersionTested -ne $expectedTarget.MaxVersionTested) {
        throw "The package OS targeting contract is invalid."
    }
    if (($languages -join '|') -ne ($expectedLanguages -join '|')) {
        throw "The package resource languages do not match the reviewed manifest."
    }
    if ($capabilities.Count -ne 1 -or $capabilities[0].LocalName -ne "Capability" -or $capabilities[0].Name -ne "runFullTrust") {
        throw "The package must declare only the runFullTrust restricted capability."
    }
    if ($null -eq $visualElements -or $null -eq $expectedVisualElements) {
        throw "The package visual elements are missing."
    }
    foreach ($attribute in @("DisplayName", "Description", "BackgroundColor", "Square150x150Logo", "Square44x44Logo")) {
        if ($visualElements.GetAttribute($attribute) -ne $expectedVisualElements.GetAttribute($attribute)) {
            throw "The package visual element '$attribute' does not match the reviewed manifest."
        }
    }
    if ($null -eq $defaultTile -or $null -eq $expectedDefaultTile -or
        $defaultTile.Wide310x150Logo -ne $expectedDefaultTile.Wide310x150Logo) {
        throw "The package default-tile contract does not match the reviewed manifest."
    }
    foreach ($propertyName in @("DisplayName", "PublisherDisplayName", "Logo")) {
        $value = $manifest.SelectSingleNode("/f:Package/f:Properties/f:$propertyName", $namespace)
        $expectedValue = $expectedManifest.SelectSingleNode("/f:Package/f:Properties/f:$propertyName", $expectedNamespace)
        if ($null -eq $value -or $null -eq $expectedValue -or $value.InnerText -ne $expectedValue.InnerText) {
            throw "The package property '$propertyName' does not match the reviewed manifest."
        }
    }
}

function Assert-PayloadContract {
    param(
        [Parameter(Mandatory = $true)][string]$PayloadPath,
        [switch]$RequireResourceIndex,
        [switch]$RequireScaleResourcePackages
    )

    $required = @(
        (Join-Path $PayloadPath "bin\sunplayer.exe"),
        (Join-Path $PayloadPath "bin\qt.conf"),
        (Join-Path $PayloadPath "bin\msvcp140.dll"),
        (Join-Path $PayloadPath "bin\vcruntime140.dll"),
        (Join-Path $PayloadPath "plugins\platforms\qwindows.dll")
    )
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required packaged runtime file is missing: $path"
        }
    }

    $executables = @(Get-ChildItem -LiteralPath $PayloadPath -File -Recurse -Filter "*.exe")
    $expectedExecutable = Get-FullPath (Join-Path $PayloadPath $relativeExecutable)
    if ($executables.Count -ne 1 -or (Get-FullPath $executables[0].FullName) -ne $expectedExecutable) {
        throw "The package payload must contain only $relativeExecutable; found $($executables.Count) executable files."
    }

    $forbiddenNames = @("vc_redist.x64.exe", "devcert.pfx", "devcert.cer")
    $forbidden = @(Get-ChildItem -LiteralPath $PayloadPath -File -Recurse | Where-Object {
        $forbiddenNames -contains $_.Name -or $_.Extension -in @(".pdb", ".ilk", ".obj", ".lib", ".pfx", ".cer")
    })
    if ($forbidden.Count -ne 0) {
        throw "Forbidden package payload file found: $($forbidden[0].FullName)"
    }

    $graphicsCompiler = @(Get-ChildItem -LiteralPath (Join-Path $PayloadPath "bin") -File | Where-Object {
        $_.Name -like "d3dcompiler_*.dll" -or $_.Name -like "dxcompiler*.dll" -or $_.Name -like "dxil*.dll"
    })
    if ($graphicsCompiler.Count -ne 0) {
        throw "The package must not carry app-local D3D or DXC compiler DLLs: $($graphicsCompiler[0].FullName)"
    }

    $qmlDebuggingPlugins = @(Get-ChildItem -LiteralPath $PayloadPath -File -Recurse | Where-Object {
        $_.FullName -match '[\\/]plugins[\\/]qmltooling[\\/]'
    })
    if ($qmlDebuggingPlugins.Count -ne 0) {
        throw "Release payloads must not contain Qt QML debugging plugins: $($qmlDebuggingPlugins[0].FullName)"
    }

    if ($RequireResourceIndex) {
        Assert-ResourceIndexContract -PayloadPath $PayloadPath -RequireScaleResourcePackages:$RequireScaleResourcePackages
    } else {
        foreach ($generatedName in @("resources.pri", "pri.resfiles", "priconfig.xml")) {
            if (Test-Path -LiteralPath (Join-Path $PayloadPath $generatedName)) {
                throw "The CMake install tree must not contain winapp-generated metadata: $generatedName"
            }
        }
    }
}

function Assert-ResourceIndexContract {
    param(
        [Parameter(Mandatory = $true)][string]$PayloadPath,
        [switch]$RequireScaleResourcePackages
    )

    foreach ($requiredName in @("resources.pri", "pri.resfiles", "priconfig.xml")) {
        if (-not (Test-Path -LiteralPath (Join-Path $PayloadPath $requiredName) -PathType Leaf)) {
            throw "winapp did not create required resource-index metadata: $requiredName"
        }
    }
    if ($RequireScaleResourcePackages -and
        @(Get-ChildItem -LiteralPath $PayloadPath -File -Filter "resources.scale-*.pri").Count -eq 0) {
        throw "winapp did not create any scale-qualified PRI resource package."
    }

    $priConfig = Get-Content -LiteralPath (Join-Path $PayloadPath "priconfig.xml") -Raw
    if ($priConfig -match '(?i)[a-z]:\\') {
        throw "priconfig.xml contains an absolute host path."
    }

    $entries = @(Get-Content -LiteralPath (Join-Path $PayloadPath "pri.resfiles") | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($RequireScaleResourcePackages -and $entries.Count -eq 0) {
        throw "pri.resfiles contains no indexed resources."
    }
    foreach ($entry in $entries) {
        if ([System.IO.Path]::IsPathRooted($entry) -or (($entry -split '[\\/]') -contains '..')) {
            throw "pri.resfiles contains an unsafe resource path: $entry"
        }
        $resourcePath = Get-FullPath (Join-Path $PayloadPath $entry)
        if (-not (Test-PathWithin -Candidate $resourcePath -Parent (Get-FullPath $PayloadPath)) -or
            -not (Test-Path -LiteralPath $resourcePath -PathType Leaf)) {
            throw "pri.resfiles references a missing or out-of-payload resource: $entry"
        }
    }
}

function Write-PackageEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$UnpackedPath,
        [Parameter(Mandatory = $true)][string]$PackagePath,
        [Parameter(Mandatory = $true)][string]$EvidenceDirectory,
        [Parameter(Mandatory = $true)][string]$BuildToolsVersion,
        [Parameter(Mandatory = $true)][string]$PackagingMode
    )

    $inventory = @(Get-ChildItem -LiteralPath $UnpackedPath -File -Recurse | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($UnpackedPath.Length).TrimStart('\').Replace('\', '/')
            size = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    } | Sort-Object path)
    $inventory | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "package-inventory.json") -Encoding UTF8

    $crtFiles = @(Get-ChildItem -LiteralPath (Join-Path $UnpackedPath "bin") -File | Where-Object {
        $_.Name -match '^(msvcp|vcruntime|concrt)[0-9].*\.dll$'
    } | ForEach-Object {
        [ordered]@{
            name = $_.Name
            fileVersion = $_.VersionInfo.FileVersion
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    } | Sort-Object name)
    $toolchain = [ordered]@{
        mode = $PackagingMode
        winapp = $requiredWinAppVersion
        windowsSdkBuildTools = $BuildToolsVersion
        visualCppRuntimeSource = "CMake install tree"
        visualCppRuntime = $crtFiles
    }
    $toolchain | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "toolchain.json") -Encoding UTF8

    $packageHash = Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256
    "{0}  {1}" -f $packageHash.Hash, [System.IO.Path]::GetFileName($PackagePath) | Set-Content -LiteralPath (Join-Path $EvidenceDirectory "package-sha256.txt") -Encoding ASCII
}

$scriptDirectory = Get-FullPath $PSScriptRoot
$repositoryDirectory = Get-FullPath (Join-Path $scriptDirectory "..\..")
$installDirectory = Get-FullPath $InstallPrefix
$outputPath = Get-FullPath $OutputDirectory
$templatePath = Join-Path $scriptDirectory "Package.appxmanifest.in"
$assetSource = Join-Path $scriptDirectory "Assets"
$winAppConfig = Join-Path $scriptDirectory "winapp.yaml"
$storeIdentityPath = Join-Path $scriptDirectory "StoreIdentity.json"
$cmakePath = Join-Path $repositoryDirectory "CMakeLists.txt"

if (-not (Test-Path -LiteralPath $installDirectory -PathType Container)) {
    throw "InstallPrefix is not a directory: $installDirectory"
}
if (-not (Test-Path -LiteralPath (Join-Path $installDirectory $relativeExecutable) -PathType Leaf)) {
    throw "The installed SunPlayer executable is missing: $(Join-Path $installDirectory $relativeExecutable)"
}
if (-not (Test-Path -LiteralPath $templatePath -PathType Leaf) -or -not (Test-Path -LiteralPath $assetSource -PathType Container)) {
    throw "The checked-in manifest template or generated asset directory is missing."
}
if (-not (Test-Path -LiteralPath $winAppConfig -PathType Leaf)) {
    throw "The pinned winapp configuration is missing: $winAppConfig"
}
$buildToolsVersion = Get-BuildToolsVersion -Path $winAppConfig
if (Test-PathWithin -Candidate $outputPath -Parent $installDirectory) {
    throw "OutputDirectory must not be inside InstallPrefix."
}
if (Test-Path -LiteralPath $outputPath) {
    throw "OutputDirectory must not already exist: $outputPath"
}

if ($Mode -eq "Store") {
    foreach ($parameterName in @("PackageVersion", "IdentityName", "Publisher", "PublisherDisplayName")) {
        if ($PSBoundParameters.ContainsKey($parameterName)) {
            throw "$parameterName is derived from reviewed source files and cannot be overridden in Store mode."
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($CertificatePath) -or
        -not [string]::IsNullOrWhiteSpace($CertificatePassword) -or
        $GenerateDevelopmentCertificate) {
        throw "Store packages must be unsigned; certificate options are not allowed in Store mode."
    }
    $storeIdentity = Get-StoreIdentity -Path $storeIdentityPath
    $PackageVersion = Get-StorePackageVersion -CMakePath $cmakePath
    $applicationVersion = $PackageVersion.Substring(0, $PackageVersion.LastIndexOf('.'))
    Assert-InstalledApplicationVersion -ExecutablePath (Join-Path $installDirectory $relativeExecutable) -ExpectedVersion $applicationVersion
    $IdentityName = $storeIdentity.identityName
    $Publisher = $storeIdentity.publisher
    $PublisherDisplayName = $storeIdentity.publisherDisplayName
} else {
    if ([string]::IsNullOrWhiteSpace($PackageVersion)) { $PackageVersion = "1.0.0.0" }
    if ([string]::IsNullOrWhiteSpace($IdentityName)) { $IdentityName = "SunPlayerDevelopment" }
    if ([string]::IsNullOrWhiteSpace($Publisher)) { $Publisher = "CN=SunPlayerDevelopment" }
    if ([string]::IsNullOrWhiteSpace($PublisherDisplayName)) { $PublisherDisplayName = "SunPlayer Development" }
}

Assert-PackageVersion -Version $PackageVersion
if ($IdentityName -notmatch '^[A-Za-z0-9.-]{3,50}$') {
    throw "IdentityName must be 3..50 characters containing only letters, digits, periods, or hyphens."
}
if ($Publisher -notmatch '(^|,)\s*(CN|OU|O|L|S|C)=') {
    throw "Publisher must be a distinguished name, such as CN=Publisher."
}
if ([string]::IsNullOrWhiteSpace($PublisherDisplayName)) {
    throw "PublisherDisplayName must not be empty."
}

if ($Mode -eq "Development") {
    if ($GenerateDevelopmentCertificate -and -not [string]::IsNullOrWhiteSpace($CertificatePath)) {
        throw "Choose either -GenerateDevelopmentCertificate or -CertificatePath, not both."
    }
    if (-not $GenerateDevelopmentCertificate -and [string]::IsNullOrWhiteSpace($CertificatePath)) {
        throw "Development mode requires -CertificatePath or -GenerateDevelopmentCertificate."
    }
    if ($GenerateDevelopmentCertificate -and [string]::IsNullOrWhiteSpace($CertificatePassword)) {
        throw "CertificatePassword is required when generating a development certificate."
    }
} elseif (-not [string]::IsNullOrWhiteSpace($CertificatePath) -or
          -not [string]::IsNullOrWhiteSpace($CertificatePassword) -or
          $GenerateDevelopmentCertificate) {
    throw "Certificate options are valid only in Development mode."
}
if ($Mode -ne "Loose" -and $PSBoundParameters.ContainsKey("ApplicationArguments")) {
    throw "ApplicationArguments are valid only in Loose mode."
}
if ($Mode -eq "Loose" -and ($ApplicationArguments.Count -eq 0 -or @($ApplicationArguments | Where-Object { [string]::IsNullOrWhiteSpace($_) }).Count -ne 0)) {
    throw "Loose mode requires at least one non-empty application argument."
}

$outputParent = Split-Path -Parent $outputPath
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    New-Item -ItemType Directory -Path $outputParent | Out-Null
}
$workspaceName = '.{0}.workspace-{1}-{2}' -f (Split-Path -Leaf $outputPath), $PID, [guid]::NewGuid().ToString('N')
$workspace = Join-Path $outputParent $workspaceName
$payload = Join-Path $workspace "payload"
$manifestPath = Join-Path $workspace "Package.appxmanifest"
$unpacked = Join-Path $workspace "unpacked"
$result = Join-Path $workspace "result"
$evidence = Join-Path $result "evidence"
try {
    New-Item -ItemType Directory -Path $workspace, $payload, $result, $evidence | Out-Null
    $winApp = Get-WinAppCommand -DownloadDirectory $workspace -AllowBootstrap $BootstrapWinApp.IsPresent
    Get-ChildItem -LiteralPath $installDirectory -Force | Copy-Item -Destination $payload -Recurse -Force
    Copy-Item -LiteralPath $assetSource -Destination (Join-Path $workspace "Assets") -Recurse
    Write-MaterializedManifest -TemplatePath $templatePath -DestinationPath $manifestPath -Name $IdentityName -PublisherDn $Publisher -DisplayPublisher $PublisherDisplayName -Version $PackageVersion
    Assert-PayloadContract -PayloadPath $payload
    Assert-X64PortableExecutable -Path (Join-Path $payload $relativeExecutable)
    Assert-ManifestContract -ManifestPath $manifestPath -ExpectedManifestPath $manifestPath -ExpectedExecutable '$targetnametoken$.exe'

    Invoke-NativeCommand -FilePath (Join-Path $payload $relativeExecutable) -Arguments @("--verify-qml", "--no-log-file")

    Push-Location $scriptDirectory
    try {
        if ($Mode -eq "Loose") {
            $looseLayout = Join-Path $workspace "loose"
            $looseArguments = @(
                "run", $payload,
                "--manifest", $manifestPath,
                "--output-appx-directory", $looseLayout,
                "--executable", $relativeExecutable,
                "--unregister-on-exit",
                "--"
            )
            $looseArguments += $ApplicationArguments
            Invoke-NativeCommand -FilePath $winApp -Arguments $looseArguments
            Assert-PayloadContract -PayloadPath $looseLayout -RequireResourceIndex
            Assert-ManifestContract -ManifestPath (Join-Path $looseLayout "AppxManifest.xml") -ExpectedManifestPath $manifestPath -ExpectedExecutable $relativeExecutable -RequireX64Architecture
            Copy-Item -LiteralPath (Join-Path $looseLayout "AppxManifest.xml") -Destination (Join-Path $evidence "AppxManifest.xml")
        } else {
            $packagePath = Join-Path $workspace ("SunPlayer_{0}_x64.msix" -f $PackageVersion)
            $packageArguments = @(
                "package", $payload,
                "--manifest", $manifestPath,
                "--executable", $relativeExecutable,
                "--output", $packagePath
            )

            if ($Mode -eq "Development") {
                if ($GenerateDevelopmentCertificate) {
                    $CertificatePath = Join-Path $workspace "SunPlayerDevelopment.pfx"
                    Invoke-NativeCommand -FilePath $winApp -Arguments @(
                        "cert", "generate",
                        "--publisher", $Publisher,
                        "--output", $CertificatePath,
                        "--password", $CertificatePassword,
                        "--valid-days", "30",
                        "--export-cer"
                    )
                } else {
                    $CertificatePath = Get-FullPath $CertificatePath
                    if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
                        throw "CertificatePath does not exist: $CertificatePath"
                    }
                }
                $packageArguments += @("--cert", $CertificatePath)
                if (-not [string]::IsNullOrWhiteSpace($CertificatePassword)) {
                    $packageArguments += @("--cert-password", $CertificatePassword)
                }
            }

            Invoke-NativeCommand -FilePath $winApp -Arguments $packageArguments
            if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
                throw "winapp did not create the expected package: $packagePath"
            }

            Invoke-NativeCommand -FilePath $winApp -Arguments @("tool", "makeappx", "unpack", "/p", $packagePath, "/d", $unpacked, "/o") -Quiet
            Assert-PayloadContract -PayloadPath $unpacked -RequireResourceIndex -RequireScaleResourcePackages
            Assert-ManifestContract -ManifestPath (Join-Path $unpacked "AppxManifest.xml") -ExpectedManifestPath $manifestPath -ExpectedExecutable $relativeExecutable -RequireX64Architecture

            $signaturePath = Join-Path $unpacked "AppxSignature.p7x"
            if ($Mode -in @("Store", "UnsignedDevelopment") -and (Test-Path -LiteralPath $signaturePath)) {
                throw "Unsigned packages must not contain AppxSignature.p7x."
            }
            if ($Mode -eq "Development" -and -not (Test-Path -LiteralPath $signaturePath -PathType Leaf)) {
                throw "The development package is missing its signature."
            }

            Copy-Item -LiteralPath (Join-Path $unpacked "AppxManifest.xml") -Destination (Join-Path $evidence "AppxManifest.xml")
            Write-PackageEvidence -UnpackedPath $unpacked -PackagePath $packagePath -EvidenceDirectory $evidence -BuildToolsVersion $buildToolsVersion -PackagingMode $Mode
            Move-Item -LiteralPath $packagePath -Destination (Join-Path $result ([System.IO.Path]::GetFileName($packagePath)))
            if ($Mode -eq "Development" -and $GenerateDevelopmentCertificate) {
                Move-Item -LiteralPath $CertificatePath -Destination (Join-Path $result ([System.IO.Path]::GetFileName($CertificatePath)))
                $generatedCer = [System.IO.Path]::ChangeExtension($CertificatePath, ".cer")
                if (-not (Test-Path -LiteralPath $generatedCer -PathType Leaf)) {
                    throw "winapp did not export the expected development certificate: $generatedCer"
                }
                Move-Item -LiteralPath $generatedCer -Destination (Join-Path $result ([System.IO.Path]::GetFileName($generatedCer)))
            }
        }
    } finally {
        Pop-Location
    }

    Move-Item -LiteralPath $result -Destination $outputPath
} finally {
    if (-not $KeepWorkspace) {
        $resolvedWorkspace = Get-FullPath $workspace
        if (-not (Test-PathWithin -Candidate $resolvedWorkspace -Parent (Get-FullPath $outputParent))) {
            throw "Refusing to remove an unexpected workspace path: $resolvedWorkspace"
        }
        if (Test-Path -LiteralPath $resolvedWorkspace) {
            Remove-Item -LiteralPath $resolvedWorkspace -Recurse -Force
        }
    }
}

Write-Output "SunPlayer $Mode packaging completed: $outputPath"
