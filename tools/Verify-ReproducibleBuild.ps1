[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$PrimaryExecutable,
    [Parameter(Mandatory=$true)][string]$MSBuildPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$PrimaryExecutable = [IO.Path]::GetFullPath($PrimaryExecutable)
$MSBuildPath = [IO.Path]::GetFullPath($MSBuildPath)
$metadataPath = Join-Path $ProjectDir '.cache\generated\Publish\x64\build-metadata.json'

if (-not (Test-Path -LiteralPath $PrimaryExecutable -PathType Leaf)) { throw "Primary unsigned executable missing: $PrimaryExecutable" }
if (-not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) { throw "MSBuild missing: $MSBuildPath" }
if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) { throw "Publish metadata missing: $metadataPath" }

$git = (Get-Command git -ErrorAction Stop).Source
$status = @(& $git -C $ProjectDir status --porcelain --untracked-files=normal --ignore-submodules)
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0) { throw 'Independent rebuild requires a clean Git worktree.' }
$commit = (& $git -C $ProjectDir rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'Could not resolve HEAD for independent rebuild.' }

$reproRoot = Join-Path ([IO.Path]::GetTempPath()) ('omnighost-repro-' + [Guid]::NewGuid().ToString('N'))
$worktreeAdded = $false
try {
    & $git -C $ProjectDir worktree add --detach $reproRoot $commit
    if ($LASTEXITCODE -ne 0) { throw 'Could not create isolated reproducibility worktree.' }
    $worktreeAdded = $true

    $project = Join-Path $reproRoot 'OmiGhost.vcxproj'
    Write-Host "[OmniGhost Repro] Independent build at $reproRoot"
    & $MSBuildPath $project /t:Build /m:1 /nr:false /p:Configuration=Publish /p:Platform=x64 `
        /p:OmniGhostReproducibilityPass=true /p:OmniGhostSkipReleaseSigning=true `
        /p:OmniGhostSkipPackaging=true /p:OmniGhostSkipRemotePublish=true `
        /p:OmniGhostSkipVersionAdvance=true /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Independent Publish rebuild failed with exit code $LASTEXITCODE." }

    $secondExecutable = Join-Path $reproRoot 'build\Publish\OmniGhost.exe'
    if (-not (Test-Path -LiteralPath $secondExecutable -PathType Leaf)) { throw 'Independent rebuild did not produce OmniGhost.exe.' }
    $primaryHash = (Get-FileHash -LiteralPath $PrimaryExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    $secondHash = (Get-FileHash -LiteralPath $secondExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($primaryHash -cne $secondHash) {
        throw "Independent rebuild differs byte-for-byte. primary=$primaryHash independent=$secondHash"
    }

    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    if ([string]$metadata.commit_id -cne $commit -or $metadata.dirty -eq $true -or
        $metadata.reproducibility_prerequisites_met -ne $true) {
        throw 'Build metadata does not match the clean independently rebuilt commit.'
    }
    $metadata.reproducible = $true
    $metadata | Add-Member -NotePropertyName reproducibility_sha256 -NotePropertyValue $primaryHash -Force
    $metadata | Add-Member -NotePropertyName reproducibility_verified_commit -NotePropertyValue $commit -Force
    $metadata | Add-Member -NotePropertyName reproducibility_verified_utc -NotePropertyValue ([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')) -Force
    $utf8 = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($metadataPath, (($metadata | ConvertTo-Json -Depth 8) + [Environment]::NewLine), $utf8)
    Write-Host "[OmniGhost Repro] PASS SHA-256=$primaryHash commit=$commit"
}
finally {
    if ($worktreeAdded) {
        & $git -C $ProjectDir worktree remove --force $reproRoot 2>$null
        & $git -C $ProjectDir worktree prune 2>$null
    }
}
