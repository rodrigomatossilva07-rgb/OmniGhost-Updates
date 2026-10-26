[CmdletBinding()]
param(
    [string]$CollisionFolder = (Join-Path $PSScriptRoot '..\..\data\fivem\collision'),
    [switch]$Undo
)

$ErrorActionPreference = 'Stop'
$folder = [IO.Path]::GetFullPath($CollisionFolder)
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$expectedRoot = [IO.Path]::GetFullPath((Join-Path $workspace 'data\fivem'))
if (-not $folder.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "A pasta tem de estar dentro de '$expectedRoot'."
}
if (-not (Test-Path -LiteralPath (Join-Path $folder 'manifest.json') -PathType Leaf)) {
    throw "manifest.json não encontrado em '$folder'."
}

$regions = @(Get-ChildItem -LiteralPath $folder -Filter '*.ogcol' -File | Sort-Object Name)
if ($regions.Count -eq 0) { throw 'Nenhuma região .ogcol encontrada.' }

$validMagic = [Text.Encoding]::ASCII.GetBytes("OGCOL1`0`0")
foreach ($region in $regions) {
    $stream = [IO.File]::Open($region.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 60) { throw "Região truncada: $($region.Name)" }
        $magic = [byte[]]::new(8)
        if ($stream.Read($magic, 0, 8) -ne 8) { throw "Não foi possível validar $($region.Name)" }
        for ($i = 0; $i -lt 8; $i++) {
            if ($magic[$i] -ne $validMagic[$i]) { throw "Assinatura inválida: $($region.Name)" }
        }
    } finally { $stream.Dispose() }
}

$logicalBytes = [uint64](($regions | Measure-Object Length -Sum).Sum)
$compact = Join-Path $env:SystemRoot 'System32\compact.exe'
if (-not (Test-Path -LiteralPath $compact -PathType Leaf)) { throw 'compact.exe não encontrado.' }

Write-Host "[FiveM Collision] $($regions.Count) regiões validadas; $([math]::Round($logicalBytes / 1GB, 2)) GiB lógicos."
if ($Undo) {
    & $compact /u /s:"$folder" /i /q
    if ($LASTEXITCODE -ne 0) { throw "compact.exe terminou com código $LASTEXITCODE" }
    Write-Host '[FiveM Collision] Compressão NTFS removida.'
} else {
    # LZX dá a melhor relação do CompactOS e continua a permitir leitura aleatória transparente.
    & $compact /c /s:"$folder" /i /f /q /exe:lzx
    if ($LASTEXITCODE -ne 0) { throw "compact.exe terminou com código $LASTEXITCODE" }
    Write-Host '[FiveM Collision] Compressão LZX aplicada. O formato .ogcol não foi alterado.'
}

# Confirma que todos os ficheiros continuam legíveis depois da operação.
foreach ($region in $regions) {
    $stream = [IO.File]::Open($region.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $magic = [byte[]]::new(8)
        if ($stream.Read($magic, 0, 8) -ne 8) { throw "Leitura pós-otimização falhou: $($region.Name)" }
    } finally { $stream.Dispose() }
}
Write-Host '[FiveM Collision] Verificação pós-otimização concluída.'
