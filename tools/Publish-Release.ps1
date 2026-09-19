[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [Parameter(Mandatory=$false)]
    [string]$ReleaseDirectory = '',
    [Parameter(Mandatory=$false)]
    [string]$Confirm = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$configPath = Join-Path $ProjectDir 'release-publish.json'
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { throw "Missing $configPath" }
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json

if ($config.enabled -ne $true) {
    throw 'Publication is disabled in release-publish.json. No remote changes were made.'
}
$phrase = if ($config.confirmationPhrase) { [string]$config.confirmationPhrase } else { 'PUBLISH OMNIGHOST' }
if ($config.requireConfirmation -eq $true -and $Confirm -cne $phrase) {
    throw "Publication is explicit. Re-run with -Confirm '$phrase'. No remote changes were made."
}

if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory = Join-Path $ProjectDir 'artifacts\release'
}
$ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)

Write-Host "[OmniGhost Publish] A repetir o envio dos assets já preparados em $ReleaseDirectory"
& (Join-Path $ProjectDir 'tools\Publish-Build.ps1') -ProjectDir $ProjectDir -BuildDir (Join-Path $ProjectDir 'build\Publish') -ReleaseDirectory $ReleaseDirectory -SkipPackaging -ExplicitConfirmed
exit $LASTEXITCODE
