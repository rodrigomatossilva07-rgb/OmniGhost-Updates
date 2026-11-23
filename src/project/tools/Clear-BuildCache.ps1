[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [ValidateSet('All', 'Configuration')]
    [string]$Scope = 'All',

    [string]$Configuration = '',

    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = [IO.Path]::GetFullPath($ProjectDir)
$cacheRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot '.cache'))
$target = $cacheRoot

if ($Scope -eq 'Configuration') {
    if ([string]::IsNullOrWhiteSpace($Configuration) -or
        $Configuration -notmatch '^(Debug|Release|Development|Publish)$') {
        throw 'Uma configuracao CMake valida e obrigatoria para limpeza parcial.'
    }
    $target = [IO.Path]::GetFullPath((Join-Path $cacheRoot (Join-Path 'cmake' $Configuration)))
}

$cachePrefix = $cacheRoot.TrimEnd('\') + '\'
if (-not $target.StartsWith($cachePrefix, [StringComparison]::OrdinalIgnoreCase) -and
    -not $target.Equals($cacheRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destino de limpeza inválido: $target"
}

if (-not (Test-Path -LiteralPath $target)) {
    Write-Host "[OK] Nao existe cache para limpar: $target"
    exit 0
}

$locksDirectory = Join-Path $cacheRoot 'locks'
$locks = @()
if (Test-Path -LiteralPath $locksDirectory -PathType Container) {
    $locks = @(Get-ChildItem -LiteralPath $locksDirectory -Directory -Filter 'build-*.lock' -ErrorAction SilentlyContinue)
}
if ($Scope -eq 'All' -and $locks.Count -gt 0 -and -not $Force) {
    $names = $locks.Name -join ', '
    throw "Existe uma compilacao possivelmente ativa ($names). Fecha-a ou executa 'build.bat clean force' apenas se tiver ficado bloqueada."
}

for ($attempt = 1; $attempt -le 5; ++$attempt) {
    try {
        # Antivírus e ferramentas de indexação podem deixar atributos de só
        # leitura nos .tlog/.obj. Normaliza-los antes de remover reduz falhas
        # de limpeza depois de builds interrompidas.
        Get-ChildItem -LiteralPath $target -Force -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object { $_.Attributes = [IO.FileAttributes]::Normal }
        (Get-Item -LiteralPath $target -Force).Attributes = [IO.FileAttributes]::Normal
        Remove-Item -LiteralPath $target -Force -Recurse -ErrorAction Stop
        if (-not (Test-Path -LiteralPath $target)) {
            Write-Host "[OK] Cache removida: $target"
            exit 0
        }
    }
    catch {
        if ($attempt -eq 5) {
            throw "Nao foi possivel remover '$target' apos 5 tentativas. Fecha Visual Studio, Explorer aberto nessa pasta e qualquer build em execucao. Detalhe: $($_.Exception.Message)"
        }
        Start-Sleep -Milliseconds (350 * $attempt)
    }
}

throw "A limpeza nao foi concluida: $target"
