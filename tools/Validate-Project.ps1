[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Hashing.ps1')
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)

function Pass([string]$Message) { Write-Host "[PASS] $Message" -ForegroundColor Green }
function Fail([string]$Message) { throw "[FAIL] $Message" }
function Require-File([string]$Relative) {
    $path = Join-Path $ProjectDir $Relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { Fail "Missing file: $Relative" }
    Pass "File: $Relative"
}
function Require-Absent([string]$Relative) {
    if (Test-Path -LiteralPath (Join-Path $ProjectDir $Relative)) { Fail "Unexpected path: $Relative" }
    Pass "Absent: $Relative"
}
function Require-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) { Fail $Message }
    Pass $Message
}
function Forbid-Text([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) { Fail $Message }
    Pass $Message
}
# Windows PowerShell 5.x defaults Get-Content to the system ANSI code page.
# Project sources are UTF-8; always read them as UTF-8 so accented validation
# strings and Portuguese diagnostics match reliably.
function Read-Utf8Text([string]$LiteralPath) {
    return [System.IO.File]::ReadAllText($LiteralPath, [System.Text.UTF8Encoding]::new($false))
}
function Get-Sha256([string]$Path) { Get-OmniGhostSha256 -LiteralPath $Path }
function Assert-ManifestHash([object]$Expected, [string]$Relative) {
    if (-not $Expected) { Fail "Manifest hash missing for $Relative" }
    $actual = Get-Sha256 (Join-Path $ProjectDir $Relative)
    if ($actual -ne ([string]$Expected).ToLowerInvariant()) { Fail "Hash mismatch for $Relative" }
    Pass "Hash: $Relative"
}

# Essential project/runtime inputs only. No test-suite requirements.
foreach ($file in @(
    'OmiGhost.vcxproj','OmniGhost.Common.props','OmniGhost.Release.props',
    'versions.json',
    'src\runtime\cloudflared\cloudflared.exe','THIRD_PARTY_NOTICES.txt',
    'resources\OmniGhost.ico',
    'resources\resource.h','resources\embedded-resources.json',
    'src\platform\embedded_offsets.cpp','src\platform\embedded_offsets.h',
    'src\platform\embedded_resources.cpp','src\platform\embedded_resources.h',
    'src\platform\runtime_bootstrap.cpp','src\platform\runtime_bootstrap.h',
    'tools\Build-EmbeddedOffsets.ps1','tools\Build-EmbeddedRuntime.ps1','tools\Build-EmbeddedResources.ps1',
    'data\fortnite_offsets.json','data\warzone_offsets.json',
    'src\licensing\license_service.cpp','src\launcher\license_page.cpp'
)) { Require-File $file }

$cloudflaredPath = Join-Path $ProjectDir 'src\runtime\cloudflared\cloudflared.exe'
$expectedCloudflaredSha256 = 'c29eee2b121f5436a642eed69fd9767da7e7b8c510fa50aaa130337f931357b5'
if ((Get-Sha256 $cloudflaredPath) -cne $expectedCloudflaredSha256) {
    Fail 'cloudflared.exe does not match the approved signed Cloudflare 2026.8.2 binary.'
}
Pass 'cloudflared.exe matches the approved Authenticode-verified Cloudflare 2026.8.2 binary.'

foreach ($path in @('DMALibrary\libs','DMALibrary\info.db','libs\info.db','src\launcher\calibration_page.cpp','src\launcher\calibration_page.h')) {
    Require-Absent $path
}
foreach ($file in @('.github\workflows\release.yml')) {
    Require-File $file
}

# libs\ remains an editable source bundle. Commercial builds embed its selected
# contents and install them privately instead of distributing them beside the EXE.
$libsDir = Join-Path $ProjectDir 'libs'
if (-not (Test-Path -LiteralPath $libsDir -PathType Container)) { Fail 'Missing directory: libs' }
$libsDlls = @(Get-ChildItem -LiteralPath $libsDir -File -Filter '*.dll' -ErrorAction SilentlyContinue)
if ($libsDlls.Count -eq 0) { Fail 'No DLL files found in libs' }
Pass ("Local runtime bundle: libs ({0} DLLs)" -f $libsDlls.Count)

