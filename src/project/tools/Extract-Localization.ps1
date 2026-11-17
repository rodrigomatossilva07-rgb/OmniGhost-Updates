<#
.SYNOPSIS
    Extracts translations from localization.cpp and generates initial locale JSON files.

.DESCRIPTION
    Reads the legacy localization.cpp file with its inline translation table
    and generates per-language JSON files in src/window/locales/.

.EXAMPLE
    .\Extract-Localization.ps1
#>

[CmdletBinding()]
param()

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

$SRC_FILE = "src/window/localization.cpp"
$LOCALE_DIR = "src/window/locales"

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
function Write-Log {
    param(
        [string]$Message,
        [ValidateSet('Info','Warn','Error','Pass','Fail')]
        [string]$Level = 'Info'
    )
    $prefix = switch ($Level) {
        'Info'  { '[INFO]' }
        'Warn'  { '[WARN]' }
        'Error' { '[ERROR]' }
        'Pass'  { '[PASS]' }
        'Fail'  { '[FAIL]' }
    }
    Write-Host "${prefix} ${Message}"
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
$projectRoot = Get-Location
$srcFilePath = Join-Path $projectRoot $SRC_FILE
$localeDirPath = Join-Path $projectRoot $LOCALE_DIR

if (-not (Test-Path $srcFilePath)) {
    Write-Log "Source file not found: ${srcFilePath}" -Level Error
    exit 1
}

if (-not (Test-Path $localeDirPath)) {
    New-Item -ItemType Directory -Path $localeDirPath -Force | Out-Null
}

Write-Log "Extracting translations from ${SRC_FILE}..."

$content = Get-Content $srcFilePath -Raw -Encoding UTF8

# Pattern to match: {"key", {"pt", "en", "de", "es", "fr", "it", "ru"}},
$pattern = '\{\s*"([^"]+)"\s*,\s*\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}\s*\}'
$regex = [System.Text.RegularExpressions.Regex]::new($pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline -bor [System.Text.RegularExpressions.RegexOptions]::Singleline)

$translations = [ordered]@{}
$matches = $regex.Matches($content)

foreach ($match in $matches) {
    $key = $match.Groups[1].Value
    $translations[$key] = @{
        'pt' = $match.Groups[2].Value
        'en' = $match.Groups[3].Value
        'de' = $match.Groups[4].Value
        'es' = $match.Groups[5].Value
        'fr' = $match.Groups[6].Value
        'it' = $match.Groups[7].Value
        'ru' = $match.Groups[8].Value
    }
}

Write-Log "Found ${translations.Count} translation keys"

Write-Log "Generating locale JSON files..."
foreach ($langCode in $LANGUAGES.Keys) {
    $localeData = [ordered]@{}
    foreach ($key in $translations.Keys) {
        $localeData[$key] = $translations[$key][$langCode]
    }
    
    $filePath = Join-Path $localeDirPath "$langCode.json"
    $localeData | ConvertTo-Json | Set-Content $filePath -Encoding UTF8
    Write-Log "Generated ${filePath} with ${localeData.Count} keys"
}

Write-Log "Done!" -Level Pass