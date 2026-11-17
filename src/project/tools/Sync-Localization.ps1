<#
.SYNOPSIS
    Syncs OmniGhost localization keys between source code and locale JSON files.

.DESCRIPTION
    This script extracts all Loc::Tr() and Loc::TrFmt() keys from C++ source files
    and synchronizes them with per-language JSON files in src/window/locales/.
    
    Supported languages: pt (Português), en (English), de (Deutsch), 
    es (Español), fr (Français), it (Italiano), ru (Русский)

.PARAMETER DryRun
    Show what would be done without making changes

.PARAMETER Verbose
    Verbose output

.PARAMETER GenerateHeader
    Generate C++ header with key constants (src/window/localization_keys.h)

.PARAMETER Report
    Output report to JSON file

.PARAMETER SrcDir
    Source directory to scan (default: src)

.PARAMETER LocaleDir
    Locale directory (default: src/window/locales)

.EXAMPLE
    .\Sync-Localization.ps1

.EXAMPLE
    .\Sync-Localization.ps1 -DryRun -Verbose

.EXAMPLE
    .\Sync-Localization.ps1 -GenerateHeader

.EXAMPLE
    .\Sync-Localization.ps1 -Report "sync-report.json"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [switch]$DryRun,

    [Parameter(Mandatory=$false)]
    [switch]$ShowVerbose,

    [Parameter(Mandatory=$false)]
    [switch]$GenerateHeader,

    [Parameter(Mandatory=$false)]
    [string]$Report,

    [Parameter(Mandatory=$false)]
    [string]$SrcDir = "src",

    [Parameter(Mandatory=$false)]
    [string]$LocaleDir = "src/window/locales"
)

$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------
$LANGUAGES = @{
    'pt' = 'Português'
    'en' = 'English'
    'de' = 'Deutsch'
    'es' = 'Español'
    'fr' = 'Français'
    'it' = 'Italiano'
    'ru' = 'Русский'
}

$DEFAULT_LANG = 'pt'

$LOC_TR_PATTERN = 'Loc::Tr\s*\(\s*["'']([^"'']+)["'']\s*\)'
$LOC_TR_FMT_PATTERN = 'Loc::TrFmt\s*\(\s*["'']([^"'']+)["'']\s*\)'

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
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
    Write-Host "$prefix $Message"
}

function Find-CppFiles {
    param([string]$Root)
    $files = @()
    foreach ($ext in @('.cpp', '.h', '.hpp', '.inl')) {
        $files += Get-ChildItem -Path $Root -Recurse -Filter "*$ext" -File -ErrorAction SilentlyContinue
    }
    # Filter out excluded directories
    $excluded = @('third_party', '.cache', 'build', '.vs', '.git', 'vcpkg', 'packages', 'dumper', 'node_modules')
    return $files | Where-Object { $f = $_; -not ($excluded | Where-Object { $f.FullName -like "*\$_\*" }) }
}

function Extract-KeysFromFile {
    param([string]$FilePath)
    $keys = @()
    try {
        $content = Get-Content $FilePath -Raw -Encoding UTF8
    }
    catch {
        try {
            $content = Get-Content $FilePath -Raw -Encoding Latin1
        }
        catch {
            return $keys
        }
    }

    # Find Loc::Tr() calls
    $matches = [System.Text.RegularExpressions.Regex]::Matches($content, $LOC_TR_PATTERN)
    foreach ($match in $matches) {
        $keys += $match.Groups[1].Value
    }

    # Find Loc::TrFmt() calls
    $matches = [System.Text.RegularExpressions.Regex]::Matches($content, $LOC_TR_FMT_PATTERN)
    foreach ($match in $matches) {
        $keys += $match.Groups[1].Value
    }

    return $keys | Select-Object -Unique
}

function Extract-AllKeys {
    param([string]$SrcDir)
    Write-Log "Scanning source files in $SrcDir..."
    $files = Find-CppFiles -Root $SrcDir
    Write-Log "Found $($files.Count) source files"
    
    $allKeys = @()
    foreach ($file in $files) {
        $keys = Extract-KeysFromFile -FilePath $file.FullName
        $allKeys += $keys
    }
    
    return $allKeys | Select-Object -Unique | Sort-Object
}

