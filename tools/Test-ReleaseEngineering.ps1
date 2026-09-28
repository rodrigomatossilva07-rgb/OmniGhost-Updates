[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "[Release engineering test] $Message" }
}

function Is-Version([string]$Value) {
    return $Value -match '^(0|[1-9]\d*)\.[0-9]\.[0-9]$'
}

function Next-Version([string]$Value) {
    if (-not (Is-Version $Value)) { throw "Invalid test version: $Value" }
    $parts = $Value.Split('.')
    $major = [int]$parts[0]
    $minor = [int]$parts[1]
    $patch = [int]$parts[2] + 1
    if ($patch -eq 10) { $patch = 0; $minor++ }
    if ($minor -eq 10) { $minor = 0; $major++ }
    return "$major.$minor.$patch"
}

Assert (Is-Version '2.9.9') '2.9.9 should be valid.'
Assert (-not (Is-Version '2.10.0')) '2.10.0 must be rejected by the OmniGhost version policy.'
Assert (-not (Is-Version '2.11.2')) '2.11.2 must be rejected by the OmniGhost version policy.'
Assert ((Next-Version '2.4.9') -ceq '2.5.0') 'PATCH rollover is incorrect.'
Assert ((Next-Version '2.9.9') -ceq '3.0.0') 'MAJOR rollover is incorrect.'

$version = [IO.File]::ReadAllText((Join-Path $ProjectDir 'config\release\version.txt')).Trim()
$header = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\app_version.h'))
$resource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'resources\app.rc'))
Assert (Is-Version $version) "version.txt is invalid: $version"
Assert ($header -match ('Version\[\]\s*=\s*"' + [regex]::Escape($version) + '"')) 'app_version.h is not synchronized.'
$p = $version.Split('.')
Assert ($resource -match "OMNIGHOST_VERSION_MAJOR\s+$($p[0])") 'app.rc major is not synchronized.'
Assert ($resource -match "OMNIGHOST_VERSION_MINOR\s+$($p[1])") 'app.rc minor is not synchronized.'
Assert ($resource -match "OMNIGHOST_VERSION_PATCH\s+$($p[2])") 'app.rc patch is not synchronized.'

$policy = Get-Content -LiteralPath (Join-Path $ProjectDir 'config\release\release-publish.json') -Raw | ConvertFrom-Json
Assert ($policy.publishAfterBuild -eq $true) 'Publish must upload automatically after a successful build.'
Assert ($policy.requireExplicitPublishCommand -eq $false) 'Selecting Publish is the explicit command.'
Assert ($policy.requireConfirmation -eq $false) 'Publish must remain non-interactive inside MSBuild.'
Assert ($policy.requireCleanReproducibleBuild -eq $true) 'Clean reproducible source must be required.'
Assert ($policy.requireTagRelease -eq $true) 'A matching release tag must be required.'
Assert ($policy.allowDeterministicSourceSnapshot -eq $true) 'Deterministic source snapshot publication must be explicitly enabled for this extracted project.'
Assert ($policy.requireAuthenticode -eq $false -and $policy.requireManifestSignature -eq $false) 'ZIP-only releases must not require certificate signatures.'

$project = [IO.File]::ReadAllText((Join-Path $ProjectDir 'OmiGhost.vcxproj'))
$publishBuild = [IO.File]::ReadAllText((Join-Path $ProjectDir 'tools\Publish-Build.ps1'))
Assert ($project -match 'PublishOmniGhostRelease') 'Publish remote target is missing.'
Assert ($project -match 'prepare-publish-version') 'Publish version preparation target is missing.'
Assert ($project -match 'OmniGhostSkipVersionAdvance') 'Publish version preparation cannot be disabled for CI/preflight.'
Assert ($project -match "'\$\(Configuration\)'=='Publish'.*OmniGhostSkipRemotePublish") 'Remote target is not isolated to Publish with a CI bypass.'
Assert ($project -notmatch "'\$\(Configuration\)'=='Release'.*publish-build") 'Release invokes remote publication.'

$workflow = [IO.File]::ReadAllText((Join-Path $ProjectDir '.github\workflows\release.yml'))
Assert ($workflow -match 'workflow_dispatch:') 'Manual recovery workflow is unavailable.'
Assert ($workflow -match 'environment:\s*production') 'Production approval environment is missing.'
Assert ($workflow -match 'needs:\s*build_test_package') 'Publication does not require the validated build/package job.'
Assert ($workflow -match 'Mode Publish') 'Release workflow does not package the unsigned ZIP build.'
Assert ($workflow -notmatch 'Validate-CommercialRelease\.ps1|Test-SignedReleaseTampering\.ps1') 'Legacy signature validation remains in the workflow.'
Assert ($workflow -notmatch 'DISCORD_WEBHOOK_URL|Notify-DiscordRelease') 'Removed Discord webhook is still referenced by the workflow.'
Assert ($workflow -notmatch 'hardware_integration_test|OMNIGHOST_TEST_GAME') 'Obsolete mandatory DMA release gate is still configured.'
Assert ($publishBuild -match 'publish_github_release\.ps1') 'Publish build does not upload its validated local artifacts directly.'
Assert ($publishBuild -match 'Finalize-VersionCommit\.ps1') 'Publish does not finalize the mechanical next-version commit.'
Assert ($workflow -match 'lfs:\s*true') 'Release checkout does not fetch Git LFS assets.'
Assert ($workflow -match 'Validate-ReleaseArtifacts\.ps1') 'Workflow does not validate the final ZIP.'
Assert ($workflow -match 'SBOM\.cdx\.json') 'Workflow does not publish the SBOM.'
Assert ($workflow -notmatch 'uses:\s*[^\r\n]+@v\d') 'A GitHub Action still uses a mutable major-version reference.'

Write-Host '[OmniGhost Tests] Version rollover, synchronized metadata, publication policy and CI workflow: PASS'
