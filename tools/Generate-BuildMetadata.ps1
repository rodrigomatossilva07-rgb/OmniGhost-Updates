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
#
# Full SHA-256 of every packaged file (including third_party binaries) can take
# many minutes and looks like a freeze in Visual Studio. Strategy:
#   - Always check path safety + existence (fast)
#   - Hash only for Publish configuration (commercial reproducibility gate)
#   - Skip hashing under heavy/generated trees even on Publish
#   - Emit progress so the build log does not appear stuck
$SourcePackageDirty = $false
if ($null -ne $SourcePackageManifest -and $null -ne $SourcePackageManifest.files) {
    $ProjectRootPrefix = $ProjectDir.TrimEnd('\') + '\'
    $Entries = @($SourcePackageManifest.files)
    $TotalEntries = $Entries.Count
    $VerifyHashes = ($Configuration -eq 'Publish')
    Write-Host "[BuildMetadata] verifying source package manifest entries=$TotalEntries hashVerify=$VerifyHashes"

    $Index = 0
    foreach ($Entry in $Entries) {
        $Index++
        $Relative = [string]$Entry.path
        if ([string]::IsNullOrWhiteSpace($Relative)) { continue }

        if (($Index % 50) -eq 0 -or $Index -eq $TotalEntries) {
            Write-Host "[BuildMetadata] source package verify progress $Index/$TotalEntries"
        }

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

            if (-not $VerifyHashes -or -not $Entry.sha256) { continue }

            $Norm = $Relative.Replace('\', '/').ToLowerInvariant()
            $SkipHash = $Norm.StartsWith('third_party/') -or
                        $Norm.StartsWith('.cache/') -or
                        $Norm.StartsWith('build/') -or
                        $Norm.StartsWith('runtime/') -or
                        $Norm.EndsWith('.dll') -or
                        $Norm.EndsWith('.lib') -or
                        $Norm.EndsWith('.pdb') -or
                        $Norm.EndsWith('.exe') -or
                        $Norm.EndsWith('.obj')
            if ($SkipHash) { continue }

            $ActualHash = Get-OmniGhostSha256 -LiteralPath $Candidate
            if ($ActualHash -ne ([string]$Entry.sha256).ToLowerInvariant()) {
                $SourcePackageDirty = $true
                Write-Host "[BuildMetadata] source package changed: hash mismatch $Relative"
                break
            }
        } catch {
            $SourcePackageDirty = $true
            Write-Warning "Could not verify source package entry '$Relative': $($_.Exception.Message)"
            break
        }
    }

    Write-Host "[BuildMetadata] source package verification complete dirty=$SourcePackageDirty"
}

function Get-GitValue([string[]]$Arguments, [string]$Fallback) {
    if ($null -eq $Git) { return $Fallback }
    try {
        $result = Invoke-GitWithTimeout -Arguments $Arguments -TimeoutSeconds 8
        if (-not $result.TimedOut -and $result.ExitCode -eq 0 -and $result.Output.Count -gt 0) {
            $value = [string]$result.Output[0]
            if (-not [string]::IsNullOrWhiteSpace($value)) { return $value.Trim() }
        }
    } catch {}
    return $Fallback
}

# Git status/diff can hang for a long time on large trees, broken worktrees, or
# when AV scans every touched file. Bound all git invocations used at build time.
function Invoke-GitWithTimeout {
    param(
        [string[]]$Arguments,
        [int]$TimeoutSeconds = 12
    )
    if ($null -eq $Git) { return @{ TimedOut = $false; ExitCode = -1; Output = @() } }

    $stdout = Join-Path $env:TEMP ("omni-git-out-{0}.txt" -f [Guid]::NewGuid().ToString('N'))
    $stderr = Join-Path $env:TEMP ("omni-git-err-{0}.txt" -f [Guid]::NewGuid().ToString('N'))
    try {
        $argList = @('-C', $ProjectDir) + $Arguments
        $proc = Start-Process -FilePath $Git.Source -ArgumentList $argList `
            -NoNewWindow -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $finished = $proc.WaitForExit($TimeoutSeconds * 1000)
        if (-not $finished) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            try { $proc.WaitForExit(2000) } catch {}
            return @{ TimedOut = $true; ExitCode = -1; Output = @() }
        }
        $output = @()
        if (Test-Path -LiteralPath $stdout) {
            $output = @(Get-Content -LiteralPath $stdout -ErrorAction SilentlyContinue)
        }
        return @{ TimedOut = $false; ExitCode = $proc.ExitCode; Output = $output }
    } catch {
        return @{ TimedOut = $false; ExitCode = -1; Output = @() }
    } finally {
        Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "[BuildMetadata] resolving source provenance..."
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
    # Prefer a cheap porcelain status (tracked files only). Full `git diff` on large
    # working trees is a common Visual Studio "freeze" during metadata generation.
    Write-Host "[BuildMetadata] checking git dirty state (timeout 12s)..."
    $status = Invoke-GitWithTimeout -Arguments @('status','--porcelain','-uno','--ignore-submodules','--','.') -TimeoutSeconds 12
    if ($status.TimedOut) {
        Write-Warning "[BuildMetadata] git status timed out; treating tree as dirty for safety."
        $Dirty = $true
    } else {
        $Dirty = @($status.Output | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count -gt 0
    }
} elseif ($null -ne $SourceMetadata -and $null -ne $SourceMetadata.dirty) {
    $Dirty = [bool]$SourceMetadata.dirty
}
if ($SourcePackageDirty) {
    $Dirty = $true
}
Write-Host "[BuildMetadata] provenance=$SourceProvenance commit=$CommitShort dirty=$Dirty"

$SourceDateEpoch = 0L
$HasSourceDateEpoch = [long]::TryParse($env:SOURCE_DATE_EPOCH, [ref]$SourceDateEpoch) -and $SourceDateEpoch -gt 0
if (-not $HasSourceDateEpoch -and $null -ne $Git) {
    $epochResult = Invoke-GitWithTimeout -Arguments @('show','-s','--format=%ct','HEAD') -TimeoutSeconds 8
    if (-not $epochResult.TimedOut -and $epochResult.Output.Count -gt 0) {
        $CommitEpoch = 0L
        if ([long]::TryParse([string]$epochResult.Output[0], [ref]$CommitEpoch) -and $CommitEpoch -gt 0) {
            $SourceDateEpoch = $CommitEpoch
            $HasSourceDateEpoch = $true
        }
    }
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

Write-Host "[BuildMetadata] resolving toolchain..."
$ToolchainVersion = if ($env:VCToolsVersion) { $env:VCToolsVersion.TrimEnd('\') } else { '' }
$CompilerVersion = ''
$CompilerPath = ''
try {
    $ClCommand = Get-Command cl.exe -ErrorAction Stop
    $CompilerPath = $ClCommand.Source
} catch {}
# Prefer environment / PATH. Avoid vswhere on normal incremental builds — it can
# stall for a long time on some developer machines with many VS installs.
if ([string]::IsNullOrWhiteSpace($CompilerPath) -and $Configuration -eq 'Publish') {
    try {
        Write-Host "[BuildMetadata] locating cl.exe via vswhere (Publish only)..."
        $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $VsWhere) {
            $InstallRoot = (& $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
            if ($InstallRoot) {
                $MsvcRoot = Join-Path $InstallRoot 'VC\Tools\MSVC'
                if (Test-Path -LiteralPath $MsvcRoot) {
                    $Candidate = Get-ChildItem -LiteralPath $MsvcRoot -Directory -ErrorAction SilentlyContinue |
                        Sort-Object Name -Descending |
                        ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe' } |
                        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                        Select-Object -First 1
                    if ($Candidate) { $CompilerPath = $Candidate }
                }
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

Write-Host "[BuildMetadata] resolving Windows SDK..."
$WindowsSdkVersion = if ($env:WindowsSDKVersion) { $env:WindowsSDKVersion.TrimEnd('\') } else { '' }
if ([string]::IsNullOrWhiteSpace($WindowsSdkVersion)) {
    try {
        $KitsRoot = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction Stop).KitsRoot10
        if ($KitsRoot) {
            $IncludeRoot = Join-Path $KitsRoot 'Include'
            if (Test-Path -LiteralPath $IncludeRoot) {
                $SdkDir = Get-ChildItem -LiteralPath $IncludeRoot -Directory -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
                    Sort-Object Name -Descending |
                    Select-Object -First 1
                if ($SdkDir) { $WindowsSdkVersion = $SdkDir.Name }
            }
        }
    } catch {}
}
if ([string]::IsNullOrWhiteSpace($WindowsSdkVersion)) { $WindowsSdkVersion = 'unknown' }
Write-Host "[BuildMetadata] toolchain=$ToolchainVersion compiler=$CompilerVersion sdk=$WindowsSdkVersion"
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
$DependencyVersionCandidates = @(
    (Join-Path $ProjectDir 'third_party\dma_stack\versions.json'),
    (Join-Path $ProjectDir 'versions.json')
)
$DependencyVersionPath = $DependencyVersionCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ($DependencyVersionPath) {
    try {
        $DependencyVersions = [IO.File]::ReadAllText($DependencyVersionPath) | ConvertFrom-Json
        if ($DependencyVersions.memprocfs.detected_version) { $MemProcFSVersion = [string]$DependencyVersions.memprocfs.detected_version }
        if ($DependencyVersions.leechcore.detected_version) { $LeechCoreVersion = [string]$DependencyVersions.leechcore.detected_version }
        Write-Host "[BuildMetadata] dependency manifest=$DependencyVersionPath memprocfs=$MemProcFSVersion leechcore=$LeechCoreVersion"
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
