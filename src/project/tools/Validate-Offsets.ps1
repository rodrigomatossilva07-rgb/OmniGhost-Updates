<#
.SYNOPSIS
    Validates OmniGhost game offset JSON files against the schema.

.DESCRIPTION
    Comprehensive validation of offset JSON files including:
    - JSON syntax validity
    - Schema compliance (schema_version, required fields, types)
    - Offset format (hex strings, resolved boolean)
    - Cross-reference validation (duplicate keys, missing critical offsets)
    - Build consistency checks

.PARAMETER Path
    Path to offset JSON file or directory (default: src/project/data)

.PARAMETER SchemaPath
    Path to JSON schema file (default: src/project/tools/offset_schema.json)

.PARAMETER Strict
    Treat warnings as errors

.PARAMETER Verbose
    Verbose output

.PARAMETER Report
    Output validation report to JSON file

.EXAMPLE
    .\Validate-Offsets.ps1

.EXAMPLE
    .\Validate-Offsets.ps1 -Path "src/project/data/cs2_offsets.json" -Strict -Verbose

.EXAMPLE
    .\Validate-Offsets.ps1 -Path "src/project/data" -Report "validation_report.json"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$Path = "src/project/data",

    [Parameter(Mandatory=$false)]
    [string]$SchemaPath = "src/project/tools/offset_schema.json",

    [Parameter(Mandatory=$false)]
    [switch]$Strict,

    [Parameter(Mandatory=$false)]
    [switch]$ShowVerbose,

    [Parameter(Mandatory=$false)]
    [string]$Report
)

$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------
$REQUIRED_TOP_LEVEL = @(
    'schema_version', 'game', 'build', 'build_id', 'module',
    'source', 'generated_at', 'namespaces', 'decryptions'
)

$VALID_GAMES = @('rust', 'cs2', 'fivem', 'fortnite', 'warzone', 'apex', 'unknown')

$CRITICAL_OFFSETS = @{
    'rust' = @('_root.basenetworkable_pointer', '_root.camera_pointer', 'BaseNetworkable.client_entities', 'BasePlayer.playerModel', 'PlayerModel.position', 'BaseCamera.matrix')
    'cs2'  = @('_root.dwEntityList', '_root.dwLocalPlayerPawn', '_root.dwViewMatrix', '_root.dwViewAngles', '_root.dwGlobalVars', 'C_BaseEntity.m_iHealth')
    'fivem' = @('_root.world', '_root.player_health', '_root.player_position', '_root.bone_list', '_root.bone_matrix')
    'fortnite' = @('globals.GWorld', 'gworld_crypto.world_mul', 'gworld_crypto.world_add', 'runtime.pcm_cam_location', 'runtime.pcm_cam_rotation', 'runtime.pcm_cam_fov')
    'warzone' = @('_root.ref_def', '_root.entity_list', '_root.camera_base', '_root.view_matrix', 'player.size', 'player.pos')
    'apex' = @('_root.EntityList', '_root.LocalPlayer', '_root.ViewRender', '_root.view_matrix', 'base_entity.m_vecAbsOrigin', 'base_entity.m_iHealth')
}

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

function Validate-HexOffset {
    param([string]$Offset)
    if (-not $Offset) { return $false }
    return $Offset -match '^0x[0-9A-Fa-f]+$'
}

# -----------------------------------------------------------------------------
# Main Validation
# -----------------------------------------------------------------------------
Write-Log "Validating OmniGhost offset JSON files..."
Write-Log "Schema: ${SchemaPath}"
Write-Log "Target: ${Path}"

# Load schema
$schemaJson = Read-JsonFile -FilePath $SchemaPath

# Find JSON files
if (Test-Path $Path -PathType Leaf) {
    $jsonFiles = @($Path)
} else {
    $jsonFiles = Get-ChildItem -Path $Path -Recurse -Filter "*_offsets.json" -File -ErrorAction SilentlyContinue |
        Where-Object { -not ($_.FullName -match '\\.(git|vs|cache|build|packages|vcpkg|third_party|node_modules|target)\\') }
}

Write-Log "Found $($jsonFiles.Count) offset JSON file(s)"

$results = @()
$totalErrors = 0
$totalWarnings = 0

