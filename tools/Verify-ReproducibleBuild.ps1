[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$PrimaryExecutable,
    [Parameter(Mandatory=$true)][string]$MSBuildPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'ReleaseOutput.ps1')
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$PrimaryExecutable = [IO.Path]::GetFullPath($PrimaryExecutable)
$MSBuildPath = [IO.Path]::GetFullPath($MSBuildPath)
$metadataPath = Join-Path $ProjectDir '.cache\generated\Publish\x64\build-metadata.json'

function Get-Sha256([string]$LiteralPath) {
    $stream = [IO.File]::OpenRead($LiteralPath)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()
    } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $PrimaryExecutable -PathType Leaf)) { throw "Primary unsigned executable missing: $PrimaryExecutable" }
if (-not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) { throw "MSBuild missing: $MSBuildPath" }
if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) { throw "Publish metadata missing: $metadataPath" }

$git = (Get-Command git -ErrorAction Stop).Source
$status = @(& $git -C $ProjectDir status --porcelain --untracked-files=normal --ignore-submodules)
if ($LASTEXITCODE -ne 0) { throw 'Independent rebuild could not inspect the Git worktree.' }

# Documentation is explicitly excluded from the compiler and release payload.
# Editing/removing it while Visual Studio is open cannot alter the executable,
# so it must not invalidate a byte-for-byte rebuild. Every source, config,
# resource, dependency or unknown path remains fail-closed.
$materialChanges = New-Object System.Collections.Generic.List[string]
foreach ($entry in $status) {
    $line = [string]$entry
    $path = if ($line.Length -gt 3) { $line.Substring(3).Trim() } else { '' }
    if ($path -match ' -> ') { $path = ($path -split ' -> ')[-1].Trim() }
    $normalized = $path.Replace('\', '/')
    $documentationOnly = $normalized -match '^(docs/.*\.(md|markdown)|[^/]+\.(md|markdown))$'
    if (-not $documentationOnly) { $materialChanges.Add($line) }
}
if ($materialChanges.Count -gt 0) {
    throw ("Independent rebuild requires all material Git files to be clean:`n        " +
        ($materialChanges -join "`n        "))
}
if ($status.Count -gt 0) {
    Write-Host '[OmniGhost Repro] Alterações apenas em documentação não compilada; comparação binária autorizada.'
}
$commit = (& $git -C $ProjectDir rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'Could not resolve HEAD for independent rebuild.' }

# MSBuild warns when IntDir/OutDir live below the Windows temporary directory
# (MSB8029). Keep the disposable worktree beside the project instead, while
# still using a unique detached checkout for the byte-for-byte verification.
$reproParent = Split-Path -Parent $ProjectDir
$reproRoot = Join-Path $reproParent ('.omnighost-repro-' + [Guid]::NewGuid().ToString('N'))
$worktreeAdded = $false
$reproTimer = [Diagnostics.Stopwatch]::StartNew()
$reproLog = Join-Path $ProjectDir '.cache\logs\repro-build.log'
try {
    Write-Host '[Repro 1/3] A preparar checkout isolado...'
    & $git -C $ProjectDir worktree add --quiet --detach $reproRoot $commit
    if ($LASTEXITCODE -ne 0) { throw 'Could not create isolated reproducibility worktree.' }
    $worktreeAdded = $true

    $project = Join-Path $reproRoot 'OmiGhost.vcxproj'
    Write-Host '[Repro 2/3] A executar build independente (detalhes em .cache\logs\repro-build.log)...'
    [IO.Directory]::CreateDirectory((Split-Path -Parent $reproLog)) | Out-Null
    $buildOutput = @(& $MSBuildPath $project /t:Build /m:1 /nr:false /p:Configuration=Publish /p:Platform=x64 `
        /p:OmniGhostReproducibilityPass=true /p:OmniGhostSkipReleaseSigning=true `
        /p:OmniGhostSkipPackaging=true /p:OmniGhostSkipRemotePublish=true `
        /p:OmniGhostSkipVersionAdvance=true /v:minimal 2>&1)
    $buildExitCode = $LASTEXITCODE
    [IO.File]::WriteAllLines($reproLog, @($buildOutput | ForEach-Object { [string]$_ }), (New-Object Text.UTF8Encoding($false)))
    if ($buildExitCode -ne 0) {
        Write-Host '[OmniGhost Repro] Ultimas linhas da build independente:'
        $buildOutput | Select-Object -Last 80 | ForEach-Object { Write-Host "  $_" }
        throw "Independent Publish rebuild failed with exit code $buildExitCode. Full log: $reproLog"
    }

    $secondExecutable = Join-Path $reproRoot 'build\Publish\OmniGhost.exe'
    if (-not (Test-Path -LiteralPath $secondExecutable -PathType Leaf)) { throw 'Independent rebuild did not produce OmniGhost.exe.' }
    $primaryHash = Get-Sha256 -LiteralPath $PrimaryExecutable
    $secondHash = Get-Sha256 -LiteralPath $secondExecutable
    if ($primaryHash -cne $secondHash) {
        $diagnosticsDir = Join-Path $ProjectDir '.cache\repro-diagnostics'
        [IO.Directory]::CreateDirectory($diagnosticsDir) | Out-Null
        Copy-Item -LiteralPath $PrimaryExecutable -Destination (Join-Path $diagnosticsDir 'primary.exe') -Force
        Copy-Item -LiteralPath $secondExecutable -Destination (Join-Path $diagnosticsDir 'independent.exe') -Force
        throw "Independent rebuild differs byte-for-byte. primary=$primaryHash independent=$secondHash diagnostics=$diagnosticsDir"
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
    Write-Host "[Repro 3/3] Executaveis identicos - PASS ($(Format-OmniDuration $reproTimer.Elapsed)); SHA-256=$primaryHash"
}
finally {
    if ($worktreeAdded) {
        & $git -C $ProjectDir worktree remove --force $reproRoot 2>$null | Out-Null
        & $git -C $ProjectDir worktree prune 2>$null | Out-Null
    }
}
