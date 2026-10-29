[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [Parameter(Mandatory = $false)]
    [string]$LocalAppDataRoot = $env:LOCALAPPDATA
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
        $script:failed.Add("${Label}: $($_.Exception.Message)")
    }
}

Write-Host '[OmniGhost Cleanup] Limpeza pré-publicação de caches e ficheiros temporários...'

# Apenas diretórios recriáveis. Os artefactos validados e build\Publish têm de
# permanecer disponíveis para o upload que ocorre imediatamente a seguir.
foreach ($relative in @('.cache', '.temp', 'logs')) {
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

if ($failed.Count -gt 0) {
    throw ("A limpeza pré-publicação não ficou completa:`n - " + ($failed -join "`n - "))
}

Write-Host "[OmniGhost Cleanup] Limpeza pré-publicação concluída ($removed alvo(s) removido(s))."

