[CmdletBinding()]
param(
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [ValidateSet('Release','Publish','PrivateStatic')][string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$generator = Join-Path $ProjectDir 'tools\Build-EmbeddedRuntime.ps1'
$validator = Join-Path $ProjectDir 'tools\Validate-EmbeddedRuntime.ps1'

if (-not (Test-Path -LiteralPath $generator -PathType Leaf)) { throw "Missing: $generator" }
if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) { throw "Missing: $validator" }

$vcToolsRedistDir = ''
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if ($installation) {
        $toolsRoot = Join-Path $installation 'VC\Tools\MSVC'
        $tool = Get-ChildItem -LiteralPath $toolsRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($tool) {
            $candidate = Join-Path $tool.FullName 'Redist\MSVC'
            if (Test-Path -LiteralPath $candidate -PathType Container) {
                $vcToolsRedistDir = $candidate
            }
        }
    }
}

& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $generator `
    -ProjectDir $ProjectDir `
    -VcToolsRedistDir $vcToolsRedistDir `
    -Configuration $Configuration `
    -Platform $Platform
if ($LASTEXITCODE -ne 0) { throw "Embedded runtime generation failed with exit code $LASTEXITCODE." }

if ($Configuration -ne 'PrivateStatic') {
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $validator `
        -ProjectDir $ProjectDir `
        -Configuration $Configuration `
        -Platform $Platform
    if ($LASTEXITCODE -ne 0) { throw "Embedded runtime validation failed with exit code $LASTEXITCODE." }
}

Write-Host ''
Write-Host "Repair/preflight PASS: $Configuration|$Platform" -ForegroundColor Green
Write-Host 'Now rebuild the project from a clean output directory.'
