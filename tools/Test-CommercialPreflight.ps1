[CmdletBinding()]
param([string]$ProjectDir = '', [switch]$AllowMissingTag, [switch]$SourceOnly)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$global:LASTEXITCODE = -1
if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = Split-Path -Parent $PSScriptRoot }
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$errors = New-Object System.Collections.Generic.List[string]
$checks = New-Object System.Collections.Generic.List[string]
$versionPath = Join-Path $ProjectDir 'config\release\version.txt'
$metadataPath = Join-Path $ProjectDir '.cache\generated\Publish\x64\build-metadata.json'
$configPath = Join-Path $ProjectDir 'config\release\release-publish.json'
$version = if (Test-Path -LiteralPath $versionPath -PathType Leaf) { [IO.File]::ReadAllText($versionPath).Trim() } else { '' }
$commit = ''
if ($version -match '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') { $checks.Add("[OK] Release version: $version") }
else { $errors.Add("[ERROR] Missing/invalid version.txt: $versionPath") }

$git = Get-Command git -ErrorAction SilentlyContinue
$marker = Join-Path $ProjectDir '.git'
if ($null -eq $git -or -not (Test-Path -LiteralPath $marker)) {
    $errors.Add('[ERROR] No usable local Git repository. Run: git init; git add .; git commit -m "Prepare release"')
} else {
    $commitOutput = @(& $git.Source -C $ProjectDir rev-parse HEAD 2>$null)
    $commitExit = $LASTEXITCODE
    $commit = if ($commitOutput.Count -gt 0) { ([string]$commitOutput[0]).Trim() } else { '' }
    if ($commitExit -ne 0 -or $commit -notmatch '^[0-9a-fA-F]{40}$') {
        $errors.Add('[ERROR] Git has no valid HEAD commit. Run: git add .; git commit -m "Prepare release"')
    } else { $checks.Add("[OK] commit_id: $commit") }
    $status = @(& $git.Source -C $ProjectDir status --porcelain --untracked-files=normal --ignore-submodules)
    if ($LASTEXITCODE -ne 0) { $errors.Add('[ERROR] git status failed.') }
    elseif ($status.Count -gt 0) { $errors.Add("[ERROR] Working tree dirty:`n        " + ($status -join "`n        ")) }
    else { $checks.Add('[OK] Working tree clean') }
    if ($version) {
        $tag = "v$version"
        $tags = @(& $git.Source -C $ProjectDir tag --points-at HEAD)
        if ($LASTEXITCODE -ne 0) { $errors.Add('[ERROR] Could not list Git tags on HEAD.') }
        elseif ($tag -in $tags) { $checks.Add("[OK] Tag $tag on HEAD") }
        elseif ($AllowMissingTag) {
            $existingTag = @(& $git.Source -C $ProjectDir tag --list $tag)
            if ($LASTEXITCODE -ne 0) { $errors.Add("[ERROR] Could not inspect tag $tag.") }
            elseif ($existingTag.Count -gt 0) { $errors.Add("[ERROR] Tag $tag already exists on another commit. Increment version.txt; do not move a published tag.") }
            else { $checks.Add("[OK] Tag $tag will be created after local package validation") }
        } else { $errors.Add("[ERROR] Missing tag $tag on HEAD. Run: git tag -a $tag -m 'OmniGhost $version'") }
    }
}

