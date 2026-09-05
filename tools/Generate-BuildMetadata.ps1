[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [Parameter(Mandatory=$false)]
    [string]$OutputPath = "",
    [Parameter(Mandatory=$false)]
    [string]$MetadataPath = "",
    [Parameter(Mandatory=$false)]
    [string]$Configuration = "",
    [Parameter(Mandatory=$false)]
    [string]$Architecture = "x64",
    [Parameter(Mandatory=$false)]
    [string]$ReleaseChannel = "stable"
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Hashing.ps1')

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $Configuration = ($Configuration -replace '[^A-Za-z0-9_.-]', '_')
    $Platform = ($Architecture -replace '[^A-Za-z0-9_.-]', '_')
    $OutputPath = Join-Path $ProjectDir ".cache\generated\$Configuration\$Platform\omni_build_metadata.h"
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if ([string]::IsNullOrWhiteSpace($MetadataPath)) {
    $Configuration = ($Configuration -replace '[^A-Za-z0-9_.-]', '_')
    $Platform = ($Architecture -replace '[^A-Za-z0-9_.-]', '_')
    $MetadataPath = Join-Path $ProjectDir ".cache\generated\$Configuration\$Platform\build-metadata.json"
}
$MetadataPath = [IO.Path]::GetFullPath($MetadataPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($MetadataPath)) | Out-Null

Write-Host "[BuildMetadata] start project=$ProjectDir"

# Only inspect Git when this extracted project itself contains a .git marker.
# Without this guard, `git -C <project>` can discover a .git directory in a
# parent folder (for example C:\Users\<name>) and `git diff` may scan a very
# large unrelated working tree, which makes the Visual Studio build appear frozen.
$GitMarker = Join-Path $ProjectDir '.git'
$Git = $null
if (Test-Path -LiteralPath $GitMarker) {
    try {
        $Git = Get-Command git -ErrorAction Stop
        Write-Host "[BuildMetadata] git repository detected; collecting commit metadata..."
    } catch {
        Write-Host "[BuildMetadata] git repository detected but git.exe was not found; using fallback metadata."
    }
} else {
    Write-Host "[BuildMetadata] no project-local .git marker; skipping Git inspection."
}

# Source archives preserve commit/timestamp metadata without shipping .git.
# This keeps Diagnostics meaningful and allows deterministic timestamps when a
# customer/developer builds an extracted source package.
$SourceMetadata = $null
$SourceMetadataPath = Join-Path $ProjectDir 'SOURCE_BUILD_METADATA.json'
if (Test-Path -LiteralPath $SourceMetadataPath -PathType Leaf) {
    try {
        $SourceMetadata = [IO.File]::ReadAllText($SourceMetadataPath) | ConvertFrom-Json
        Write-Host "[BuildMetadata] source package metadata detected."
    } catch {
        Write-Warning "Invalid SOURCE_BUILD_METADATA.json: $($_.Exception.Message)"
    }
}

# Compatibility fallback for older source snapshots that predate SOURCE_BUILD_METADATA.json.
# A stable fingerprint of SOURCE_PACKAGE_MANIFEST.json gives Diagnostics a concrete
# source identity instead of "unknown" without pretending it is a Git commit.
$SourcePackageManifest = $null
$SourcePackageFingerprint = ''
$SourcePackageManifestPath = Join-Path $ProjectDir 'SOURCE_PACKAGE_MANIFEST.json'
if (Test-Path -LiteralPath $SourcePackageManifestPath -PathType Leaf) {
    try {
        $SourcePackageManifest = [IO.File]::ReadAllText($SourcePackageManifestPath) | ConvertFrom-Json
        $SourcePackageFingerprint = Get-OmniGhostSha256 -LiteralPath $SourcePackageManifestPath
        Write-Host "[BuildMetadata] legacy source package manifest detected; using stable source fingerprint fallback."
    } catch {
        Write-Warning "Invalid SOURCE_PACKAGE_MANIFEST.json: $($_.Exception.Message)"
    }
}

# If a source package manifest is present, verify its listed files before claiming
# a reproducible extracted-source build. Editing a packaged source tree must turn
# reproducible=false instead of reusing stale package provenance.
$SourcePackageDirty = $false
if ($null -ne $SourcePackageManifest -and $null -ne $SourcePackageManifest.files) {
    $ProjectRootPrefix = $ProjectDir.TrimEnd('\') + '\'
    foreach ($Entry in @($SourcePackageManifest.files)) {
        $Relative = [string]$Entry.path
        if ([string]::IsNullOrWhiteSpace($Relative)) { continue }
        try {
            $Candidate = [IO.Path]::GetFullPath((Join-Path $ProjectDir ($Relative.Replace('/', '\'))))
            if (-not $Candidate.StartsWith($ProjectRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                $SourcePackageDirty = $true
                Write-Warning "Source package manifest path escapes project root: $Relative"
                break
            }
            if (-not (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
                $SourcePackageDirty = $true
                Write-Host "[BuildMetadata] source package changed: missing $Relative"
                break
            }
            if ($Entry.sha256) {
                $ActualHash = Get-OmniGhostSha256 -LiteralPath $Candidate
                if ($ActualHash -ne ([string]$Entry.sha256).ToLowerInvariant()) {
                    $SourcePackageDirty = $true
                    Write-Host "[BuildMetadata] source package changed: hash mismatch $Relative"
                    break
                }
            }
        } catch {
            $SourcePackageDirty = $true
            Write-Warning "Could not verify source package entry '$Relative': $($_.Exception.Message)"
            break
        }
    }
}

function Get-GitValue([string[]]$Arguments, [string]$Fallback) {
    if ($null -eq $Git) { return $Fallback }
    try {
        $value = (& $Git.Source -C $ProjectDir @Arguments 2>$null | Select-Object -First 1)
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($value)) {
            return $value.Trim()
        }
    } catch {}
    return $Fallback
}

$SourceProvenance = 'local-untracked'
$Commit = if ($env:GITHUB_SHA) {
    $SourceProvenance = 'github'
    [string]$env:GITHUB_SHA
} elseif ($null -ne $Git) {
    $SourceProvenance = 'git'
    Get-GitValue @('rev-parse','HEAD') 'unknown'
} elseif ($null -ne $SourceMetadata -and $SourceMetadata.commit_id -and [string]$SourceMetadata.commit_id -ne 'unknown') {
    $SourceProvenance = 'source-build-metadata'
    [string]$SourceMetadata.commit_id
} elseif (-not [string]::IsNullOrWhiteSpace($SourcePackageFingerprint)) {
    $SourceProvenance = 'source-package-fingerprint'
    'source-' + $SourcePackageFingerprint.Substring(0,16)
} else {
    'untracked-source'
}
if ($Commit.Length -gt 12) { $CommitShort = $Commit.Substring(0,12) } else { $CommitShort = $Commit }
$Dirty = $false
if ($null -ne $Git) {
    try {
        # Restrict the check to this project directory instead of allowing Git
        # to inspect unrelated paths from a parent repository.
        & $Git.Source -C $ProjectDir diff --quiet --ignore-submodules HEAD -- . 2>$null
        $Dirty = ($LASTEXITCODE -ne 0)
    } catch {}
} elseif ($null -ne $SourceMetadata -and $null -ne $SourceMetadata.dirty) {
    $Dirty = [bool]$SourceMetadata.dirty
}
if ($SourcePackageDirty) {
    $Dirty = $true
}

$SourceDateEpoch = 0L
$HasSourceDateEpoch = [long]::TryParse($env:SOURCE_DATE_EPOCH, [ref]$SourceDateEpoch) -and $SourceDateEpoch -gt 0
if (-not $HasSourceDateEpoch -and $null -ne $Git) {
    try {
        $CommitEpochText = (& $Git.Source -C $ProjectDir show -s --format=%ct HEAD 2>$null | Select-Object -First 1)
        $CommitEpoch = 0L
        if ([long]::TryParse([string]$CommitEpochText, [ref]$CommitEpoch) -and $CommitEpoch -gt 0) {
            $SourceDateEpoch = $CommitEpoch
            $HasSourceDateEpoch = $true
        }
    } catch {}
}
if (-not $HasSourceDateEpoch -and $null -ne $SourceMetadata -and $SourceMetadata.source_date_epoch) {
    $PackagedEpoch = 0L
    if ([long]::TryParse([string]$SourceMetadata.source_date_epoch, [ref]$PackagedEpoch) -and $PackagedEpoch -gt 0) {
        $SourceDateEpoch = $PackagedEpoch
        $HasSourceDateEpoch = $true
    }
}
if (-not $HasSourceDateEpoch -and $null -ne $SourcePackageManifest -and $SourcePackageManifest.generatedUtc) {
    try {
        $PackagedTime = [DateTimeOffset]::Parse([string]$SourcePackageManifest.generatedUtc).ToUniversalTime()
        $SourceDateEpoch = $PackagedTime.ToUnixTimeSeconds()
        $HasSourceDateEpoch = $SourceDateEpoch -gt 0
    } catch {}
}
$Reproducible = $HasSourceDateEpoch -and -not $Dirty
if ($HasSourceDateEpoch) {
    # Use a stable source timestamp so /Brepro builds do not change only because
    # the same source was compiled at a different wall-clock time.
    $BuildTime = [DateTimeOffset]::FromUnixTimeSeconds($SourceDateEpoch).UtcDateTime
} else {
    $BuildTime = [DateTime]::UtcNow
}
$BuildUtc = $BuildTime.ToString('yyyy-MM-ddTHH:mm:ssZ')
$BuildId = if ($env:GITHUB_RUN_ID) {
    "gh-$($env:GITHUB_RUN_ID)-$CommitShort"
} else {
    "local-$CommitShort"
}
if ($Dirty) { $BuildId += '-dirty' }

$ToolchainVersion = if ($env:VCToolsVersion) { $env:VCToolsVersion.TrimEnd('\') } else { '' }
$CompilerVersion = ''
$CompilerPath = ''
try {
    $ClCommand = Get-Command cl.exe -ErrorAction Stop
    $CompilerPath = $ClCommand.Source
} catch {}
if ([string]::IsNullOrWhiteSpace($CompilerPath)) {
    try {
        $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $VsWhere) {
            $InstallRoot = (& $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
            if ($InstallRoot) {
                $Candidate = Get-ChildItem -LiteralPath (Join-Path $InstallRoot 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
                    Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe' } |
                    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
                if ($Candidate) { $CompilerPath = $Candidate }
            }
        }
    } catch {}
}
if (-not [string]::IsNullOrWhiteSpace($CompilerPath)) {
    try {
        $Info = [Diagnostics.FileVersionInfo]::GetVersionInfo($CompilerPath)
        $CompilerVersion = if ($Info.ProductVersion) { [string]$Info.ProductVersion } else { [string]$Info.FileVersion }
        if ([string]::IsNullOrWhiteSpace($ToolchainVersion)) {
            $normalizedCompilerPath = $CompilerPath.Replace('/', '\')
            $match = [regex]::Match($normalizedCompilerPath, '\\VC\\Tools\\MSVC\\([^\\]+)\\', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if ($match.Success) { $ToolchainVersion = $match.Groups[1].Value }
        }
    } catch {}
}
if ([string]::IsNullOrWhiteSpace($ToolchainVersion)) { $ToolchainVersion = 'unknown' }
if ([string]::IsNullOrWhiteSpace($CompilerVersion)) { $CompilerVersion = 'unknown' }

$WindowsSdkVersion = if ($env:WindowsSDKVersion) { $env:WindowsSDKVersion.TrimEnd('\') } else { '' }
if ([string]::IsNullOrWhiteSpace($WindowsSdkVersion)) {
    try {
        $KitsRoot = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction Stop).KitsRoot10
        if ($KitsRoot) {
            $SdkDir = Get-ChildItem -LiteralPath (Join-Path $KitsRoot 'Include') -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } | Sort-Object Name -Descending | Select-Object -First 1
            if ($SdkDir) { $WindowsSdkVersion = $SdkDir.Name }
        }
    } catch {}
}
if ([string]::IsNullOrWhiteSpace($WindowsSdkVersion)) { $WindowsSdkVersion = 'unknown' }
if ([string]::IsNullOrWhiteSpace($Configuration)) { $Configuration = if ($env:Configuration) { $env:Configuration } else { 'unknown' } }
if ([string]::IsNullOrWhiteSpace($Architecture)) { $Architecture = 'x64' }
if ([string]::IsNullOrWhiteSpace($ReleaseChannel)) { $ReleaseChannel = 'stable' }
$AppVersion = 'unknown'
$VersionPath = Join-Path $ProjectDir 'version.txt'
if (Test-Path -LiteralPath $VersionPath -PathType Leaf) {
    $CandidateVersion = ([IO.File]::ReadAllText($VersionPath)).Trim()
    if ($CandidateVersion -match '^\d+\.\d+\.\d+$') { $AppVersion = $CandidateVersion }
}
$MemProcFSVersion = 'unknown'
$LeechCoreVersion = 'unknown'
$DependencyVersionPath = Join-Path $ProjectDir 'third_party\dma_stack\versions.json'
if (Test-Path -LiteralPath $DependencyVersionPath -PathType Leaf) {
    try {
        $DependencyVersions = [IO.File]::ReadAllText($DependencyVersionPath) | ConvertFrom-Json
        if ($DependencyVersions.memprocfs.detected_version) { $MemProcFSVersion = [string]$DependencyVersions.memprocfs.detected_version }
        if ($DependencyVersions.leechcore.detected_version) { $LeechCoreVersion = [string]$DependencyVersions.leechcore.detected_version }
    } catch {
        Write-Warning "Could not read dependency versions: $($_.Exception.Message)"
    }
}

function Escape-Cpp([string]$Value) {
    return $Value.Replace('\','\\').Replace('"','\"')
}

$Content = @"
#pragma once
namespace OmniGhost::BuildInfo {
inline constexpr char AppVersion[] = "$(Escape-Cpp $AppVersion)";
inline constexpr char BuildId[] = "$(Escape-Cpp $BuildId)";
inline constexpr char CommitId[] = "$(Escape-Cpp $Commit)";
inline constexpr char SourceProvenance[] = "$(Escape-Cpp $SourceProvenance)";
inline constexpr char BuildUtc[] = "$(Escape-Cpp $BuildUtc)";
inline constexpr char ToolchainVersion[] = "$(Escape-Cpp $ToolchainVersion)";
inline constexpr char WindowsSdkVersion[] = "$(Escape-Cpp $WindowsSdkVersion)";
inline constexpr char CompilerVersion[] = "$(Escape-Cpp $CompilerVersion)";
inline constexpr char Architecture[] = "$(Escape-Cpp $Architecture)";
inline constexpr char Configuration[] = "$(Escape-Cpp $Configuration)";
inline constexpr char ReleaseChannel[] = "$(Escape-Cpp $ReleaseChannel)";
inline constexpr char MemProcFSVersion[] = "$(Escape-Cpp $MemProcFSVersion)";
inline constexpr char LeechCoreVersion[] = "$(Escape-Cpp $LeechCoreVersion)";
inline constexpr bool Reproducible = $(if ($Reproducible) { 'true' } else { 'false' });
}
"@
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$NormalizedContent = $Content.TrimStart([char[]]"`r`n")
[IO.File]::WriteAllText($OutputPath, $NormalizedContent + [Environment]::NewLine, $Utf8NoBom)
$Metadata = [ordered]@{
    schema = 1
    app_version = $AppVersion
    build_id = $BuildId
    commit_id = $Commit
    source_provenance = $SourceProvenance
    dirty = [bool]$Dirty
    build_utc = $BuildUtc
    reproducible = [bool]$Reproducible
    architecture = $Architecture
    configuration = $Configuration
    release_channel = $ReleaseChannel
    source_date_epoch = if ($HasSourceDateEpoch) { $SourceDateEpoch } else { $null }
    toolchain = [ordered]@{
        vc_tools_version = $ToolchainVersion
        compiler_version = $CompilerVersion
        compiler_path = $CompilerPath
        windows_sdk_version = $WindowsSdkVersion
    }
    dependencies = [ordered]@{
        memprocfs = $MemProcFSVersion
        leechcore = $LeechCoreVersion
    }
}
$MetadataJson = $Metadata | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($MetadataPath, $MetadataJson + [Environment]::NewLine, $Utf8NoBom)
Write-Host "[BuildMetadata] header=$OutputPath"
Write-Host "[BuildMetadata] metadata=$MetadataPath"
Write-Host "[BuildMetadata] build_id=$BuildId commit=$CommitShort provenance=$SourceProvenance reproducible=$Reproducible config=$Configuration arch=$Architecture channel=$ReleaseChannel vc=$ToolchainVersion compiler=$CompilerVersion sdk=$WindowsSdkVersion"
