[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$Configuration
)

$ErrorActionPreference = 'Stop'
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if ($Configuration -notin @('Release','Tester','Publish')) {
    throw "Unknown OmniGhost build configuration: $Configuration"
}

if ($Configuration -eq 'Tester') {
    $policy = Get-Content -LiteralPath (Join-Path $ProjectDir 'OmniGhost.Tester.props') -Raw
    if ($policy -notmatch 'OMNIGHOST_TESTER_BUILD=1' -or
        $policy -notmatch 'OMNIGHOST_REQUIRE_LOCAL_LICENSE=1') {
        throw 'Tester must be visibly identified and protected by the local-license gate.'
    }
    Write-Host '[OmniGhost Distribution] Tester: internal development channel + local licence.'
    exit 0
}

if ($Configuration -eq 'Publish') {
    $publish = Get-Content -LiteralPath (Join-Path $ProjectDir 'release-publish.json') -Raw | ConvertFrom-Json
    if ($publish.releaseChannel -cne 'stable') {
        throw 'Publish must retain the stable updater protocol channel.'
    }
    Write-Host '[OmniGhost Distribution] Publish: PRIVATE DEVELOPMENT BUILD / NOT FOR DISTRIBUTION; private GitHub update channel.'
    exit 0
}

Write-Host "[OmniGhost Distribution] ${Configuration}: local non-publishing build."
