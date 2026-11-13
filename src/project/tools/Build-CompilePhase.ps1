[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Start', 'Complete')][string]$Action,
    [Parameter(Mandatory = $true)][string]$StatePath,
    [int]$SourceCount = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'ReleaseOutput.ps1')
$StatePath = [IO.Path]::GetFullPath($StatePath)

if ($Action -eq 'Start') {
    [IO.Directory]::CreateDirectory((Split-Path -Parent $StatePath)) | Out-Null
    [IO.File]::WriteAllText($StatePath, [DateTime]::UtcNow.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture))
    Write-OmniPhase 4 "Compilação principal ($SourceCount unidades)"
    return
}

$elapsed = [TimeSpan]::Zero
if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
    $startTicks = [int64]::Parse([IO.File]::ReadAllText($StatePath), [Globalization.CultureInfo]::InvariantCulture)
    $elapsed = [DateTime]::UtcNow - [DateTime]::new($startTicks, [DateTimeKind]::Utc)
    Remove-Item -LiteralPath $StatePath -Force -ErrorAction SilentlyContinue
}
Write-Host "[Compile] Concluído em $(Format-OmniDuration $elapsed)."