function Load-LocaleFile {
    param([string]$Lang, [string]$LocaleDir)
    $filePath = Join-Path $LocaleDir "$Lang.json"
    if (Test-Path $filePath) {
        try {
            $content = Get-Content $filePath -Raw -Encoding UTF8
            if ($content.StartsWith([char]0xFEFF)) {
                $content = $content.Substring(1)
            }
            $json = $content | ConvertFrom-Json
            # Convert to hashtable for easier manipulation
            $hash = @{}
            foreach ($prop in $json.PSObject.Properties) {
                $hash[$prop.Name] = $prop.Value
            }
            return $hash
        }
        catch {
            Write-Log "Warning: Failed to parse ${filePath}: $($_.Exception.Message)" -Level Warn
        }
    }
    return @{}
}

function Save-LocaleFile {
    param([string]$Lang, [object]$Data, [string]$LocaleDir)
    $filePath = Join-Path $LocaleDir "$Lang.json"
    # Sort keys for consistent output
    $keys = $Data.Keys | Sort-Object
    # Build JSON manually for precise control
    $jsonLines = @()
    $jsonLines += "{"
    $first = $true
    foreach ($key in $keys) {
        $value = $Data[$key]
        # Escape value for JSON
        $escapedValue = $value -replace '"', '\"' -replace '\\', '\\\\' -replace "`r", '\r' -replace "`n", '\n' -replace "`t", '\t'
        $prefix = if ($first) { "    " } else { "    ," }
        $jsonLines += "$prefix`"$key`": `"$escapedValue`""
        $first = $false
    }
    $jsonLines += "}"
    # Write UTF-8 without BOM using .NET File.WriteAllText with UTF8Encoding without BOM
    $content = $jsonLines -join "`r`n"
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($filePath, $content, $encoding)
}

