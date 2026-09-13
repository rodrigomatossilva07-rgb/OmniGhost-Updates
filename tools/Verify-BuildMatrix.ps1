[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir,
    [string]$Platform = 'x64',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$project = Join-Path $ProjectDir 'OmiGhost.vcxproj'
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) { throw "Project not found: $project" }

$msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if (-not $msbuild) { $msbuild = Get-Command msbuild -ErrorAction SilentlyContinue }
if (-not $msbuild) {
    $candidates = @(
        "$env:ProgramFiles\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\17\Community\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
    $candidates = @($candidates)
    if ($candidates.Count -gt 0) { $msbuild = Get-Item -LiteralPath $candidates[0] }
}
if (-not $msbuild) { throw 'MSBuild was not found. Open a Developer PowerShell or run the Visual Studio MSBuild setup first.' }
$msbuildPath = if ($msbuild.PSObject.Properties.Name -contains 'Path') { $msbuild.Path } else { $msbuild.FullName }
if (-not $msbuildPath) { throw 'MSBuild path could not be resolved.' }

$profiles = @(
    @{ Name = 'Tester';  Exe = 'build\Tester\OmniGhost.exe'; External = $true },
    @{ Name = 'Release'; Exe = 'build\OmniGhost.exe';        External = $false },
    @{ Name = 'Publish'; Exe = 'build\Publish\OmniGhost.exe'; External = $false }
)

foreach ($profile in $profiles) {
    $name = $profile.Name
    Write-Host "[BuildMatrix] === $name|$Platform ==="
    if (-not $SkipBuild) {
        & $msbuildPath $project /m "/p:Configuration=$name" "/p:Platform=$Platform" `
            /p:OmniGhostSkipPackaging=true /p:OmniGhostSkipRemotePublish=true
        if ($LASTEXITCODE -ne 0) { throw "[BuildMatrix] $name build failed with exit code $LASTEXITCODE." }
    }

    $exe = Join-Path $ProjectDir $profile.Exe
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "[BuildMatrix] $name executable missing: $exe" }
    & (Join-Path $ProjectDir 'tools\Validate-DistributionBuild.ps1') -ProjectDir $ProjectDir -Configuration $name
    if ($LASTEXITCODE -ne 0) { throw "[BuildMatrix] $name distribution validation failed." }

    if ($profile.External) {
        $offsets = Join-Path $ProjectDir 'data\cs2_offsets.json'
        if (-not (Test-Path -LiteralPath $offsets -PathType Leaf)) {
            throw '[BuildMatrix] Tester requires the canonical external data\cs2_offsets.json.'
        }
        Write-Host '[BuildMatrix] Tester: canonical external offsets present.'
    } else {
        & (Join-Path $ProjectDir 'tools\Validate-EmbeddedOffsets.ps1') `
            -ProjectDir $ProjectDir -Executable $exe -Configuration $name -Platform $Platform
        if ($LASTEXITCODE -ne 0) { throw "[BuildMatrix] $name embedded-offset validation failed." }
    }
}

Write-Host '[BuildMatrix] PASS: Tester external offsets, Release embedded resources and Publish final resources are valid.'
