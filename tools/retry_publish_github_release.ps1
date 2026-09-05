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

Write-Host '[OmniGhost Publish Retry] Retry is an explicit Publish action.'
Write-Host '[OmniGhost Publish Retry] It never deletes local release artefacts.'

$arguments = @{
    ProjectDir = $ProjectDir
    Confirm = $Confirm
}
if (-not [string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $arguments.ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
}

& (Join-Path $ProjectDir 'tools\Publish-Release.ps1') @arguments
exit $LASTEXITCODE
