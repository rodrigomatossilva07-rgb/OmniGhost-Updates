[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('A','B','C','D')][string]$Case,
    [Parameter(Mandatory=$false)][string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [string]$LogPath = "$env:LOCALAPPDATA\OmniGhost\logs\logs.txt"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) { throw "logs.txt not found: $LogPath" }
$text = Get-Content -LiteralPath $LogPath -Raw
$isMock = $text -match 'mode=MOCK'
function Status([bool]$Condition) { if ($isMock) { 'NOT_TESTED' } elseif ($Condition) { 'PASS' } else { 'NOT_TESTED' } }
$pdbCalls = ([regex]::Matches($text, '\[VMM\].*api=VMMDLL_Pdb[^\r\n]*executed=YES')).Count
$record = [ordered]@{
    schemaVersion = 1
    case = $Case
    collectedUtc = [DateTime]::UtcNow.ToString('o')
    sourceLog = [IO.Path]::GetFullPath($LogPath)
    mockHardwareDetected = $isMock
    makcu = Status ($text -match '\[STARTUP\]\[Hardware\] makcu=AVAILABLE(?!.*MOCK)')
    dmaPnp = Status ($text -match '\[DMA\]\[StartupProbe\].*result=READY')
    ftd3xxwu = Status ($text -match 'FTD3XXWU\.dll => READY')
    leechcore = Status ($text -match 'leechcore\.dll => OK|static_leechcore=PASS')
    vmmInitialize = Status ($text -match '\[DMA\]\[Init\].*result=OK')
    vmmVersion = Status ($text -match '\[VMM\] version=')
    pidDiscovery = Status ($text -match '\[DMA\]\[Bind\] pid lookup done pid=[1-9]')
    readOnlySanity = Status ($text -match 'attach validate MZ.*result=OK|soft-probe lobby OK')
    adapterInitialization = Status ($text -match 'transition=.*Running|attach.*PASS|Backend: Ready')
    cleanShutdown = Status ($text -match 'clean shutdown requested|OmniGhost terminado')
    pdbCallsExecuted = $pdbCalls
    infoDbDisabledObserved = $text -match 'info\.db => SKIPPED \(-disable-infodb\)'
    symbolsDisabledObserved = $text -match 'pdbcrust=DISABLED_BY_BUILD|pdbcrust\.dll.*MISSING \(optional\)'
}
$outputDir = Join-Path $ProjectDir 'artifacts\hardware-validation'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$copy = Join-Path $outputDir "$Case-logs.txt"
Copy-Item -LiteralPath $LogPath -Destination $copy -Force
$json = Join-Path $outputDir "$Case-result.json"
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $json -Encoding UTF8
Write-Host "[HardwareMatrix] case=$Case makcu=$($record.makcu) vmm=$($record.vmmInitialize) pid=$($record.pidDiscovery) read=$($record.readOnlySanity) adapter=$($record.adapterInitialization) shutdown=$($record.cleanShutdown) pdb_calls=$pdbCalls"
Write-Host "[HardwareMatrix] evidence=$json log_copy=$copy"
