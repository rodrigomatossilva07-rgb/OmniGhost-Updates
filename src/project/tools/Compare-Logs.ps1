[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Left,
    [Parameter(Mandatory=$true)][string]$Right,
    [int]$Context = 4
)
$ErrorActionPreference = "Stop"

function Normalize-Line([string]$Line) {
    if ($null -eq $Line) { return "" }
    $value = $Line -replace '^\[[0-9]{4}-[0-9]{2}-[0-9]{2}[^\]]*\]\s*',''
    $value = $value -replace '0x[0-9a-fA-F]+','0x<ADDR>'
    $value = $value -replace '\bPID=\d+\b','PID=<PID>'
    $value = $value -replace '\belapsed=\d+ms\b','elapsed=<MS>'
    return $value.TrimEnd()
}

$leftLines = @(Get-Content -LiteralPath $Left)
$rightLines = @(Get-Content -LiteralPath $Right)
$max = [Math]::Max($leftLines.Count, $rightLines.Count)
$first = -1
for ($i = 0; $i -lt $max; $i++) {
    $a = if ($i -lt $leftLines.Count) { Normalize-Line $leftLines[$i] } else { "<EOF>" }
    $b = if ($i -lt $rightLines.Count) { Normalize-Line $rightLines[$i] } else { "<EOF>" }
    if ($a -ne $b) { $first = $i; break }
}
if ($first -lt 0) {
    Write-Host "No normalized divergence found."
    exit 0
}

Write-Host "First normalized divergence at line $($first + 1)" -ForegroundColor Yellow
$start = [Math]::Max(0, $first - $Context)
$end = [Math]::Min($max - 1, $first + $Context)
for ($i = $start; $i -le $end; $i++) {
    $a = if ($i -lt $leftLines.Count) { Normalize-Line $leftLines[$i] } else { "<EOF>" }
    $b = if ($i -lt $rightLines.Count) { Normalize-Line $rightLines[$i] } else { "<EOF>" }
    $mark = if ($i -eq $first) { ">>" } else { "  " }
    Write-Host ("{0} L{1}: {2}" -f $mark, ($i + 1), $a)
    Write-Host ("{0} R{1}: {2}" -f $mark, ($i + 1), $b)
}
