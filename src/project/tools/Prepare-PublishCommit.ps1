[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$git = Get-Command git -ErrorAction Stop

& $git.Source -C $ProjectDir rev-parse --is-inside-work-tree | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Publish requer um repositório Git local para criar o commit de release.'
}

$conflicts = @(& $git.Source -C $ProjectDir diff --name-only --diff-filter=U)
if ($LASTEXITCODE -ne 0) { throw 'Não foi possível verificar conflitos Git.' }
if ($conflicts.Count -gt 0) {
    throw ("Publish bloqueado: resolve primeiro os conflitos Git:`n" + ($conflicts -join "`n"))
}

$status = @(& $git.Source -C $ProjectDir status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Não foi possível obter o estado Git.' }
if ($status.Count -eq 0) {
    Write-Host '[OmniGhost Publish] Árvore Git já limpa; não é necessário criar commit.'
    exit 0
}

$versionPath = Join-Path $ProjectDir 'src\project\config\release\version.txt'
if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    throw "Versão de release não encontrada: $versionPath"
}
$version = ([IO.File]::ReadAllText($versionPath)).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.\d+\.\d+$') {
    throw "Versão de release inválida: '$version'."
}

Write-Host "[OmniGhost Publish] A preparar commit automático para v$version..."
& $git.Source -C $ProjectDir add -A
if ($LASTEXITCODE -ne 0) { throw 'Não foi possível preparar as alterações Git para o commit.' }

& $git.Source -C $ProjectDir diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host '[OmniGhost Publish] Não existem alterações staged para commit.'
    exit 0
}
if ($LASTEXITCODE -ne 1) { throw 'Não foi possível validar as alterações staged.' }

& $git.Source -C $ProjectDir commit -m "chore(release): prepare v$version"
if ($LASTEXITCODE -ne 0) {
    throw 'O commit automático falhou. Confirma a identidade Git (user.name e user.email) e tenta novamente.'
}

Write-Host "[OmniGhost Publish] Commit automático criado para v$version."