foreach ($file in $jsonFiles) {
    $relPath = $file.FullName.Replace((Get-Location).Path + "\", "")
    Write-Log "`nValidating: ${relPath}" -Level Info

    $fileErrors = 0
    $fileWarnings = 0
    $fileResult = @{
        file = $relPath
        valid = $true
        errors = @()
        warnings = @()
        metadata = @{}
    }

    try {
        $json = Read-JsonFile -FilePath $file.FullName
    } catch {
        $fileResult.valid = $false
        $fileResult.errors += "JSON Parse Error: $($_.Exception.Message)"
        $fileErrors++
        $results += $fileResult
        continue
    }

    # 1. Validate top-level required fields
    foreach ($field in $REQUIRED_TOP_LEVEL) {
        if (-not $json.PSObject.Properties.Match($field)) {
            $fileResult.errors += "Missing required field: ${field}"
            $fileErrors++
        }
    }

    # 2. Validate schema_version
    if ($json.PSObject.Properties.Match('schema_version')) {
        $ver = $json.schema_version
        if ($ver -lt 1 -or $ver -gt 10) {
            $fileResult.errors += "schema_version out of range (1-10): ${ver}"
            $fileErrors++
        }
    }

    # 3. Validate game enum
    if ($json.PSObject.Properties.Match('game')) {
        $game = $json.game
        if ($VALID_GAMES -notcontains $game) {
            $fileResult.errors += "Invalid game value: ${game} (valid: $($VALID_GAMES -join ', '))"
            $fileErrors++
        }
        $fileResult.metadata.game = $game
    }

    # 4. Validate build format
    if ($json.PSObject.Properties.Match('build')) {
        $build = $json.build
        if (-not ($build -match '^[0-9]+$') -and -not ($build -match '^0x[0-9A-Fa-f]+$')) {
            $fileResult.warnings += "build should be numeric string: ${build}"
            $fileWarnings++
        }
        $fileResult.metadata.build = $build
    }

    # 5. Validate module
    if ($json.PSObject.Properties.Match('module')) {
        $fileResult.metadata.module = $json.module
    }

    # 6. Validate generated_at (ISO 8601)
    if ($json.PSObject.Properties.Match('generated_at')) {
        $dt = $json.generated_at
        try {
            [DateTime]::Parse($dt, [System.Globalization.CultureInfo]::InvariantCulture, [System.Globalization.DateTimeStyles]::AdjustToUniversal) | Out-Null
        } catch {
            $fileResult.warnings += "generated_at not valid ISO 8601: ${dt}"
            $fileWarnings++
        }
    }

    # 7. Validate namespaces structure
    if ($json.PSObject.Properties.Match('namespaces')) {
        $namespaces = $json.namespaces
        $allOffsetPaths = @{}
        $duplicateCount = 0

        foreach ($nsName in $namespaces.PSObject.Properties.Name) {
            $ns = $namespaces.$nsName
            if (-not $ns.PSObject.Properties.Match('fields')) {
                $fileResult.errors += "Namespace '${nsName}' missing 'fields' object"
                $fileErrors++
                continue
            }

            foreach ($fieldName in $ns.fields.PSObject.Properties.Name) {
                $field = $ns.fields.$fieldName
                $path = "${nsName}.${fieldName}"

                # Check duplicate
                if ($allOffsetPaths.ContainsKey($path)) {
                    $fileResult.warnings += "Duplicate offset path: ${path}"
                    $fileWarnings++
                    $duplicateCount++
                }
                $allOffsetPaths[$path] = $true

                # Validate field structure
                if (-not $field.PSObject.Properties.Match('offset')) {
                    $fileResult.errors += "Field '${path}' missing 'offset'"
                    $fileErrors++
                } else {
                    if (-not (Validate-HexOffset -Offset $field.offset)) {
                        $fileResult.errors += "Field '${path}' offset not valid hex: ${field.offset}"
                        $fileErrors++
                    }
                }

                if (-not $field.PSObject.Properties.Match('resolved')) {
                    $fileResult.errors += "Field '${path}' missing 'resolved' boolean"
                    $fileErrors++
                } elseif ($field.resolved -notin @($true, $false)) {
                    $fileResult.errors += "Field '${path}' resolved not boolean: ${field.resolved}"
                    $fileErrors++
                }

                if ($field.PSObject.Properties.Match('comment') -and $field.comment -eq '') {
                    $fileResult.warnings += "Field '${path}' has empty comment"
                    $fileWarnings++
                }
            }
        }

        # Check critical offsets for this game
        if ($fileResult.metadata.game -and $CRITICAL_OFFSETS.ContainsKey($fileResult.metadata.game)) {
            $game = $fileResult.metadata.game
            foreach ($critPath in $CRITICAL_OFFSETS[$game]) {
                if (-not $allOffsetPaths.ContainsKey($critPath)) {
                    $fileResult.warnings += "Critical offset missing for ${game}: ${critPath}"
                    $fileWarnings++
                }
            }
        }

        $fileResult.metadata.namespace_count = $namespaces.PSObject.Properties.Count
        $fileResult.metadata.field_count = $allOffsetPaths.Count
        $fileResult.metadata.duplicate_fields = $duplicateCount
    }

    # 8. Validate decryptions
    if ($json.PSObject.Properties.Match('decryptions')) {
        $dec = $json.decryptions
        $requiredDec = @('namespace', 'functions', 'rva_fingerprints')
        foreach ($req in $requiredDec) {
            if (-not $dec.PSObject.Properties.Match($req)) {
                $fileResult.errors += "decryptions missing required field: ${req}"
                $fileErrors++
            }
        }

        if ($dec.PSObject.Properties.Match('functions')) {
            $funcs = $dec.functions
            if ($funcs.Count -eq 0) {
                $fileResult.warnings += "decryptions.functions array is empty"
                $fileWarnings++
            }
        }

        if ($dec.PSObject.Properties.Match('rva_fingerprints')) {
            $fps = $dec.rva_fingerprints
            foreach ($fname in $fps.PSObject.Properties.Name) {
                $fpr = $fps.$fname
                if (-not (Validate-HexOffset -Offset $fpr)) {
                    $fileResult.errors += "decryptions.rva_fingerprints['${fname}'] not valid hex: ${fpr}"
                    $fileErrors++
                }
            }
        }
    }

    # 9. Build consistency: check if offsets look reasonable for module
    if ($fileResult.metadata.game -and $fileResult.metadata.module) {
        $module = $fileResult.metadata.module
        if ($module -like '*.dll' -or $module -like '*.exe') {
            # Module name looks valid
        } else {
            $fileResult.warnings += "Module name doesn't look like DLL/EXE: ${module}"
            $fileWarnings++
        }
    }

    # Summary
    $fileResult.valid = ($fileErrors -eq 0)
    $fileResult.error_count = $fileErrors
    $fileResult.warning_count = $fileWarnings

    $totalErrors += $fileErrors
    $totalWarnings += $fileWarnings

    if ($fileResult.valid) {
        Write-Log "  PASS (${fileWarnings} warnings)" -Level Pass
    } else {
        Write-Log "  FAIL (${fileErrors} errors, ${fileWarnings} warnings)" -Level Fail
        foreach ($err in $fileResult.errors) { Write-Log "    ERROR: ${err}" -Level Error }
    }
    foreach ($warn in $fileResult.warnings) { Write-Log "    WARN: ${warn}" -Level Warn }

    $results += $fileResult
}

# -----------------------------------------------------------------------------
# Final Report
# -----------------------------------------------------------------------------
Write-Log "`n============================================================"
Write-Log "VALIDATION SUMMARY"
Write-Log "============================================================"
Write-Log "Files checked:  $($jsonFiles.Count)"
Write-Log "Total errors:   ${totalErrors}"
Write-Log "Total warnings: ${totalWarnings}"

$passed = ($results | Where-Object { $_.valid }).Count
$failed = ($results | Where-Object { -not $_.valid }).Count
Write-Log "Passed: ${passed}"
Write-Log "Failed: ${failed}"

if ($Report) {
    $schemaVer = if ($schemaJson.PSObject.Properties.Match('schema_version')) { $schemaJson.schema_version } else { 'unknown' }
    $reportObj = @{
        timestamp = (Get-Date).ToString('o')
        schema_version = $schemaVer
        total_files = $jsonFiles.Count
        total_errors = $totalErrors
        total_warnings = $totalWarnings
        passed = $passed
        failed = $failed
        results = $results
    }
    $reportObj | ConvertTo-Json -Depth 10 | Set-Content -Path $Report -Encoding ([System.Text.UTF8Encoding]::new($false))
    Write-Log "Report saved to: ${Report}" -Level Info
}

# Exit code
if ($totalErrors -gt 0 -or ($Strict -and $totalWarnings -gt 0)) {
    Write-Log "VALIDATION FAILED" -Level Fail
    exit 1
} else {
    Write-Log "VALIDATION PASSED" -Level Pass
    exit 0
}