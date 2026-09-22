[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$Configuration
)

$ErrorActionPreference = 'Stop'
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if ($Configuration -notin @('Release','Publish')) {
    throw "Unknown OmniGhost build configuration: $Configuration"
}

if ($Configuration -eq 'Publish') {
    $publish = Get-Content -LiteralPath (Join-Path $ProjectDir 'config\release\release-publish.json') -Raw | ConvertFrom-Json
    if ($publish.releaseChannel -cne 'stable') {
        throw 'Publish must retain the stable updater protocol channel.'
    }
    Write-Host '[OmniGhost Distribution] Publish: PRIVATE DEVELOPMENT BUILD / NOT FOR DISTRIBUTION; private GitHub update channel.'
    exit 0
}

Write-Host "[OmniGhost Distribution] ${Configuration}: local non-publishing build."
