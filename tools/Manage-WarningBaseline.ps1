[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$false)][switch]$GenerateBaseline,
    [Parameter(Mandatory=$false)][switch]$CheckNewWarnings,
    [Parameter(Mandatory=$false)][string]$OutputPath = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$baselineDir = Join-Path $ProjectDir '.cache\warnings'
$baselineFile = Join-Path $baselineDir 'baseline.txt'
$msbuildLog = Join-Path $baselineDir 'build.log'

New-Item -ItemType Directory -Path $baselineDir -Force | Out-Null

function Get-CompilerWarnings {
    param([string]$ProjectFile)
    
    $tempLog = [IO.Path]::GetTempFileName()
    try {
        $result = msbuild $ProjectFile `
            /p:Configuration=Release `
            /p:Platform=x64 `
            /p:OmniGhostNewWarningsAsErrors=true `
            /p:OmniGhostNewWarningsAsErrorsBaseline=$baselineFile `
            /flp:LogFile=$tempLog;Verbosity=Normal `
            /v:q /nologo /maxcpucount 2>&1
    } finally {
        if (Test-Path $tempLog) { Remove-Item $tempLog -Force }
    }
    
    if (-not (Test-Path $tempLog)) { return @() }
    
    $warnings = @()
    Get-Content $tempLog -Raw | ForEach-Object {
        if ($_ -match 'warning\s+([A-Z]\d+):\s*(.+)') {
            $code = $matches[1]
            $msg = $matches[2].Trim()
            $warnings += "$code: $msg"
        }
    }
    return $warnings | Sort-Object -Unique
}

if ($GenerateBaseline) {
    Write-Host "[WarningBaseline] Generating warning baseline..."
    $warnings = Get-CompilerWarnings (Join-Path $ProjectDir 'OmiGhost.vcxproj')
    [IO.File]::WriteAllLines($baselineFile, $warnings)
    Write-Host "[WarningBaseline] Baseline generated with $($warnings.Count) known warnings"
    exit 0
}

if ($CheckNewWarnings) {
    Write-Host "[WarningBaseline] Checking for new warnings..."
    if (-not (Test-Path $baselineFile)) {
        Write-Warning "No baseline file found. Run with -GenerateBaseline first."
        exit 1
    }
    
    $baseline = Get-Content $baselineFile | Sort-Object -Unique
    $current = Get-CompilerWarnings (Join-Path $ProjectDir 'OmiGhost.vcxproj')
    
    $newWarnings = $current | Where-Object { $_ -notin $baseline }
    
    if ($newWarnings.Count -gt 0) {
        Write-Error "[FAIL] Found $($newWarnings.Count) new warning(s):"
        $newWarnings | ForEach-Object { Write-Host "  $_" }
        exit 1
    } else {
        Write-Host "[PASS] No new warnings detected."
        exit 0
    }
}

Write-Error "Specify either -GenerateBaseline or -CheckNewWarnings"
exit 1
