<#
.SYNOPSIS
    Verifies that a build is reproducible by building twice and comparing outputs.

.DESCRIPTION
    Performs a clean build, records the binary hash, rebuilds from scratch in a
    separate directory, and compares the resulting binaries byte-for-byte.

.PARAMETER ProjectDir
    Path to project root (default: current directory)

.PARAMETER PrimaryExecutable
    Path to the primary executable to verify (relative to ProjectDir)

.PARAMETER MSBuildPath
    Path to MSBuild.exe (auto-detected if not provided)

.PARAMETER Config
    Build configuration to test (default: Publish)

.PARAMETER CleanBetweenBuilds
    Whether to do a full clean between builds (default: true)

.PARAMETER ComparePDBs
    Whether to compare PDB files as well (default: false)

.EXAMPLE
    .\Verify-ReproducibleBuild.ps1 -ProjectDir "." -PrimaryExecutable "build/Publish/OmniGhost.exe"

.EXAMPLE
    .\Verify-ReproducibleBuild.ps1 -Config Release -ComparePDBs
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = ".",

    [Parameter(Mandatory=$true)]
    [string]$PrimaryExecutable,

    [Parameter(Mandatory=$false)]
    [string]$MSBuildPath,

    [Parameter(Mandatory=$false)]
    [string]$Config = "Publish",

    [Parameter(Mandatory=$false)]
    [switch]$CleanBetweenBuilds = $true,

    [Parameter(Mandatory=$false)]
    [switch]$ComparePDBs = $false
)

$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
function Write-Log {
    param(
        [string]$Message,
        [ValidateSet('Info','Warn','Error','Pass','Fail','Debug')]
        [string]$Level = 'Info'
    )
    $prefix = switch ($Level) {
        'Info'  { '[INFO]' }
        'Warn'  { '[WARN]' }
        'Error' { '[ERROR]' }
        'Pass'  { '[PASS]' }
        'Fail'  { '[FAIL]' }
        'Debug' { '[DEBUG]' }
    }
    Write-Host "${prefix} ${Message}"
}

function Get-MSBuildPath {
    if ($MSBuildPath -and (Test-Path $MSBuildPath)) {
        return $MSBuildPath
    }
    # Try VS 2022
    $vsWhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $msbuild) { return $msbuild }
        }
    }
    # Fallback
    $candidates = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    throw "MSBuild not found. Set -MSBuildPath parameter."
}

function Get-FileHashSafe {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $null }
    return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLower()
}

function Compare-Files {
    param([string]$File1, [string]$File2, [string]$Label)
    $h1 = Get-FileHashSafe -Path $File1
    $h2 = Get-FileHashSafe -Path $File2

    if ($null -eq $h1 -or $null -eq $h2) {
        Write-Log "${Label}: MISSING FILE" -Level Fail
        return $false
    }

    if ($h1 -eq $h2) {
        Write-Log "${Label}: MATCH (${h1})" -Level Pass
        return $true
    } else {
        Write-Log "${Label}: MISMATCH" -Level Fail
        Write-Log "  Build 1: ${h1}" -Level Error
        Write-Log "  Build 2: ${h2}" -Level Error
        return $false
    }
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
$projectRoot = Resolve-Path $ProjectDir
$exePath = Join-Path $projectRoot $PrimaryExecutable
$msbuild = Get-MSBuildPath

Write-Log "Reproducible Build Verification"
Write-Log "Project: ${projectRoot}"
Write-Log "Executable: ${PrimaryExecutable}"
Write-Log "Config: ${Config}"
Write-Log "MSBuild: ${msbuild}"

if (-not (Test-Path $exePath)) {
    Write-Log "Primary executable not found: ${exePath}" -Level Error
    exit 1
}

$buildDir1 = Join-Path $projectRoot "build_repro_1"
$buildDir2 = Join-Path $projectRoot "build_repro_2"

# Build 1
Write-Log "`n=== BUILD 1 ===" -Level Info
if ($CleanBetweenBuilds -and (Test-Path $buildDir1)) {
    Remove-Item -Recurse -Force $buildDir1 -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $buildDir1 | Out-Null

$args1 = @(
    "-S", $projectRoot,
    "-B", $buildDir1,
    "--preset", "windows-${Config.ToLower()}"
)
& $msbuild @args1
if ($LASTEXITCODE -ne 0) {
    Write-Log "Build 1 FAILED" -Level Fail
    exit 1
}

$exe1 = Join-Path $buildDir1 "OmniGhost.exe"
$pdb1 = Join-Path $buildDir1 "OmniGhost.pdb"

# Build 2
Write-Log "`n=== BUILD 2 ===" -Level Info
if ($CleanBetweenBuilds -and (Test-Path $buildDir2)) {
    Remove-Item -Recurse -Force $buildDir2 -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $buildDir2 | Out-Null

$args2 = @(
    "-S", $projectRoot,
    "-B", $buildDir2,
    "--preset", "windows-${Config.ToLower()}"
)
& $msbuild @args2
if ($LASTEXITCODE -ne 0) {
    Write-Log "Build 2 FAILED" -Level Fail
    exit 1
}

$exe2 = Join-Path $buildDir2 "OmniGhost.exe"
$pdb2 = Join-Path $buildDir2 "OmniGhost.pdb"

# Compare
Write-Log "`n=== COMPARISON ===" -Level Info

$allMatch = $true

$allMatch = Compare-Files -File1 $exe1 -File2 $exe2 -Label "OmniGhost.exe" -and $allMatch

if ($ComparePDBs) {
    $allMatch = Compare-Files -File1 $pdb1 -File2 $pdb2 -Label "OmniGhost.pdb" -and $allMatch
}

# Also compare any other DLLs in output
$dlls1 = Get-ChildItem -Path $buildDir1 -Filter "*.dll" -File
$dlls2 = Get-ChildItem -Path $buildDir2 -Filter "*.dll" -File

foreach ($dll1 in $dlls1) {
    $dll2 = $dlls2 | Where-Object { $_.Name -eq $dll1.Name }
    if ($dll2) {
        $allMatch = Compare-Files -File1 $dll1.FullName -File2 $dll2.FullName -Label "DLL: $($dll1.Name)" -and $allMatch
    }
}

# Summary
Write-Log "`n============================================================"
Write-Log "REPRODUCIBLE BUILD VERIFICATION RESULT"
Write-Log "============================================================"

if ($allMatch) {
    Write-Log "REPRODUCIBLE: YES" -Level Pass
    Write-Log "All artifacts match byte-for-byte" -Level Pass
    exit 0
} else {
    Write-Log "REPRODUCIBLE: NO" -Level Fail
    Write-Log "One or more artifacts differ between builds" -Level Fail
    exit 1
}