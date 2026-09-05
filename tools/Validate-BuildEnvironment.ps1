[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
)
$ErrorActionPreference = "Stop"
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)

if (-not [Environment]::Is64BitOperatingSystem) { throw "OmniGhost requires 64-bit Windows." }
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw "Visual Studio Installer/vswhere not found." }

$installation = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -format json | ConvertFrom-Json
if (-not $installation) { throw "Visual Studio with MSBuild not found." }
Write-Host "[PASS] Visual Studio: $($installation.displayName) $($installation.catalog.productDisplayVersion)"
Write-Host "[PASS] Project: $ProjectDir"

& (Join-Path $ProjectDir "tools\Validate-Project.ps1") -ProjectDir $ProjectDir
