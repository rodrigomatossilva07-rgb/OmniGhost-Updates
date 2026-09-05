[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)

try {
    & (Join-Path $ProjectDir 'tools\Validate-Project.ps1') -ProjectDir $ProjectDir
}
catch {
    Write-Error $_
    exit 1
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe not found. Install Visual Studio Build Tools.' }
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2>$null | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found.' }

Write-Host '[OmniGhost Build] Release|x64: compilação local. Esta ação nunca publica no GitHub.'
& $msbuild (Join-Path $ProjectDir 'OmiGhost.vcxproj') /m /p:Configuration=Release /p:Platform=x64 /p:OmniGhostReleaseChannel=stable
exit $LASTEXITCODE