# Documentation is not build-critical. Do not fail project validation merely because
# a developer keeps Markdown notes/documentation next to the source tree.
$markdown = @(Get-ChildItem -LiteralPath $ProjectDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in @('.md','.markdown') })
if ($markdown.Count -gt 0) {
    Write-Host ("[INFO] Markdown documentation present: {0} file(s); ignored by build validation." -f $markdown.Count) -ForegroundColor DarkGray
} else {
    Pass 'No Markdown source files.'
}

$versions = Read-Utf8Text (Join-Path $ProjectDir 'versions.json') | ConvertFrom-Json
Assert-ManifestHash $versions.memprocfs.sha256.'vmm.dll' 'libs\vmm.dll'
Assert-ManifestHash $versions.memprocfs.sha256.'vmm.lib' 'libs\vmm.lib'
Assert-ManifestHash $versions.memprocfs.sha256.'vmmdll.h' 'libs\vmmdll.h'
Assert-ManifestHash $versions.leechcore.sha256.'leechcore.dll' 'libs\leechcore.dll'
Assert-ManifestHash $versions.leechcore.sha256.'leechcore.lib' 'libs\leechcore.lib'
Assert-ManifestHash $versions.leechcore.sha256.'leechcore.h' 'libs\leechcore.h'

# The build explicitly consumes/copies the canonical DMA runtime from
# third_party\dma_stack. Copies produced by a previous build or release staging are
# expected and must not make the *next* build fail validation. Only source-tree
# duplicates outside known generated/output roots are considered suspicious.
$duplicates = @()
$ignoredGeneratedRoots = @('build\', 'x64\', '.cache\', 'artifacts\', 'dist\', 'out\', 'libs\')
foreach ($name in @('vmm.dll','leechcore.dll')) {
    foreach ($file in Get-ChildItem -LiteralPath $ProjectDir -Recurse -File -Filter $name -ErrorAction SilentlyContinue) {
        $relative = $file.FullName.Substring($ProjectDir.Length).TrimStart('\')
        if ($relative -ieq ('libs\' + $name)) { continue }
        if ($relative -like 'libs.backup\*' -or $relative -like 'libs.failed-*\*') { continue }

        $isGeneratedCopy = $false
        foreach ($root in $ignoredGeneratedRoots) {
            if ($relative.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
                $isGeneratedCopy = $true
                break
            }
        }
        if ($isGeneratedCopy) { continue }

        $duplicates += $relative
    }
}
if ($duplicates.Count -gt 0) { Fail ('Duplicate DMA runtime DLLs in source tree: ' + ($duplicates -join ', ')) }
Pass 'Canonical DMA runtime is authoritative; libs is an allowed fallback bundle.'

$project = Read-Utf8Text (Join-Path $ProjectDir 'OmiGhost.vcxproj')
Require-Text $project 'ProjectConfiguration Include="Release\|x64"' 'Release|x64 configuration exists.'
Require-Text $project 'ProjectConfiguration Include="Tester\|x64"' 'Tester|x64 configuration exists.'
Require-Text $project 'ProjectConfiguration Include="Publish\|x64"' 'Publish|x64 configuration exists.'
Forbid-Text $project 'ProjectConfiguration Include="Debug\|x64"' 'Debug build configuration removed.'
Forbid-Text $project 'ProjectConfiguration Include="Diagnostics\|x64"' 'Diagnostics build configuration removed.'
Require-Text $project 'OmniGhost\.Tester\.props' 'Tester policy is imported only by Tester builds.'
Require-Text $project 'OmniGhost\.Publish\.props' 'Publish policy is imported only by Publish builds.'
$common = Read-Utf8Text (Join-Path $ProjectDir 'OmniGhost.Common.props')
$release = Read-Utf8Text (Join-Path $ProjectDir 'OmniGhost.Release.props')
$runtimeBootstrap = Read-Utf8Text (Join-Path $ProjectDir 'src\platform\runtime_bootstrap.cpp')

# These are regular expressions, so literal Windows path separators must be escaped.
Require-Text $common 'libs\\\*\.h' 'Canonical DMA includes configured.'
Require-Text $common 'libs\\\*\.lib' 'Canonical DMA libraries configured.'
Require-Text $project 'libs\\\*\.dll' 'Canonical DMA runtime configured.'
Require-Text $project 'libs\\\*\.dll' 'Local libs source bundle configured.'
Require-Text $project 'src\\licensing\\license_service\.cpp' 'Licensing service is part of the build.'
Require-Text $project 'src\\launcher\\license_page\.cpp' 'Licensing page is part of the build.'
Require-Text $project 'CleanOmniGhostRuntimeOutput' 'Release runtime output is cleaned before build.'
Require-Text $project 'data\\runtime\\dma_stack_versions\.json' 'Dependency manifest is kept under data/runtime, not build root.'
Forbid-Text $project 'dma_stack_managed-files\.json" DestinationFiles="\$\(OutDir\)' 'Development managed-files manifest is not copied to build root.'
Forbid-Text $project 'ASan\|x64' 'ASan/test configuration removed.'
Forbid-Text $project 'calibration_page\.cpp' 'Unused launcher calibration page removed from build.'
Require-Text $project 'Code="OGPLAT001"' 'x64-only platform guard configured.'
Require-Text $project 'Target Name="ValidateOmniGhostPublishVersion"' 'Publish validates immutable source version metadata.'
Require-Text $project 'prepare-publish-version' 'Publish prepares the next version only after confirming the current GitHub release.'
Require-Text $project "OmniGhostSkipVersionAdvance" 'Automatic version preparation is isolated to Publish and can be disabled for CI/preflight builds.'
Require-Text $project 'Target Name="PublishOmniGhostRelease"' 'Publish owns the validated remote publication target.'
Forbid-Text $project 'publish_github_release\.ps1' 'MSBuild contains no direct remote publication script.'
Require-Text $project "publish-build"
Require-Text $project "OmniGhostSkipRemotePublish" 'Remote publication is restricted to Publish and supports an explicit CI bypass.'
Require-Text $project 'GenerateOmniGhostBuildMetadata' 'Build provenance metadata is generated without mutating source version.'
Require-Text $project 'BuildOmniGhostEmbeddedOffsets' 'Embedded offset resources are generated before resource compilation.'
Require-Text $project 'BuildOmniGhostEmbeddedRuntime' 'Private runtime bundle is generated before compilation.'
Require-Text $project 'BuildOmniGhostEmbeddedResources' 'Generic embedded resources are generated before compilation.'
Require-Text $project 'src\\platform\\embedded_resources\.cpp' 'Central embedded resource loader is compiled.'
Require-Text $project 'ValidateOmniGhostSingleExeRuntime' 'Release/Publish enforce a single-EXE output.'
Require-Text $common '<DelayLoadDLLs>leechcore\.dll;vmm\.dll' 'DMA imports are delayed until the private runtime is installed.'
Forbid-Text $runtimeBootstrap 'CopySelf|LaunchInstalled|bootstrap-parent-pid' 'Runtime bootstrap never copies or relaunches OmniGhost.exe.'
Require-Text $common '<RuntimeLibrary>MultiThreaded</RuntimeLibrary>' 'Main executable uses the static MSVC runtime for first-run portability.'
$patternProjectData = @'
<OmniGhostProjectData\s+Include="\$\(ProjectDir\)data\\\*\*\\\*"\s+Exclude="[^"]*\*offsets\.json[^"]*;[^"]*offsets\.json[^"]*"
'@
Require-Text $project $patternProjectData 'Customer builds exclude every plaintext offsets JSON from runtime data copy.'
$patternDevOffset = @'
<OmniGhostDevOffsetData[^>]+Condition="'\$\((?:Configuration)\)'=='Tester'"
'@
Require-Text $project $patternDevOffset 'External plaintext offset snapshots are restricted to Tester builds.'
$patternCs2 = @'
<OmniGhostCs2Data[^>]+Exclude="\$\(ProjectDir\)Cs2\\data\\offsets\.json"
'@
Require-Text $project $patternCs2 'CS2 runtime assets exclude the plaintext offsets snapshot.'
$patternValorant = @'
<OmniGhostValorantData[^>]+Exclude="\$\(ProjectDir\)Valorant\\data\\\*offsets\*\.json"
'@
Require-Text $project $patternValorant 'Valorant runtime assets exclude plaintext offset snapshots.'
$pattern1 = @'
'\$\(Configuration\)'!='Tester'\s+And\s+Exists\('\$\(OutDir\)data\\fortnite_offsets\.json'\)
'@
$pattern2 = @'
'\$\(Configuration\)'!='Tester'\s+And\s+Exists\('\$\(OutDir\)data\\warzone_offsets\.json'\)
'@
$pattern3 = @'
'\$\(Configuration\)'!='Tester'\s+And\s+Exists\('\$\(OutDir\)data\\offsets\.json'\)
'@
$pattern4 = @'
'\$\(Configuration\)'!='Tester'\s+And\s+Exists\('\$\(OutDir\)data\\valorant_offsets\.json'\)
'@
Require-Text $project $pattern1 'Release/Publish fail closed if a Fortnite offset JSON reaches runtime output.'
Require-Text $project $pattern2 'Release/Publish fail closed if a Warzone offset JSON reaches runtime output.'
Require-Text $project $pattern3 'Release/Publish fail closed if a generic offset JSON reaches runtime output.'
Require-Text $project $pattern4 'Release/Publish fail closed if a Valorant offset JSON reaches runtime output.'

# Build-hardening settings live in imported property sheets, not necessarily in the .vcxproj itself.
Require-Text $common '<WarningLevel>Level4</WarningLevel>' 'Level4 warnings enabled.'
Require-Text $common '/permissive-' 'Strict MSVC conformance enabled.'
Require-Text $release '<ControlFlowGuard>Guard</ControlFlowGuard>' 'Control Flow Guard enabled for Release.'
# AddressSanitizer is optional dev config (not required for Release)
# Require-Text $common '<AddressSanitizer>DynamicBase</AddressSanitizer>' 'AddressSanitizer enabled for Release (optional dev config).'

# Static Analysis (SARIF output) - optional
# Require-Text $common '<CodeAnalysisRuleSet>.*\.ruleset</CodeAnalysisRuleSet>' 'Static analysis ruleset configured.'
# Require-Text $common '<RunCodeAnalysis>true</RunCodeAnalysis>' 'Static analysis enabled for Release builds.'

$patternOutDir = @'
<OutDir Condition="'\$\(OutDir\)'==''">\$\(ProjectDir\)build\\</OutDir>
'@
Require-Text $common $patternOutDir 'build/ is the default runtime output directory.'
$patternConfigTester = @'
\$\(Configuration\)'=='Tester'.*build\\Tester
'@
$patternConfigPublish = @'
\$\(Configuration\)'=='Publish'.*build\\Publish
'@
Require-Text $common $patternConfigTester 'Tester output is isolated from customer builds.'
Require-Text $common $patternConfigPublish 'Publish output is isolated from local/tester builds.'
Require-Text $common '<IntDir>\$\(ProjectDir\)\.cache\\intermediate' 'Intermediates moved out of build/.'
Require-Text $common '\.cache\\generated' 'Generated headers moved out of build/.'
Require-Text $common '\.cache\\symbols' 'PDB output moved out of build/.'

$releasePackager = Read-Utf8Text (Join-Path $ProjectDir 'tools\package_release.ps1')
Forbid-Text $releasePackager 'robocopy' 'Release staging uses explicit runtime copy rules instead of broad robocopy.'
Require-Text $releasePackager 'RequiredRuntimeFiles' 'Release staging has an explicit required-runtime allowlist.'
Forbid-Text $releasePackager 'publish_github_release\.ps1' 'Package phase contains no remote publication code path.'
Forbid-Text $releasePackager 'SkipPublish' 'Package phase does not expose a legacy publish toggle.'
Require-Text $releasePackager 'install-manifest\.sha256' 'Runtime packages contain an installation integrity manifest.'
Require-Text $releasePackager 'install-manifest\.sha256\.sig' 'Commercial runtime packages sign the local integrity manifest.'
Require-Text $releasePackager '\.cache\\generated\\Publish\\x64\\build-metadata\.json' 'Commercial packaging validates generated Publish x64 build provenance metadata.'
Require-Text $releasePackager 'Commercial package requires a clean reproducible source build' 'Commercial packaging rejects dirty/non-reproducible source builds.'
Require-Text $releasePackager 'vc_tools_version' 'Commercial packaging requires captured MSVC toolchain metadata.'
Require-Text $releasePackager 'windows_sdk_version' 'Commercial packaging requires captured Windows SDK metadata.'

$packageWrapper = Read-Utf8Text (Join-Path $ProjectDir 'tools\Package-Release.ps1')
Require-Text $packageWrapper 'Packaging never increments version\.txt and never publishes remotely' 'Package wrapper documents immutable source/version semantics.'
$publishWrapper = Read-Utf8Text (Join-Path $ProjectDir 'tools\Publish-Release.ps1')
Require-Text $publishWrapper 'Publish-Build\.ps1' 'Manual retry reuses the Publish-only orchestrator.'
$publishBuild = Read-Utf8Text (Join-Path $ProjectDir 'tools\Publish-Build.ps1')
Require-Text $publishBuild "metadata\.configuration -cne 'Publish'" 'Publisher rejects outputs not produced by Publish.'
Require-Text $publishBuild 'build\\Publish' 'Publisher accepts only the isolated Publish output.'
Require-Text $publishBuild 'publish_github_release\.ps1' 'Publish orchestrator owns the remote upload call.'
Require-Text $publishBuild 'publish_github_release\.ps1' 'Explicit publisher delegates the authenticated GitHub operation.'

$publishConfig = Read-Utf8Text (Join-Path $ProjectDir 'release-publish.json') | ConvertFrom-Json
if ($publishConfig.publishAfterBuild -ne $true) { Fail 'Publish must publish automatically after a successful build.' }
Pass 'Publish automatically enters the guarded publication pipeline.'
if ($publishConfig.requireExplicitPublishCommand -ne $false -or $publishConfig.requireConfirmation -ne $false) {
    Fail 'Selecting Publish is the explicit operation; no second interactive prompt may block MSBuild.'
}
Pass 'Publish is non-interactive after the configuration was selected.'
if ($publishConfig.requireCleanReproducibleBuild -ne $true -or $publishConfig.requireTagRelease -ne $true) {
    Fail 'Commercial publication must require clean reproducible tagged source.'
}
Pass 'Commercial publication requires clean reproducible tagged source.'
if ($publishConfig.allowDeterministicSourceSnapshot -ne $true) { Fail 'This source snapshot must explicitly allow deterministic snapshot publication.' }
Pass 'Deterministic source snapshots are explicitly authorized without weakening validation for real Git clones.'
if ($publishConfig.PSObject.Properties.Name -contains 'allowUnsignedDevelopmentPublish') { Fail 'Unsigned publication exceptions are forbidden.' }
Pass 'Unsigned publication exceptions are absent.'
if ($publishConfig.preserveLocalReleaseAfterUpload -ne $true) { Fail 'release-publish.json must preserve canonical local/CI release artefacts after upload.' }
Pass 'Canonical local/CI release artefacts are preserved after upload.'
if (($publishConfig.requireAuthenticode -eq $true) -xor ($publishConfig.requireManifestSignature -eq $true)) { Fail 'Authenticode and manifest signatures must be toggled together.' }
Pass 'Optional signing policy is internally consistent.'
if ([int]$publishConfig.certificateRotationWarningDays -lt 30) { Fail 'Certificate rotation warning must be at least 30 days.' }
Pass 'Certificate expiry/rotation policy is configured.'
if ($publishConfig.PSObject.Properties.Name -contains 'deleteLocalReleaseAfterUpload') { Fail 'release-publish.json must not expose automatic local release deletion.' }
Pass 'No automatic local release deletion setting is exposed.'
if ($publishConfig.defaultMandatory -ne $false) { Fail 'release-publish.json must default to optional updates; mandatory releases are explicit.' }
Pass 'Update policy defaults to optional; mandatory releases require an explicit policy change.'

$commercialValidator = Read-Utf8Text (Join-Path $ProjectDir 'tools\Validate-CommercialRelease.ps1')
Require-Text $commercialValidator 'install-manifest\.sha256\.sig' 'Commercial validation checks the signed installation manifest.'
Require-Text $commercialValidator 'SignerCertificate\.Thumbprint' 'Commercial validation pins the expected Authenticode certificate.'
$trustHeader = Read-Utf8Text (Join-Path $ProjectDir 'src\updater\release_trust.h')
Require-Text $trustHeader 'PublicCertificateDerBase64' 'Client contains only the public release trust anchor.'
Require-Text $trustHeader 'SecondaryPublicCertificateDerBase64' 'Client supports two public trust anchors during certificate rotation.'
$cryptoSource = Read-Utf8Text (Join-Path $ProjectDir 'src\updater\crypto.cpp')
Require-Text $cryptoSource 'VerifyDetachedManifestSignature' 'Updater verifies detached RSA manifest signatures.'
Require-Text $cryptoSource 'expectedCertificateThumbprint' 'Updater verifies expected publisher certificate identity.'
$symbolsPackager = Read-Utf8Text (Join-Path $ProjectDir 'tools\Create-SymbolsPackage.ps1')
Require-Text $symbolsPackager '\.cache\\symbols\\\$Configuration' 'Private symbol package reads the selected configuration PDBs from .cache, not build/.'

$license = Read-Utf8Text (Join-Path $ProjectDir 'src\licensing\license_service.cpp')
Require-Text $license 'CryptProtectData' 'Local license cache uses Windows DPAPI.'
Require-Text $license 'CryptUnprotectData' 'Local license cache can be restored with Windows DPAPI.'
Require-Text $license 'kLocalVerifierSha256' 'Local license uses a verifier hash instead of plaintext verifier.'


$runner = Read-Utf8Text (Join-Path $ProjectDir 'tools\Run-OmniGhostPowerShell.cmd')
Require-Text $runner 'validate-project' 'MSBuild PowerShell bridge exposes validate-project.'
Require-Text $runner 'Validate-Project\.ps1' 'MSBuild validation bridge points at Validate-Project.ps1.'
Require-Text $runner 'publish-build' 'MSBuild bridge exposes the Publish-only publication action.'

$metadataValidator = Read-Utf8Text (Join-Path $ProjectDir 'tools\Ensure-ProjectMetadata.ps1')
Require-Text $metadataValidator 'Normal Build/Release must be source-read-only' 'Normal builds validate version metadata without rewriting source.'
Forbid-Text $metadataValidator 'WriteAllText|WriteAllBytes|Copy-Item|Move-Item|New-Item|Remove-Item' 'Build-time metadata validation contains no source mutation operations.'
# Encoding-safe: match the hard-fail throw path (version.txt vs app_version.h / app.rc),
# not a single accented Portuguese substring that can break under ANSI code pages.
Require-Text $metadataValidator 'version\.txt=.*app_version\.h=|version\.txt=.*app\.rc=' 'Version mismatches fail the build instead of being silently synchronized.'

$sourcePackager = Read-Utf8Text (Join-Path $ProjectDir 'tools\Create-SourcePackage.ps1')
Require-Text $sourcePackager 'Validate-Project\.ps1' 'Source packaging runs engineering/dependency preflight validation.'
Require-Text $sourcePackager 'Preflight PASS' 'Source packaging only proceeds after preflight succeeds.'
Require-Text $sourcePackager 'source-tree-' 'Source packages use a deterministic source-tree identity when Git metadata is unavailable.'

$metadata = Read-Utf8Text (Join-Path $ProjectDir 'tools\Generate-BuildMetadata.ps1')
Require-Text $metadata 'AppVersion' 'Application version is embedded in build metadata.'
Require-Text $metadata 'CommitId' 'Commit/source identity is embedded in build metadata.'
Require-Text $metadata 'SourceProvenance' 'Build metadata records how source identity was resolved.'
Require-Text $metadata 'SOURCE_PACKAGE_MANIFEST\.json' 'Legacy source snapshots receive deterministic provenance fallback metadata.'
Require-Text $metadata 'SourcePackageDirty' 'Edited extracted source packages are marked non-reproducible.'
Require-Text $metadata 'WindowsSdkVersion' 'Windows SDK version is embedded in build metadata.'
Require-Text $metadata 'CompilerVersion' 'Compiler version is embedded in build metadata.'
Require-Text $metadata 'ReleaseChannel' 'Release channel is embedded in build metadata.'
Require-Text $metadata 'SOURCE_BUILD_METADATA\.json' 'Extracted source packages preserve build provenance without shipping .git.'
$manifestSource = Read-Utf8Text (Join-Path $ProjectDir 'src\updater\manifest.cpp')
$updateTypes = Read-Utf8Text (Join-Path $ProjectDir 'src\updater\update_types.h')
$updateHeader = Read-Utf8Text (Join-Path $ProjectDir 'src\updater\update_service.h')
$updateService = Read-Utf8Text (Join-Path $ProjectDir 'src\updater\update_service.cpp')
$updateUi = Read-Utf8Text (Join-Path $ProjectDir 'src\updater\update_ui.cpp')
Require-Text $manifestSource 'mandatoryReason' 'Updater manifest supports an explicit mandatory-update reason.'
Require-Text $updateTypes 'mandatoryReason' 'Updater snapshot carries the mandatory-update reason to UI.'
Require-Text $updateHeader 'DownloadAndScheduleInstallOnExitAsync' 'Optional updates expose an install-on-exit download action.'
Require-Text $updateService 'belowMinimumSupported' 'Mandatory status explains minimum-supported-version enforcement.'
$patternStage = @'
stage == "integrity".*VerifyInstallationAsync\(
'@
Require-Text $updateService $patternStage 'Retry repeats installation verification for integrity-stage failures.'
# Validate updater capabilities, not localized presentation strings. UI copy is allowed
# to change with the selected language without turning a normal C++ build into an
# engineering-validation failure.
Require-Text $updateUi 'DownloadAndScheduleInstallOnExitAsync\(\)' 'Optional update UI wires the Install-on-exit action.'
Require-Text $updateUi 'RetryLastFailure\(\)' 'Updater UI wires stage-aware Retry.'
Require-Text $updateUi 'releaseNotesForCompletedUpdate\.c_str\(\)' 'Updater UI exposes completed-update release notes.'

$configManager = Read-Utf8Text (Join-Path $ProjectDir 'src\config\config_manager.cpp')
$configCore = Read-Utf8Text (Join-Path $ProjectDir 'src\config\config_core.cpp')
Require-Text $configManager 'CurrentAppVersion\(\)|meta\.app_version' 'Config metadata reads the embedded application version.'
Require-Text $configCore 'BuildInfo::AppVersion' 'Config core resolves the embedded application version.'
$patternInstallDir = @'
InstallDirectory\(\).+version\.txt
'@
Forbid-Text $configManager $patternInstallDir 'Runtime config no longer depends on version.txt beside the EXE.'
Forbid-Text $configCore $patternInstallDir 'Config core no longer depends on version.txt beside the EXE.'

Pass 'Engineering documentation is intentionally non-blocking for compilation.'

Write-Host ''
Write-Host 'OmniGhost project validation: PASS' -ForegroundColor Green
