





























[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('cs2','fivem','fortnite','warzone','apex','rust','all')]
    [string]$Game = 'all',

    [Parameter(Mandatory=$false)]
    [switch]$Force,

    [Parameter(Mandatory=$false)]
    [switch]$DryRun,

    [Parameter(Mandatory=$false)]
    [switch]$ShowVerbose
)

$ErrorActionPreference = 'Stop'




$OFFSET_SOURCES = @{
    'cs2' = @{
        Url = 'https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/offsets.json'
        Module = 'client.dll'
        Game = 'cs2'
    }
    'fivem' = @{
        Url = 'https://raw.githubusercontent.com/fivem-offset-dumper/main/offsets.json'
        Module = 'GTAProcess.exe'
        Game = 'fivem'
    }
    'fortnite' = @{
        Url = 'https://raw.githubusercontent.com/fortnite-offset-dumper/main/offsets.json'
        Module = 'FortniteClient-Win64-Shipping.exe'
        Game = 'fortnite'
    }
    'warzone' = @{
        Url = 'https://raw.githubusercontent.com/warzone-offset-dumper/main/offsets.json'
        Module = 'cod.exe'
        Game = 'warzone'
    }
    'apex' = @{
        Url = 'https://raw.githubusercontent.com/a2x/apex-dumper/main/output/offsets.json'
        Module = 'r5apex.exe'
        Game = 'apex'
    }
    'rust' = @{
        Url = 'https://rustoffsets.com/api/offsets.json'
        Module = 'GameAssembly.dll'
        Game = 'rust'
    }
}

$SCHEMA_VERSION = 7
$DATA_DIR = "src/project/data"




function Write-Log {
    param(
        [string]$Message,
        [ValidateSet('Info','Warn','Error','Pass','Fail','Debug')]
        [string]$Level = 'Info'
    )
    $prefix = switch ($Level) {
        'Info'  { '[INFO]' }
        'Warn'  { '[WARN]' }
        'Error' { '[ERROR]' }
        'Pass'  { '[PASS]' }
        'Fail'  { '[FAIL]' }
        'Debug' { '[DEBUG]' }
    }
    Write-Host "${prefix} ${Message}"
}

function Read-JsonFile {
    param([string]$FilePath)
    try {
        $bytes = [System.IO.File]::ReadAllBytes($FilePath)
        if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
            $content = [System.Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3)
        } else {
            $content = [System.Text.Encoding]::UTF8.GetString($bytes)
        }
        return $content | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "Failed to parse JSON: $($_.Exception.Message)"
    }
}

function Write-JsonFile {
    param([string]$FilePath, [object]$Data)
    $json = $Data | ConvertTo-Json -Depth 10
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($FilePath, $json, $encoding)
}

function Fetch-UpstreamOffsets {
    param([hashtable]$Source)
    try {
        $response = Invoke-WebRequest -Uri $Source.Url -TimeoutSec 30 -UseBasicParsing
        if ($response.StatusCode -eq 200) {
            $content = $response.Content
            if ($content.StartsWith([char]0xFEFF)) { $content = $content.Substring(1) }
            return $content | ConvertFrom-Json -ErrorAction Stop
        } else {
            Write-Log "HTTP $($response.StatusCode) for $($Source.Game)" -Level Warn
            return $null
        }
    } catch {
        Write-Log "Failed to fetch $($Source.Game): $($_.Exception.Message)" -Level Error
        return $null
    }
}

function Merge-Offsets {
    param($Local, $Upstream, [string]$Game)



    return $Local
}




Write-Log "OmniGhost Offset Synchronization"
Write-Log "Target game(s): ${Game}"
Write-Log "Force update: ${Force}"
Write-Log "Dry run: ${DryRun}"

$gamesToSync = if ($Game -eq 'all') { $OFFSET_SOURCES.Keys } else { @($Game) }
$changed = $false
$results = @{}

foreach ($gameName in $gamesToSync) {
    if (-not $OFFSET_SOURCES.ContainsKey($gameName)) {
        Write-Log "Unknown game: ${gameName}" -Level Warn
        continue
    }

    $source = $OFFSET_SOURCES[$gameName]
    $localFile = Join-Path $DATA_DIR "${gameName}_offsets.json"

    Write-Log "`nProcessing: ${gameName}"


    Write-Log "  Fetching from ${source.Url}..."
    $upstream = Fetch-UpstreamOffsets -Source $source
    if (-not $upstream) {
        Write-Log "  SKIP: Failed to fetch upstream" -Level Warn
        $results[$gameName] = @{ status = 'fetch_failed' }
        continue
    }


    if (-not (Test-Path $localFile)) {
        Write-Log "  Local file not found: ${localFile}" -Level Warn
        $results[$gameName] = @{ status = 'local_missing' }
        continue
    }

    $local = Read-JsonFile -FilePath $localFile


    $localBuild = $local.build ?? $local.version ?? 'unknown'
    $upstreamBuild = $upstream.build ?? $upstream.version ?? $upstream.build_id ?? 'unknown'

    Write-Log "  Local build: ${localBuild}"
    Write-Log "  Upstream build: ${upstreamBuild}"

    $needsUpdate = $Force -or ($localBuild -ne $upstreamBuild)

    if (-not $needsUpdate) {
        Write-Log "  SKIP: Already at latest build" -Level Info
        $results[$gameName] = @{ status = 'current', local_build = $localBuild }
        continue
    }

    Write-Log "  UPDATE available: ${localBuild} -> ${upstreamBuild}"

    if ($DryRun) {
        Write-Log "  DRY RUN: Would update ${gameName}" -Level Info
        $results[$gameName] = @{ status = 'would_update', from = $localBuild, to = $upstreamBuild }
        $changed = $true
        continue
    }



    Write-Log "  MANUAL MERGE REQUIRED: Review upstream changes and merge into ${localFile}" -Level Warn
    $results[$gameName] = @{ status = 'manual_merge_required', from = $localBuild, to = $upstreamBuild }
    $changed = $true
}


Write-Log "`n============================================================"
Write-Log "SYNC SUMMARY"
Write-Log "============================================================"
foreach ($gameName in $results.Keys) {
    $r = $results[$gameName]
    switch ($r.status) {
        'current'       { Write-Log "  ${gameName}: CURRENT (build ${r.local_build})" -Level Pass }
        'would_update'  { Write-Log "  ${gameName}: WOULD UPDATE (${r.from} -> ${r.to})" -Level Info }
        'manual_merge_required' { Write-Log "  ${gameName}: MANUAL MERGE REQUIRED (${r.from} -> ${r.to})" -Level Warn }
        'fetch_failed'  { Write-Log "  ${gameName}: FETCH FAILED" -Level Fail }
        'local_missing' { Write-Log "  ${gameName}: LOCAL FILE MISSING" -Level Fail }
        default         { Write-Log "  ${gameName}: ${r.status}" -Level Info }
    }
}


$anyUpdates = $results.Values | Where-Object { $_.status -in @('would_update', 'manual_merge_required') }
if ($anyUpdates) {
    Write-Log "CHANGES DETECTED" -Level Info
    exit 0
} else {
    Write-Log "NO CHANGES" -Level Pass
    exit 0
}