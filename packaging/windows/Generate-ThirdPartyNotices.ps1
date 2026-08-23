[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$InstallPrefix,
    [Parameter(Mandatory = $true)] [string]$RepositoryRoot,
    [Parameter(Mandatory = $true)] [string]$VcpkgPrefix,
    [Parameter(Mandatory = $true)] [string]$QtPrefix,
    [Parameter(Mandatory = $true)] [string]$QtSourceRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Convert-ToRelativePath([string]$Path) {
    $relative = $Path.Replace('\', '/')
    if ($relative.StartsWith('./', [StringComparison]::Ordinal)) {
        $relative = $relative.Substring(2)
    }
    if ([IO.Path]::IsPathRooted($relative) -or $relative -match '(^|/)\.\.(/|$)') {
        throw "Invalid relative path in dependency metadata: $Path"
    }
    return $relative
}

function Get-RelativePath([string]$Root, [string]$Path) {
    return [IO.Path]::GetRelativePath($Root, $Path).Replace('\', '/')
}

function Get-SpdxSourceLocations($Packages) {
    return @($Packages | ForEach-Object {
        foreach ($propertyName in @('downloadLocation', 'homepage')) {
            $property = $_.PSObject.Properties[$propertyName]
            if ($null -ne $property) { [string]$property.Value }
        }
    } | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and $_ -notin @('NONE', 'NOASSERTION')
    } | Sort-Object -Unique)
}

function Add-Owner($Table, [string]$Path, [string]$ComponentId) {
    $key = $Path.ToLowerInvariant()
    if (-not $Table.ContainsKey($key)) {
        $Table[$key] = [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
    }
    [void]$Table[$key].Add($ComponentId)
}

$install = (Resolve-Path -LiteralPath $InstallPrefix).Path
$repository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$vcpkg = (Resolve-Path -LiteralPath $VcpkgPrefix).Path
$qt = (Resolve-Path -LiteralPath $QtPrefix).Path
$qtSource = (Resolve-Path -LiteralPath $QtSourceRoot).Path
$metadataRoot = Join-Path $install 'share/sunplayer'
$noticesPath = Join-Path $metadataRoot 'ThirdPartyNotices.txt'
$legacyInventoryPath = Join-Path $metadataRoot 'ThirdPartyInventory.json'

New-Item -ItemType Directory -Path $metadataRoot -Force | Out-Null
if (Test-Path -LiteralPath $noticesPath) {
    Remove-Item -LiteralPath $noticesPath -Force
}
if (Test-Path -LiteralPath $legacyInventoryPath) {
    Remove-Item -LiteralPath $legacyInventoryPath -Force
}

$ownersByPath = @{}
$componentsById = @{}

$vcpkgSpdxFiles = @(Get-ChildItem -LiteralPath (Join-Path $vcpkg 'share') `
    -Filter 'vcpkg.spdx.json' -File -Recurse | Sort-Object FullName)
if ($vcpkgSpdxFiles.Count -eq 0) {
    throw "No target-triplet vcpkg SPDX files found below '$vcpkg'."
}
foreach ($spdxFile in $vcpkgSpdxFiles) {
    $document = Get-Content -LiteralPath $spdxFile.FullName -Raw | ConvertFrom-Json
    $portPackages = @($document.packages | Where-Object SPDXID -CEQ 'SPDXRef-port')
    if ($portPackages.Count -ne 1) {
        throw "Expected one SPDXRef-port package in '$($spdxFile.FullName)'."
    }
    $portPackage = $portPackages[0]
    $port = [string]$portPackage.name
    $id = "vcpkg:$port"
    $notice = Join-Path $spdxFile.Directory.FullName 'copyright'
    if (-not (Test-Path -LiteralPath $notice -PathType Leaf)) {
        throw "Missing installed vcpkg copyright for '$port'."
    }
    if ($componentsById.ContainsKey($id)) {
        throw "Duplicate vcpkg component '$id'."
    }
    $componentsById[$id] = [pscustomobject]@{
        Id = $id
        Name = $port
        Version = [string]$portPackage.versionInfo
        SourceLocations = @(Get-SpdxSourceLocations $document.packages)
        NoticePaths = @($notice)
    }

    foreach ($file in @($document.files)) {
        $relative = Convert-ToRelativePath ([string]$file.fileName)
        if ($relative -notmatch '^(?:debug/)?bin/.+\.(?:dll|exe)$') {
            continue
        }
        if ($relative.StartsWith('debug/', [StringComparison]::OrdinalIgnoreCase)) {
            $relative = $relative.Substring(6)
        }
        Add-Owner $ownersByPath $relative $id
    }
}

$qtComponents = @{}
$qtSpdxFiles = @(Get-ChildItem -LiteralPath (Join-Path $qt 'sbom') `
    -Filter '*.spdx.json' -File | Sort-Object Name)
if ($qtSpdxFiles.Count -eq 0) {
    throw "No Qt module SPDX files found below '$qt'."
}
foreach ($spdxFile in $qtSpdxFiles) {
    $document = Get-Content -LiteralPath $spdxFile.FullName -Raw | ConvertFrom-Json
    $packages = @($document.packages)
    if ($packages.Count -eq 0) {
        throw "Qt SPDX has no package record: $($spdxFile.FullName)"
    }
    $primaryPackage = $packages[0]
    $module = [string]$primaryPackage.name
    $documentName = [string]$document.name
    $versionPrefix = "$module-"
    if (-not $documentName.StartsWith($versionPrefix, [StringComparison]::Ordinal)) {
        throw "Cannot derive the Qt module version from '$documentName'."
    }
    $id = "qt:$module"
    if ($qtComponents.ContainsKey($id)) {
        throw "Duplicate Qt component '$id'."
    }
    $qtComponents[$id] = [pscustomobject]@{
        Id = $id
        Name = $module
        Version = $documentName.Substring($versionPrefix.Length)
        SourceLocations = @(Get-SpdxSourceLocations @($primaryPackage))
        NoticePaths = @()
        Module = $module
    }
    foreach ($file in @($document.files)) {
        $relative = Convert-ToRelativePath ([string]$file.fileName)
        if ($relative -match '\.(?:dll|exe)$') {
            Add-Owner $ownersByPath $relative $id
        }
    }
}

$runtimeCount = 0
$runtimeFiles = @(Get-ChildItem -LiteralPath $install -Recurse -File |
    Where-Object { $_.Extension -in @('.dll', '.exe') } |
    Sort-Object FullName)
foreach ($runtime in $runtimeFiles) {
    $relative = Get-RelativePath -Root $install -Path $runtime.FullName
    if ($relative -ieq 'bin/sunplayer.exe') {
        continue
    }
    $runtimeCount++
    $key = $relative.ToLowerInvariant()
    if (-not $ownersByPath.ContainsKey($key)) {
        throw "Unknown packaged runtime: $relative"
    }
    $owners = @($ownersByPath[$key])
    if ($owners.Count -ne 1) {
        throw "Ambiguous packaged runtime ownership for '$relative': $($owners -join ', ')"
    }
    $ownerId = [string]$owners[0]
    if ($ownerId.StartsWith('qt:', [StringComparison]::OrdinalIgnoreCase) -and
        -not $componentsById.ContainsKey($ownerId)) {
        $component = $qtComponents[$ownerId]
        $licenseRoot = Join-Path $qtSource "$($component.Module)/LICENSES"
        $notices = @(Get-ChildItem -LiteralPath $licenseRoot -File -ErrorAction SilentlyContinue |
            Sort-Object Name | ForEach-Object FullName)
        if ($notices.Count -eq 0) {
            throw "Missing Qt source notices for '$($component.Module)': $licenseRoot"
        }
        $component.NoticePaths = $notices
        $componentsById[$ownerId] = $component
    }
}

$lucideReadme = Join-Path $repository 'src/app/icons/lucide/README.md'
$lucideNotice = Join-Path $repository 'src/app/icons/lucide/LICENSE'
$lucideText = Get-Content -LiteralPath $lucideReadme -Raw
$lucideVersion = [regex]::Match($lucideText, 'Lucide\s+(?<version>\d+(?:\.\d+)+)')
$lucideSource = [regex]::Match($lucideText, '(?m)^Source:\s*(?<source>\S+)\s*$')
if (-not $lucideVersion.Success -or -not $lucideSource.Success -or
    -not (Test-Path -LiteralPath $lucideNotice -PathType Leaf)) {
    throw 'Lucide README/LICENSE does not contain the required colocated metadata.'
}
$componentsById['embedded:lucide'] = [pscustomobject]@{
    Id = 'embedded:lucide'
    Name = 'Lucide Icons'
    Version = $lucideVersion.Groups['version'].Value
    SourceLocations = @($lucideSource.Groups['source'].Value)
    NoticePaths = @($lucideNotice)
}

$sections = @(
    'SunPlayer third-party notices',
    ''
)
foreach ($component in @($componentsById.Values | Sort-Object Id)) {
    $sections += @(
        '================================================================================',
        "$($component.Name) $($component.Version)"
    )
    foreach ($source in @($component.SourceLocations)) {
        $sections += "Source: $source"
    }
    foreach ($notice in @($component.NoticePaths)) {
        $sections += ''
        $sections += (Get-Content -LiteralPath $notice -Raw).TrimEnd()
    }
    $sections += ''
}

[IO.File]::WriteAllText(
    $noticesPath,
    ($sections -join "`n") + "`n",
    [Text.UTF8Encoding]::new($false))
Write-Host "Generated notices for $($componentsById.Count) components from $runtimeCount packaged runtimes."
