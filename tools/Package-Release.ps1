[CmdletBinding()]
param(
    [string]$ProjectDir = '',
    [string]$BuildDir = '',
    [ValidateSet('Development','Publish','Commercial')][string]$Mode = 'Development',
    [string]$SemanticAnalysisPath = '',
    [switch]$AllowMissingTag
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = Split-Path -Parent $PSScriptRoot }
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $configuration = if ($Mode -eq 'Development') { 'Release' } else { 'Publish' }
    $BuildDir = Join-Path $ProjectDir "build\$configuration"
}
$parameters = @{ ProjectDir = $ProjectDir; BuildDir = $BuildDir }
if ($Mode -in @('Commercial', 'Publish')) { $parameters.Commercial = $true }
if ($AllowMissingTag) { $parameters.AllowMissingTag = $true }
if ($Mode -eq 'Publish') { $parameters.PublishBuild = $true }
if (-not [string]::IsNullOrWhiteSpace($SemanticAnalysisPath)) {
    $parameters.SemanticAnalysisPath = $SemanticAnalysisPath
}
& (Join-Path $PSScriptRoot 'package_release.ps1') @parameters
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
