[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$targets = @('.cache', 'artifacts', 'build')

foreach ($name in $targets) {
    $target = [IO.Path]::GetFullPath((Join-Path $ProjectDir $name))
    $parent = [IO.Path]::GetDirectoryName($target)
    if (-not $parent.Equals($ProjectDir, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($target) -ne $name) {
        throw "Recusada limpeza fora da raiz do projeto: $target"
    }
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host "[OmniGhost Build] Nada para limpar: $name"
        continue
    }
    Write-Host "[OmniGhost Build] A limpar ficheiros temporários: $name"
    Remove-Item -LiteralPath $target -Recurse -Force
}

Write-Host '[OmniGhost Build] Limpeza de workspace concluída.'
