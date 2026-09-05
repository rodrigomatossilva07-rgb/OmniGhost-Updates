[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
)
$ErrorActionPreference = "Stop"
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)

Write-Host "=== OmniGhost Engineering Doctor ===" -ForegroundColor Cyan
& (Join-Path $ProjectDir "tools\Validate-Project.ps1") -ProjectDir $ProjectDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $ProjectDir "tools\Update-DmaDependencies.ps1") -Validate
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Doctor: PASS" -ForegroundColor Green