if (-not $SourceOnly -and (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    if ([string]$metadata.app_version -ne $version) { $errors.Add("[ERROR] Version mismatch: version.txt=$version; metadata=$($metadata.app_version)") }
    else { $checks.Add('[OK] Metadata version matches') }
    if ([string]$metadata.commit_id -match '^[0-9a-fA-F]{40}$') { $checks.Add("[OK] Metadata commit: $($metadata.commit_id)") }
    else { $errors.Add("[ERROR] Metadata commit_id invalid: $($metadata.commit_id). Rebuild Publish|x64.") }
    if ($commit -match '^[0-9a-fA-F]{40}$' -and [string]$metadata.commit_id -cne $commit) {
        $errors.Add("[ERROR] Metadata commit mismatch: metadata=$($metadata.commit_id); HEAD=$commit. Rebuild Publish|x64.")
    }
    if ([string]$metadata.source_provenance -cne 'git') { $errors.Add("[ERROR] Metadata source_provenance must be git; got $($metadata.source_provenance).") }
    if ($metadata.dirty -eq $true) { $errors.Add('[ERROR] Metadata was generated from a dirty tree. Commit changes and rebuild Publish|x64.') }
    if ($metadata.reproducible -ne $true) { $errors.Add('[ERROR] Byte-for-byte reproducibility has not been verified by an independent rebuild; Commercial packaging remains blocked.') }
    foreach ($field in @('vc_tools_version','compiler_version','windows_sdk_version')) {
        $value = [string]$metadata.$field
        if ([string]::IsNullOrWhiteSpace($value) -or $value -eq 'unknown') { $errors.Add("[ERROR] Missing $field in build metadata.") }
        else { $checks.Add("[OK] ${field}: $value") }
    }
} elseif (-not $SourceOnly) { $errors.Add("[ERROR] Missing Publish metadata: $metadataPath") }

if (Test-Path -LiteralPath $configPath -PathType Leaf) {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    if ($config.requireAuthenticode -eq $true) {
        $thumbprint = ([string]$config.signingCertificateThumbprint -replace '\s','').ToUpperInvariant()
        $environmentThumb = ($env:OMNIGHOST_SIGNING_THUMBPRINT -replace '\s','').ToUpperInvariant()
        if ($environmentThumb -notmatch '^[0-9A-F]{40}$') {
            $errors.Add('[ERROR] Define OMNIGHOST_SIGNING_THUMBPRINT com os 40 caracteres do certificado Code Signing.')
        } elseif ($environmentThumb -cne $thumbprint) {
            $errors.Add('[ERROR] OMNIGHOST_SIGNING_THUMBPRINT difere do certificado público configurado. Reconfigura a confiança e recompila antes de Publish.')
        }
        $signTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
        if (-not (Test-Path -LiteralPath $signTool -PathType Leaf)) { $errors.Add("[ERROR] Required SignTool missing: $signTool") }
        if ([string]::IsNullOrWhiteSpace([string]$config.expectedPublisherSubject)) {
            $errors.Add('[ERROR] Missing expectedPublisherSubject in release-publish.json.')
        }
        if ($thumbprint -notmatch '^[0-9A-F]{40}$') { $errors.Add('[ERROR] Missing signing certificate thumbprint in release-publish.json.') }
        else {
            $cert = Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint" -ErrorAction SilentlyContinue
            if ($null -eq $cert -or -not $cert.HasPrivateKey) { $errors.Add("[ERROR] Signing certificate $thumbprint with private key not installed.") }
            else {
                $now = [DateTime]::UtcNow
                if ($cert.NotBefore.ToUniversalTime() -gt $now -or $cert.NotAfter.ToUniversalTime() -le $now) {
                    $errors.Add('[ERROR] Signing certificate is outside its validity period.')
                }
                $eku = @($cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' })
                if ($eku.Count -ne 1 -or @($eku[0].EnhancedKeyUsages | Where-Object { $_.Value -eq '1.3.6.1.5.5.7.3.3' }).Count -eq 0) {
                    $errors.Add('[ERROR] Signing certificate lacks Code Signing EKU.')
                }
                $checks.Add("[OK] Signing certificate: $thumbprint")
            }
        }
    }
} else { $errors.Add("[ERROR] Missing release config: $configPath") }

Write-Host 'Release validation:'
foreach ($check in $checks) { Write-Host $check }
foreach ($problem in $errors) { Write-Host $problem }
if ($errors.Count -gt 0) { throw "Release validation failed with $($errors.Count) problem(s)." }