function Generate-CppHeader {
    param([string[]]$Keys, [string]$OutputPath)
    $header = @()
    $header += "// Generated by Sync-Localization.ps1 - DO NOT EDIT MANUALLY"
    $header += "#pragma once"
    $header += ""
    $header += "namespace OmniGhost::Localization::Keys {"
    $header += ""
    
    foreach ($key in $Keys) {
        $constName = $key -replace '\.', '_' -replace '-', '_' | ForEach-Object { $_.ToUpper() }
        $header += "    constexpr const char* $constName = `"$key`";"
    }
    
    $header += ""
    $header += "}"
    $header += ""
    
    if (-not $DryRun) {
        $header -join "`r`n" | Set-Content $OutputPath -Encoding UTF8
        Write-Log "Generated $OutputPath"
    }
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
$projectRoot = Get-Location
$srcDirPath = Join-Path $projectRoot $SrcDir
$localeDirPath = Join-Path $projectRoot $LocaleDir

# Create locale directory if it doesn't exist
if (-not (Test-Path $localeDirPath)) {
    New-Item -ItemType Directory -Path $localeDirPath -Force | Out-Null
}

if ($GenerateHeader) {
    Write-Log "Generating C++ header with key constants..."
    $keys = Extract-AllKeys -SrcDir $srcDirPath
    $headerPath = Join-Path $projectRoot "src/window/localization_keys.h"
    Generate-CppHeader -Keys $keys -OutputPath $headerPath
    exit 0
}

Write-Log "Syncing localization keys..."

# Extract keys from source
$sourceKeys = Extract-AllKeys -SrcDir $srcDirPath
Write-Log "Found $($sourceKeys.Count) unique localization keys in source code"

# Load existing locales
$locales = @{}
$langList = @($LANGUAGES.Keys)
foreach ($lang in $langList) {
    $locales[$lang] = Load-LocaleFile -Lang $lang -LocaleDir $localeDirPath
    Write-Log "  ${lang} ($($LANGUAGES[$lang])): $($locales[$lang].Count) keys"
}

# Check for missing/extra keys
$allMissing = @{}
$allExtra = @{}

foreach ($lang in $langList) {
    $localeKeys = $locales[$lang].Keys | ForEach-Object { $_ }
    $missing = $sourceKeys | Where-Object { $_ -notin $localeKeys }
    $extra = $localeKeys | Where-Object { $_ -notin $sourceKeys }
    
    if ($missing.Count -gt 0) {
        $allMissing[$lang] = $missing | Sort-Object
        Write-Log "  ${lang}: $($missing.Count) missing keys" -Level Warn
    }
    
    if ($extra.Count -gt 0) {
        $allExtra[$lang] = $extra | Sort-Object
        Write-Log "  ${lang}: $($extra.Count) extra keys (not in source)" -Level Warn
    }
}

# Check default language completeness
$defaultKeys = $locales[$DEFAULT_LANG].Keys | ForEach-Object { $_ }
$defaultMissing = $sourceKeys | Where-Object { $_ -notin $defaultKeys }
if ($defaultMissing.Count -gt 0) {
    Write-Log "WARNING: Default language (${DEFAULT_LANG}) missing $($defaultMissing.Count) keys from source" -Level Warn
}

# Sync missing keys to each locale
if (-not $DryRun) {
    # First, populate the default language with all source keys (using key as placeholder)
    $defaultLocaleData = $locales[$DEFAULT_LANG]
    foreach ($key in $sourceKeys) {
        if (-not $defaultLocaleData.ContainsKey($key)) {
            $defaultLocaleData[$key] = $key
        }
    }
    Save-LocaleFile -Lang $DEFAULT_LANG -Data $defaultLocaleData -LocaleDir $localeDirPath
    Write-Log "  Updated ${DEFAULT_LANG}.json (default language)" -Level Pass
    
    # Now sync other languages using default as fallback
    foreach ($lang in $langList) {
        if ($lang -eq $DEFAULT_LANG) {
            continue  # Already handled
        }
        
        $localeData = $locales[$lang]
        $changed = $false
        
        foreach ($key in $sourceKeys) {
            if (-not $localeData.ContainsKey($key)) {
                # Use default language as fallback, or key as placeholder
                $fallback = if ($locales[$DEFAULT_LANG].ContainsKey($key)) {
                    $locales[$DEFAULT_LANG][$key]
                }
                else {
                    $key
                }
                $localeData[$key] = $fallback
                $changed = $true
                if ($ShowVerbose) {
                    Write-Log "    Added to ${lang}: $key" -Level Debug
                }
            }
        }
        
        if ($changed) {
            Save-LocaleFile -Lang $lang -Data $localeData -LocaleDir $localeDirPath
            Write-Log "  Updated ${lang}.json" -Level Pass
        }
    }
}

# Generate report
$report = @{
    source_keys_count = $sourceKeys.Count
    languages = @{}
    missing_keys = $allMissing
    extra_keys = $allExtra
}

foreach ($lang in $langList) {
    $missingCount = 0
    $extraCount = 0
    if ($allMissing.ContainsKey($lang)) { $missingCount = ($allMissing[$lang] | Measure-Object).Count }
    if ($allExtra.ContainsKey($lang)) { $extraCount = ($allExtra[$lang] | Measure-Object).Count }
    
    $report.languages[$lang] = @{
        name = $LANGUAGES[$lang]
        keys_count = $locales[$lang].Count
        missing_count = $missingCount
        extra_count = $extraCount
    }
}

# Save report
$reportPath = Join-Path $localeDirPath "sync_report.json"
if (-not $DryRun) {
    $report | ConvertTo-Json | Set-Content $reportPath -Encoding UTF8
    Write-Log "Report saved to $reportPath"
}

if ($Report) {
    $report | ConvertTo-Json | Set-Content (Join-Path $projectRoot $Report) -Encoding UTF8
}

$success = ($allMissing.Count -eq 0)
if (-not $success -and -not $DryRun) {
    Write-Log "WARNING: Some languages are missing keys!" -Level Warn
    exit 1
}
elseif ($DryRun) {
    Write-Log "Dry run complete. No changes made." -Level Info
}
else {
    Write-Log "Localization sync complete!" -Level Pass
    exit 0
}