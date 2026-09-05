[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [Parameter(Mandatory=$false)]
    [string]$BuildDir = '',
    [string]$SemanticAnalysisPath = '',
    [ValidateSet('Development','Publish','Commercial')]
    [string]$Mode = 'Development'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $ProjectDir 'build' }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$LogFile = Join-Path $ProjectDir 'artifacts\package-release.log'

Write-Host "[OmniGhost Package] mode=$Mode"
Write-Host '[OmniGhost Package] Packaging never increments version.txt and never publishes remotely.'
$commercialSwitch = $Mode -eq 'Commercial'
$publishSwitch = $Mode -in @('Publish','Commercial')
$arguments = @{
    ProjectDir = $ProjectDir
    BuildDir = $BuildDir
    LogFile = $LogFile
    PublishBuild = $publishSwitch
    Commercial = $commercialSwitch
}
if (-not [string]::IsNullOrWhiteSpace($SemanticAnalysisPath)) {
    $arguments.SemanticAnalysisPath = $SemanticAnalysisPath
}
try {
    & (Join-Path $ProjectDir 'tools\package_release.ps1') @arguments
}
finally {
    if ($Mode -in @('Publish','Commercial')) {
        & (Join-Path $ProjectDir 'tools\Cleanup-ReleaseCopies.ps1')
        if ($LASTEXITCODE -ne 0) {
            throw 'O pacote canónico foi criado, mas a limpeza das cópias de Transferências falhou.'
        }
    }
}
# package_release.ps1 reports failures through terminating exceptions. External
# tools used for optional metadata discovery may leave a stale LASTEXITCODE.
exit 0
