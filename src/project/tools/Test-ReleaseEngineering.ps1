[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$gitIgnore = [IO.File]::ReadAllText((Join-Path $ProjectDir '.gitignore'))

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
Assert ($gitIgnore -match '(?m)^/build/$' -and $gitIgnore -notmatch '(?m)^build/$') 'The root build ignore rule must not hide src/project/config/build property sheets.'
Assert ($gitIgnore -match '(?m)^!/src/project/third_party/dma_stack/bin/\*\.dll$') 'Canonical DMA runtime DLLs must not be hidden by the generic bin ignore rule.'
$embeddedRuntimeBuilder = Get-Content -Raw -LiteralPath (Join-Path $ProjectDir 'src\project\tools\Build-EmbeddedRuntime.ps1')
Assert ($embeddedRuntimeBuilder -match 'requiredDmaRuntime' -and $embeddedRuntimeBuilder -match "Configuration -ieq 'Publish'" -and $embeddedRuntimeBuilder -match 'Canonical DMA runtime is incomplete') 'Publish does not reject an incomplete canonical DMA runtime bundle.'
$changelogGeneratorPath = Join-Path $ProjectDir 'src\project\tools\Generate-Changelog.ps1'
$changelogGeneratorBytes = [IO.File]::ReadAllBytes($changelogGeneratorPath)
Assert ($changelogGeneratorBytes.Length -ge 3 -and $changelogGeneratorBytes[0] -eq 0xEF -and $changelogGeneratorBytes[1] -eq 0xBB -and $changelogGeneratorBytes[2] -eq 0xBF) 'Generate-Changelog.ps1 must carry an UTF-8 signature for Windows PowerShell 5.1 compatibility.'
$strictUtf8 = New-Object Text.UTF8Encoding -ArgumentList @($false, $true)
foreach ($localizedScript in Get-ChildItem (Join-Path $ProjectDir 'src\project\tools') -Recurse -File -Filter '*.ps1') {
    $scriptBytes = [IO.File]::ReadAllBytes($localizedScript.FullName)
    $hasUtf8Signature = $scriptBytes.Length -ge 3 -and $scriptBytes[0] -eq 0xEF -and $scriptBytes[1] -eq 0xBB -and $scriptBytes[2] -eq 0xBF
    $textOffset = if ($hasUtf8Signature) { 3 } else { 0 }
    $scriptText = $strictUtf8.GetString($scriptBytes, $textOffset, $scriptBytes.Length - $textOffset)
    $containsLocalizedText = $null -ne ($scriptText.ToCharArray() | Where-Object { [int]$_ -gt 127 } | Select-Object -First 1)
    Assert (-not $containsLocalizedText -or $hasUtf8Signature) "Localized PowerShell script requires an UTF-8 signature for Windows PowerShell 5.1: $($localizedScript.FullName)"
}
foreach ($propertySheet in @('OmniGhost.Common.props', 'OmniGhost.Release.props', 'OmniGhost.Publish.props')) {
    Assert (Test-Path -LiteralPath (Join-Path $ProjectDir "src\project\config\build\$propertySheet") -PathType Leaf) "Required MSBuild property sheet is missing: $propertySheet"
}

$version = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\config\release\version.txt')).Trim()
$header = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\app_version.h'))
$resource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\resources\app.rc'))
Assert (Is-Version $version) "version.txt is invalid: $version"
Assert ($header -match ('Version\[\]\s*=\s*"' + [regex]::Escape($version) + '"')) 'app_version.h is not synchronized.'
$p = $version.Split('.')
Assert ($resource -match "OMNIGHOST_VERSION_MAJOR\s+$($p[0])") 'app.rc major is not synchronized.'
Assert ($resource -match "OMNIGHOST_VERSION_MINOR\s+$($p[1])") 'app.rc minor is not synchronized.'
Assert ($resource -match "OMNIGHOST_VERSION_PATCH\s+$($p[2])") 'app.rc patch is not synchronized.'

$policy = Get-Content -LiteralPath (Join-Path $ProjectDir 'src\project\config\release\release-publish.json') -Raw | ConvertFrom-Json
Assert ($policy.publishAfterBuild -eq $true) 'Publish must upload automatically after a successful build.'
Assert ($policy.requireExplicitPublishCommand -eq $false) 'Selecting Publish is the explicit command.'
Assert ($policy.requireConfirmation -eq $false) 'Publish must remain non-interactive inside MSBuild.'
Assert ($policy.requireCleanReproducibleBuild -eq $true) 'Clean reproducible source must be required.'
Assert ($policy.requireTagRelease -eq $true) 'A matching release tag must be required.'
Assert ($policy.allowDeterministicSourceSnapshot -eq $true) 'Deterministic source snapshot publication must be explicitly enabled for this extracted project.'
Assert (-not ($policy.PSObject.Properties.Name -contains 'requireAuthenticode')) 'Release policy must not depend on Authenticode.'
Assert (-not ($policy.PSObject.Properties.Name -contains 'requireManifestSignature')) 'Release policy must not depend on detached signatures.'

$project = [IO.File]::ReadAllText((Join-Path $ProjectDir 'OmiGhost.vcxproj'))
$publishBuild = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\tools\Publish-Build.ps1'))
$powerShellBridge = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\tools\Run-OmniGhostPowerShell.cmd'))
$packageRelease = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\tools\package_release.ps1'))
$publishGithub = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\tools\publish_github_release.ps1'))
$prepareVersion = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\tools\Prepare-PublishVersion.ps1'))
$reproBuild = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\tools\Verify-ReproducibleBuild.ps1'))
Assert ($project -match 'PublishOmniGhostRelease') 'Publish remote target is missing.'
Assert ($project -match 'prepare-publish-version') 'Publish version preparation target is missing.'
Assert ($project -match 'OmniGhostSkipVersionAdvance') 'Publish version preparation cannot be disabled for CI/preflight.'
Assert ($project -match "'\$\(Configuration\)'=='Publish'.*OmniGhostSkipRemotePublish") 'Remote target is not isolated to Publish with a CI bypass.'
Assert ($project -notmatch "'\$\(Configuration\)'=='Release'.*publish-build") 'Release invokes remote publication.'
Assert ($powerShellBridge -match 'Publish-Build\.ps1[^\r\n]*-ExplicitConfirmed') 'The Publish target bridge does not authorize the guarded publisher.'
Assert ($powerShellBridge -match 'chcp 65001') 'The MSBuild PowerShell bridge does not establish UTF-8 console output.'
Assert ($project -match 'OmniGhost 01/10' -and $project -match 'OmniGhost 05/10') 'The Publish build does not expose numbered phases.'
Assert ($reproBuild -match 'worktree add --quiet' -and $reproBuild -match 'repro-build\.log') 'The reproducible build does not preserve a quiet detailed log.'
Assert ($reproBuild -match 'requiredCommittedFiles' -and $reproBuild -match 'OmniGhost\.Common\.props' -and $reproBuild -match 'cat-file -e') 'Reproducibility build does not verify required property sheets in the source commit.'
Assert ($reproBuild -notmatch 'Select-Object -Last 80') 'Nested MSBuild diagnostics are still replayed into the Visual Studio error list.'
Assert ($packageRelease -notmatch 'SignTool|Get-AuthenticodeSignature|Sign-UpdateManifest') 'Release packaging still depends on signing tools.'

$workflow = [IO.File]::ReadAllText((Join-Path $ProjectDir '.github\workflows\release.yml'))
Assert ($workflow -match 'workflow_dispatch:') 'Manual recovery workflow is unavailable.'
Assert ($workflow -match 'environment:\s*production') 'Production approval environment is missing.'
Assert ($workflow -match 'needs:\s*build_test_package') 'Publication does not require the validated build/package job.'
Assert ($workflow -match 'Mode Publish') 'Release workflow does not package the validated Publish build.'
Assert ($workflow -notmatch 'Validate-CommercialRelease\.ps1|Test-SignedReleaseTampering\.ps1') 'Legacy signature validation remains in the workflow.'
Assert ($workflow -notmatch 'DISCORD_WEBHOOK_URL|Notify-DiscordRelease') 'Removed Discord webhook is still referenced by the workflow.'
Assert ($workflow -notmatch 'hardware_integration_test|OMNIGHOST_TEST_GAME') 'Obsolete mandatory DMA release gate is still configured.'
Assert ($publishBuild -match 'publish_github_release\.ps1') 'Publish build does not upload its validated local artifacts directly.'
Assert ($publishBuild -match 'Write-OmniPhase 9[\s\S]*Finalize-VersionCommit\.ps1[\s\S]*publishCompleted\s*=\s*\$true[\s\S]*finally[\s\S]*Cleanup-PrePublish\.ps1') 'Temporary data cleanup must be the final action after GitHub publication and version finalization.'
$cleanupPrePublish = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\project\tools\Cleanup-PrePublish.ps1'))
Assert ($cleanupPrePublish -match 'DeferredCacheOnly' -and $cleanupPrePublish -match 'ParentProcessId' -and $cleanupPrePublish -match 'foreach \(\$relative in @\(''\.temp'', ''logs''\)\)') 'The active MSBuild cache must be cleaned only after its file logger releases OmiGhost.log.'
Assert ($publishBuild -match 'Finalize-VersionCommit\.ps1') 'Publish does not finalize the mechanical next-version commit.'
Assert ($publishBuild -match 'Invoke-NativeCommandCapture' -and
        $publishBuild -match "ErrorActionPreference = 'Continue'" -and
        $publishBuild -match '''push'', ''origin'', \$tag') `
    'Git push stderr can still be promoted to a terminating NativeCommandError.'
Assert ($packageRelease -match 'Copy-Item -LiteralPath \$ReleaseMetaSource -Destination \$ReleaseMeta') 'Packaging does not copy release-meta.json into the validated release directory.'
Assert ($packageRelease -match 'Assert-CopyMatches -Source \$ReleaseMetaSource -Destination \$ReleaseMeta') 'Packaging does not verify the copied release-meta.json.'
Assert ($prepareVersion -match 'tag --list.*currentTag') 'Publish preparation does not test tag existence safely on Windows PowerShell 5.1.'
Assert ($prepareVersion -match 'rev-list -n 1.*currentTag') 'Publish preparation does not detect a version tag owned by another commit.'
Assert ($prepareVersion -match 'Increment-Version\.ps1') 'Publish preparation does not advance automatically past an occupied tag.'
Assert ($prepareVersion -match 'maximumAdvances') 'Automatic version selection has no bounded safety limit.'
Assert ($workflow -match 'lfs:\s*true') 'Release checkout does not fetch Git LFS assets.'
Assert ($workflow -match 'Validate-ReleaseArtifacts\.ps1') 'Workflow does not validate the final ZIP.'
Assert ($workflow -match 'SBOM\.cdx\.json') 'Workflow does not publish the SBOM.'
Assert ($publishGithub -match "return\s+@\(\s*'OmniGhost\.zip',\s*'update\.json'\s*\)") 'Direct publication is not restricted to the essential public assets.'
Assert ($workflow -match '\$expected\s*=\s*@\(''OmniGhost\.zip'',''update\.json''\)') 'CI publication is not restricted to the essential public assets.'
Assert ($workflow -notmatch 'uses:\s*[^\r\n]+@v\d') 'A GitHub Action still uses a mutable major-version reference.'

Write-Host '[OmniGhost Tests] Version rollover, synchronized metadata, publication policy and CI workflow: PASS'
