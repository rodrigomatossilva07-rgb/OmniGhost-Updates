[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$ReleaseDirectory,
    [Parameter(Mandatory=$false)][string]$Version = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)

if (-not (Test-Path -LiteralPath $ReleaseDirectory -PathType Container)) {
    throw "Release directory not found: $ReleaseDirectory"
}

Write-Host "[ValidateRelease] Validating release directory: $ReleaseDirectory"

# 1. Check required files exist
$requiredFiles = @(
    "OmniGhost.exe",
    "update.json",
    "install-manifest.sha256",
    "install-manifest.sha256.sig",
    "SBOM.cdx.json",
    "changelog.json",
    "release-notes.md",
    "release-meta.json"
)

foreach ($file in $requiredFiles) {
    $path = Join-Path $ReleaseDirectory $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file missing: $file"
    }
    Write-Host "[PASS] Required file present: $file"
}

# 2. Validate executable version matches
$exePath = Join-Path $ReleaseDirectory 'OmniGhost.exe'
if ($Version) {
    $exeVersion = (Get-Item -LiteralPath $exePath).VersionInfo.FileVersion
    if ($exeVersion -ne $Version) {
        throw "Executable version mismatch: expected $Version, got $exeVersion"
    }
    Write-Host "[PASS] Executable version matches: $exeVersion"
}

# 2. Validate update.json structure
$updateJsonPath = Join-Path $ReleaseDirectory 'update.json'
$updateJson = Get-Content -LiteralPath $updateJsonPath -Raw | ConvertFrom-Json

$requiredFields = @('schemaVersion', 'appId', 'channel', 'version', 'minimumSupportedVersion', 'mandatory', 'publishedAt', 'releaseNotesUrl', 'packages')
foreach ($field in $requiredFields) {
    if (-not $updateJson.$field) {
        throw "update.json missing required field: $field"
    }
}
Write-Host "[PASS] update.json has all required fields"

# 3. Validate package structure
foreach ($package in $updateJson.packages) {
    if (-not $package.platform -or -not $package.architecture -or -not $package.fileName -or -not $package.url -or -not $package.size -or -not $package.sha256) {
        throw "Package missing required fields: $($package | ConvertTo-Json)"
    }
    
    $packagePath = Join-Path $ReleaseDirectory $package.fileName
    if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
        throw "Package file not found: $($package.fileName)"
    }
    
    # Verify SHA256
    $actualSha = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
    if ($actualSha -cne $package.sha256) {
        throw "SHA256 mismatch for $($package.fileName): expected $($package.sha256), got $actualSha"
    }
    
    Write-Host "[PASS] Package validated: $($package.fileName)"
}

# 4. Validate install-manifest.sha256
$manifestPath = Join-Path $ReleaseDirectory 'install-manifest.sha256'
$manifestContent = Get-Content -LiteralPath $manifestPath -Raw
if (-not $manifestContent) {
    throw "install-manifest.sha256 is empty"
}
Write-Host "[PASS] install-manifest.sha256 present and non-empty"

# 5. Validate signature
$sigPath = Join-Path $ReleaseDirectory 'install-manifest.sha256.sig'
if (-not (Test-Path -LiteralPath $sigPath -PathType Leaf)) {
    throw "Signature file missing: install-manifest.sha256.sig"
}
Write-Host "[PASS] Signature file present"

# 6. Validate SBOM
$sbomPath = Join-Path $ReleaseDirectory 'SBOM.cdx.json'
$sbom = Get-Content -LiteralPath $sbomPath -Raw | ConvertFrom-Json
if (-not $sbom.bomFormat -or -not $sbom.specVersion -or -not $sbom.components) {
    throw "SBOM missing required fields"
}
Write-Host "[PASS] SBOM valid: $($sbom.components.Count) components"

# 7. Validate changelog
$changelogPath = Join-Path $ReleaseDirectory 'changelog.json'
$changelog = Get-Content -LiteralPath $changelogPath -Raw | ConvertFrom-Json
if (-not $changelog.version -or -not $changelog.entries) {
    throw "Changelog missing required fields"
}
Write-Host "[PASS] Changelog valid with $($changelog.entries.Count) entries"

# 8. Validate release-meta.json
$metaPath = Join-Path $ReleaseDirectory 'release-meta.json'
$meta = Get-Content -LiteralPath $metaPath -Raw | ConvertFrom-Json
if (-not $meta.version -or -not $meta.buildTimestamp -or -not $meta.configuration -or -not $meta.architecture) {
    throw "release-meta.json missing required fields"
}
Write-Host "[PASS] release-meta.json valid"

Write-Host ""
Write-Host "[PASS] All release artifacts validated successfully!"

exit 0