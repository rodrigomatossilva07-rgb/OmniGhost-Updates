[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [Parameter(Mandatory = $false)]
    [string]$LocalAppDataRoot = $env:LOCALAPPDATA,
    [switch]$DeferredCacheOnly,
    [int]$ParentProcessId = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$removed = 0
$failed = [Collections.Generic.List[string]]::new()

function Remove-SafeTree {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$AllowedParent,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullParent = [IO.Path]::GetFullPath($AllowedParent).TrimEnd('\')
    $prefix = $fullParent + '\'
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Limpeza recusada fora da área autorizada: $fullPath"
    }
    if (-not (Test-Path -LiteralPath $fullPath)) { return }

    try {
        Remove-Item -LiteralPath $fullPath -Recurse -Force -ErrorAction Stop
        $script:removed++
        Write-Host "[OmniGhost Cleanup] Removido: $Label"
    } catch {
        $script:failed.Add("${Label}: $($_.Exception.Message)")
    }
}

function Remove-SafeFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $projectPrefix = $ProjectDir.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($projectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Limpeza recusada fora da raiz do projeto: $fullPath"
    }

    $relative = $fullPath.Substring($projectPrefix.Length).Replace('/', '\')
    # The active MSBuild file logger lives under .cache and cannot be removed
    # by the Publish process that is still writing to it. The whole cache is
    # handled by the deferred pass after MSBuild exits.
    if ($relative.StartsWith('.cache\', [StringComparison]::OrdinalIgnoreCase)) {
        return
    }
    $protectedPrefixes = @(
        '.git\',
        'artifacts\release\',
        'src\project\third_party\',
        'src\project\libs\'
    )
    foreach ($prefix in $protectedPrefixes) {
        if ($relative.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            return
        }
    }

    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { return }
    try {
        Remove-Item -LiteralPath $fullPath -Force -ErrorAction Stop
        $script:removed++
        Write-Verbose "[OmniGhost Cleanup] Removido: $Label"
    } catch {
        if ($relative.StartsWith('.cache\', [StringComparison]::OrdinalIgnoreCase)) {
            Write-Verbose "[OmniGhost Cleanup] Adiado até ao fim do MSBuild: $Label"
            return
        }
        $script:failed.Add("${Label}: $($_.Exception.Message)")
    }
}

if ($DeferredCacheOnly) {
    # The MSBuild file logger can keep OmiGhost.log open for a few seconds
    # after the Publish PowerShell process exits.  Retry only the project
    # cache after the parent has gone away; a locked diagnostic log must not
    # turn an already published release into a failed build.
    if ($ParentProcessId -gt 0) {
        try { Wait-Process -Id $ParentProcessId -Timeout 300 -ErrorAction SilentlyContinue } catch {}
    }

    $cachePath = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.cache'))
    $expectedCachePath = $ProjectDir.TrimEnd('\') + '\.cache'
    if (-not $cachePath.Equals($expectedCachePath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Limpeza diferida recusada fora da cache esperada: $cachePath"
    }

    for ($attempt = 1; $attempt -le 24; ++$attempt) {
        if (-not (Test-Path -LiteralPath $cachePath)) { exit 0 }
        Start-Sleep -Seconds 5
        try {
            Remove-Item -LiteralPath $cachePath -Recurse -Force -ErrorAction Stop
            exit 0
        }
        catch {
            if ($attempt -eq 24) {
                Write-Warning "A cache continuou bloqueada após as tentativas diferidas: $($_.Exception.Message)"
            }
        }
    }
    exit 0
}

Write-Host '[OmniGhost Cleanup] Limpeza pós-publicação de caches e ficheiros temporários...'

# Apenas diretórios recriáveis. Os artefactos validados e build\Publish têm de
# permanecer disponíveis para o upload que ocorre imediatamente a seguir.
foreach ($relative in @('.temp', 'logs')) {
    Remove-SafeTree `
        -Path (Join-Path $ProjectDir $relative) `
        -AllowedParent $ProjectDir `
        -Label $relative
}

# Outputs integralmente regeneráveis de ferramentas e componentes auxiliares.
$generatedTrees = @(
    'src\games\Cs2\dumper\target',
    'src\project\tools\FiveMCollisionBuilder\build'
)
foreach ($relative in $generatedTrees) {
    Remove-SafeTree `
        -Path (Join-Path $ProjectDir $relative) `
        -AllowedParent $ProjectDir `
        -Label $relative
}

# bin/obj de componentes próprios. Dependências em third_party e libs são
# deliberadamente excluídas porque podem conter binários de origem obrigatórios.
$firstPartyBuildRoots = @(
    (Join-Path $ProjectDir 'src\project\support'),
    (Join-Path $ProjectDir 'src\games')
)
foreach ($buildRoot in $firstPartyBuildRoots) {
    if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) { continue }
    $buildDirectories = @(Get-ChildItem -LiteralPath $buildRoot -Recurse -Directory -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -in @('bin', 'obj', '__pycache__', '.pytest_cache', '.mypy_cache', '.ruff_cache') } |
        Sort-Object { $_.FullName.Length } -Descending)
    foreach ($directory in $buildDirectories) {
        if (-not (Test-Path -LiteralPath $directory.FullName -PathType Container)) { continue }
        Remove-SafeTree -Path $directory.FullName -AllowedParent $ProjectDir -Label $directory.FullName.Substring($ProjectDir.Length + 1)
    }
}

# Cache de compressão criada pelo gerador de recursos fora do repositório.
if (-not [string]::IsNullOrWhiteSpace($LocalAppDataRoot)) {
    $omniLocalRoot = [IO.Path]::GetFullPath((Join-Path $LocalAppDataRoot 'OmniGhost'))
    $reproRoot = [IO.Path]::GetFullPath((Join-Path $omniLocalRoot 'build-cache\repro-worktrees')).TrimEnd('\')

    # Unregister any residual reproducibility checkout before deleting its
    # directory. Removing only the folder leaves stale Git worktree metadata
    # and Visual Studio may continue evaluating a project that no longer has
    # its imported props files.
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($null -ne $git -and (Test-Path -LiteralPath (Join-Path $ProjectDir '.git'))) {
        $worktreeLines = @(& $git.Source -C $ProjectDir worktree list --porcelain 2>$null)
        if ($LASTEXITCODE -eq 0) {
            foreach ($line in $worktreeLines) {
                if ($line -notmatch '^worktree\s+(.+)$') { continue }
                $candidate = [IO.Path]::GetFullPath($Matches[1].Trim()).TrimEnd('\')
                if (-not $candidate.StartsWith($reproRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { continue }

                & $git.Source -C $ProjectDir worktree remove --force $candidate 2>$null | Out-Null
                if ($LASTEXITCODE -ne 0 -and (Test-Path -LiteralPath $candidate)) {
                    $failed.Add("Git worktree $candidate não pôde ser removido corretamente")
                }
            }
            & $git.Source -C $ProjectDir worktree prune --expire now 2>$null | Out-Null
        }
    }

    Remove-SafeTree `
        -Path (Join-Path $omniLocalRoot 'build-cache') `
        -AllowedParent $omniLocalRoot `
        -Label '%LOCALAPPDATA%\OmniGhost\build-cache'
}

# Resíduos de compilação e ficheiros temporários perdidos em qualquer parte do
# projeto. A pesquisa ignora Git, dependências binárias e a release validada.
$temporaryExtensions = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
@('.obj', '.pdb', '.ilk', '.ipdb', '.iobj', '.tlog', '.lastbuildstate',
  '.tmp', '.temp', '.bak', '.old', '.log', '.pch', '.idb', '.exp', '.res',
  '.aps', '.sbr', '.bsc', '.ncb', '.sdf', '.opensdf') |
    ForEach-Object { [void]$temporaryExtensions.Add($_) }

$projectPrefix = $ProjectDir.TrimEnd('\') + '\'
$protectedPrefixes = @(
    (Join-Path $ProjectDir '.git').TrimEnd('\') + '\',
    (Join-Path $ProjectDir '.cache').TrimEnd('\') + '\',
    (Join-Path $ProjectDir 'artifacts\release').TrimEnd('\') + '\',
    (Join-Path $ProjectDir 'src\project\third_party').TrimEnd('\') + '\',
    (Join-Path $ProjectDir 'src\project\libs').TrimEnd('\') + '\'
)
Get-ChildItem -LiteralPath $ProjectDir -Recurse -File -Force -ErrorAction SilentlyContinue |
    Where-Object {
        $candidate = $_.FullName
        $isProtected = $false
        foreach ($prefix in $protectedPrefixes) {
            if ($candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                $isProtected = $true
                break
            }
        }
        if ($isProtected) {
            $false
        } else {
            $temporaryExtensions.Contains($_.Extension) -or $_.Name.EndsWith('~', [StringComparison]::Ordinal)
        }
    } |
    ForEach-Object {
        Remove-SafeFile -Path $_.FullName -Label $_.FullName.Substring($projectPrefix.Length)
    }

$deferredPowerShell = Get-Command pwsh.exe -ErrorAction SilentlyContinue
if ($null -eq $deferredPowerShell) {
    $deferredPowerShell = Get-Command (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -ErrorAction Stop
}
$deferredArguments = @(
    '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
    '-File', ('"{0}"' -f $PSCommandPath),
    '-ProjectDir', ('"{0}"' -f $ProjectDir),
    '-DeferredCacheOnly',
    '-ParentProcessId', [string]$PID
)
Start-Process -FilePath $deferredPowerShell.Source -ArgumentList $deferredArguments -WindowStyle Hidden | Out-Null

if ($failed.Count -gt 0) {
    throw ("A limpeza pós-publicação não ficou completa:`n - " + ($failed -join "`n - "))
}

Write-Host "[OmniGhost Cleanup] Limpeza imediata concluída ($removed alvo(s)); .cache será removida após o MSBuild libertar o log."

