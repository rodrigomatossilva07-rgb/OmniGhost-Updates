[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [string]$ReportPath = 'artifacts\validation\source-complexity.json'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$ceilings = [ordered]@{
    'src/launcher/game_select.cpp' = 2836
    'src/launcher/updates_page.cpp' = 2050
    'DMALibrary/Memory/Memory.cpp' = 2404
    'src/games/Fivem/esp/esp.cpp' = 2495
    'src/games/Cs2/cs2_game.cpp' = 2200
}
$rows = @()
foreach ($relative in $ceilings.Keys) {
    $path = Join-Path $ProjectDir ($relative.Replace('/','\'))
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Complexity input missing: $relative" }
    $lines = ([IO.File]::ReadLines($path) | Measure-Object).Count
    $ceiling = [int]$ceilings[$relative]
    $rows += [ordered]@{ file=$relative; lines=$lines; ceiling=$ceiling; status=if($lines -le $ceiling){'PASS'}else{'FAIL'} }
    if ($lines -gt $ceiling) { throw "$relative grew to $lines lines; ceiling is $ceiling. Extract a focused module first." }
}
$output = [IO.Path]::GetFullPath((Join-Path $ProjectDir $ReportPath))
New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null
[ordered]@{ schemaVersion=1; files=$rows } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $output -Encoding UTF8
Write-Host "[PASS] Large-source ceilings validated. updates_page.cpp is now $((($rows | Where-Object file -eq 'src/launcher/updates_page.cpp').lines)) lines."
