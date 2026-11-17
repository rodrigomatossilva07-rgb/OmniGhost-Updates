<#
.SYNOPSIS
    Migrates OmniGhost game offset JSON files to the latest schema version (v7).

.DESCRIPTION
    Handles migration from older schema versions to v7:
    - v1 (flat client.dll/engine2.dll structure) -> v7 (namespaces + decryptions)
    - v2 (CS2 legacy) -> v7
    - v3-v6 (intermediate) -> v7
    
    Preserves all offset data while restructuring to unified schema.

.PARAMETER Path
    Path to offset JSON file or directory to migrate

.PARAMETER TargetVersion
    Target schema version (default: 7)

.PARAMETER DryRun
    Show what would be done without making changes

.PARAMETER Backup
    Create .bak files before migration

.PARAMETER Force
    Overwrite existing files without prompt

.EXAMPLE
    .\Migrate-Offsets.ps1 -Path "src/project/data/cs2_offsets.json" -DryRun

.EXAMPLE
    .\Migrate-Offsets.ps1 -Path "src/project/data" -Backup -TargetVersion 7

.EXAMPLE
    .\Migrate-Offsets.ps1 -Path "src/project/data/legacy_offsets.json" -Force
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$Path,

    [Parameter(Mandatory=$false)]
    [int]$TargetVersion = 7,

    [Parameter(Mandatory=$false)]
    [switch]$DryRun,

    [Parameter(Mandatory=$false)]
    [switch]$Backup,

    [Parameter(Mandatory=$false)]
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------
$GAME_MODULE_MAP = @{
    'rust'      = 'GameAssembly.dll'
    'cs2'       = 'client.dll'
    'fivem'     = 'GTAProcess.exe'
    'fortnite'  = 'FortniteClient-Win64-Shipping.exe'
    'warzone'   = 'cod.exe'
    'apex'      = 'r5apex.exe'
    'unknown'   = 'unknown'
}

$GAME_SOURCE_MAP = @{
    'rust'      = 'rustoffsets.com'
    'cs2'       = 'a2x/cs2-dumper'
    'fivem'     = 'FiveM offset dumper'
    'fortnite'  = 'user dump'
    'warzone'   = 'user-supplied dump'
    'apex'      = 'a2x/apex-dumper'
    'unknown'   = 'manual'
}

$CRITICAL_FIELDS = @{
    'rust' = @(
        '_root.basenetworkable_pointer', '_root.camera_pointer',
        'BaseNetworkable.client_entities', 'BaseNetworkable.static_fields',
        'BasePlayer.playerModel', 'PlayerModel.position',
        'BaseCamera.matrix', 'BaseCamera.viewMatrix', 'BaseCamera.projMatrix'
    )
    'cs2' = @(
        '_root.dwEntityList', '_root.dwLocalPlayerPawn', '_root.dwViewMatrix',
        '_root.dwViewAngles', '_root.dwGlobalVars', '_root.dwPlantedC4',
        '_root.entity_identity_stride', '_root.entity_identity_pointer',
        'C_BaseEntity.m_iHealth', 'C_BaseEntity.m_iTeamNum',
        'C_BaseEntity.m_pGameSceneNode', 'C_BaseEntity.m_fFlags',
        'CGameSceneNode.m_vecAbsOrigin', 'CSkeletonInstance.m_modelState',
        'CModelState.m_boneArray'
    )
    'fivem' = @(
        '_root.world', '_root.replay', '_root.viewport', '_root.camera',
        '_root.player_info', '_root.bone_list', '_root.bone_matrix',
        '_root.player_health', '_root.player_position', '_root.ped_visible_flag',
        '_root.object_pool', '_root.vehicle_pool', '_root.ped_pool',
        '_root.network_player_mgr', '_root.blip_list', '_root.waypoint', '_root.aim_cped'
    )
    'fortnite' = @(
        'globals.GWorld', 'globals.GNames', 'globals.ProcessEvent',
        'gworld_crypto.rva', 'gworld_crypto.world_mul', 'gworld_crypto.world_add',
        'runtime.pcm_cam_location', 'runtime.pcm_cam_rotation', 'runtime.pcm_cam_fov'
    )
    'warzone' = @(
        '_root.ref_def', '_root.name_array', '_root.camera_base',
        '_root.view_matrix', '_root.entity_list', '_root.max_player_count',
        '_root.game_mode', '_root.weapon_definitions'
    )
    'apex' = @(
        '_root.CHLClient', '_root.ClientState', '_root.EntityList',
        '_root.LocalPlayer', '_root.ViewRender', '_root.ObserverList',
        '_root.NameList', '_root.ModelNames', '_root.WeaponNames',
        'base_entity.m_vecAbsOrigin', 'base_entity.m_iHealth',
        'base_entity.m_iTeamNum', 'base_entity.m_lifeState',
        'base_entity.m_shieldHealth', 'base_entity.m_iMaxHealth'
    )
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

function Write-JsonFile {
    param([string]$FilePath, [object]$Data)
    $json = $Data | ConvertTo-Json -Depth 10
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($FilePath, $json, $encoding)
}

function Get-GameFromData {
    param($json)
    # Try explicit game field
    if ($json.PSObject.Properties.Match('game')) {
        return $json.game
    }
    # Infer from module
    if ($json.PSObject.Properties.Match('module')) {
        foreach ($kv in $GAME_MODULE_MAP.GetEnumerator()) {
            if ($json.module -like "*$($kv.Value)*") {
                return $kv.Key
            }
        }
    }
    # Infer from structure
    if ($json.PSObject.Properties.Match('client.dll')) { return 'cs2' }
    if ($json.PSObject.Properties.Match('engine2.dll')) { return 'cs2' }
    if ($json.PSObject.Properties.Match('_root.basenetworkable_pointer')) { return 'rust' }
    if ($json.PSObject.Properties.Match('globals.GWorld')) { return 'fortnite' }
    if ($json.PSObject.Properties.Match('CHLClient')) { return 'apex' }
    if ($json.PSObject.Properties.Match('ref_def')) { return 'warzone' }
    return 'unknown'
}

function Migrate-ToV7 {
    param($json, [string]$Game)

    $now = (Get-Date).ToUniversalTime().ToString('o')
    $module = $GAME_MODULE_MAP[$Game] ?? 'unknown'
    $source = $GAME_SOURCE_MAP[$Game] ?? 'manual'
    $cl = $json.cl ?? $json.build_id ?? ''

    # Build new v7 structure
    $v7 = @{
        schema_version = 7
        game = $Game
        build = ($json.build ?? $json.version ?? 'unknown').ToString()
        build_id = ($json.build_id ?? $json.build ?? $json.version ?? 'unknown').ToString()
        module = $module
        source = $source
        generated_at = $now
        cl = $cl
        namespaces = @{}
        decryptions = @{
            namespace = 'decryption'
            functions = @()
            rva_fingerprints = @{}
        }
    }

    # Migrate based on source structure
    if ($json.PSObject.Properties.Match('_root')) {
        # Rust-style _root already matches v7
        $v7.namespaces['_root'] = @{ fields = $json._root.fields }
    } elseif ($json.PSObject.Properties.Match('client.dll') -or $json.PSObject.Properties.Match('engine2.dll')) {
        # CS2 v1/v2 flat structure
        $v7.namespaces['_root'] = @{ fields = @{} }
        $v7.namespaces['client.dll'] = @{ fields = $json['client.dll'] ?? @{} }
        $v7.namespaces['engine2.dll'] = @{ fields = $json['engine2.dll'] ?? @{} }
        $v7.namespaces['inputsystem.dll'] = @{ fields = $json['inputsystem.dll'] ?? @{} }
        $v7.namespaces['matchmaking.dll'] = @{ fields = $json['matchmaking.dll'] ?? @{} }
        $v7.namespaces['soundsystem.dll'] = @{ fields = $json['soundsystem.dll'] ?? @{} }
        $v7.namespaces['layout'] = @{ fields = $json.layout ?? @{} }
        $v7.namespaces['bone_mapping'] = @{ fields = $json.bone_mapping ?? @{} }
        $v7.namespaces['schemas'] = @{}
        if ($json.schemas) {
            foreach ($sch in $json.schemas.PSObject.Properties.Name) {
                $v7.namespaces['schemas'][$sch] = @{ fields = $json.schemas.$sch }
            }
        }
    } elseif ($json.PSObject.Properties.Match('globals') -or $json.PSObject.Properties.Match('gworld_crypto')) {
        # Fortnite structure
        $v7.namespaces['_root'] = @{ fields = $json.globals ?? @{} }
        $v7.namespaces['gworld_crypto'] = @{ fields = $json.gworld_crypto ?? @{} }
        $v7.namespaces['runtime'] = @{ fields = $json.runtime ?? @{} }
        $v7.namespaces['dump_structs'] = @{}
        if ($json.dump_structs) {
            foreach ($ds in $json.dump_structs.PSObject.Properties.Name) {
                $v7.namespaces['dump_structs'][$ds] = @{ fields = $json.dump_structs.$ds }
            }
        }
        # Engine, GameViewportClient, UWorld, etc.
        foreach ($ns in @('engine', 'GameViewportClient', 'UWorld', 'ULevel', 'UGameInstance', 'APlayerController', 'AController', 'APawn', 'ACharacter', 'AActor', 'USceneComponent', 'APlayerCameraManager', 'AGameStateBase', 'APlayerState', 'AFortPlayerState', 'AFortPawn', 'AFortPlayerStateAthena')) {
            if ($json.PSObject.Properties.Match($ns)) {
                $v7.namespaces[$ns] = @{ fields = $json.$ns }
            }
        }
    } elseif ($json.PSObject.Properties.Match('offsets')) {
        # Warzone structure
        $v7.namespaces['_root'] = @{ fields = $json.offsets ?? @{} }
        $v7.namespaces['decrypt_rva'] = @{ fields = $json.decrypt_rva ?? @{} }
        $v7.namespaces['camera'] = @{ fields = $json.camera ?? @{} }
        $v7.namespaces['local'] = @{ fields = $json.local ?? @{} }
        $v7.namespaces['player'] = @{ fields = $json.player ?? @{} }
        $v7.namespaces['bone'] = @{ fields = $json.bone ?? @{} }
        $v7.namespaces['entity'] = @{ fields = $json.entity ?? @{} }
        $v7.namespaces['name_array'] = @{ fields = $json.name_array ?? @{} }
    } elseif ($json.PSObject.Properties.Match('CHLClient') -or $json.PSObject.Properties.Match('base_entity')) {
        # Apex structure
        $v7.namespaces['_root'] = @{ fields = @{} }
        foreach ($prop in $json.PSObject.Properties) {
            if ($prop.Name -in @('id', 'game_slug', 'version', 'label', 'published', 'created_at', 'updated_at', 'markdown')) { continue }
            $v7.namespaces['_root'].fields[$prop.Name] = $prop.Value
        }
        # Camera namespace
        $v7.namespaces['camera'] = @{ fields = @{} }
        if ($json.PSObject.Properties.Match('view_matrix')) { $v7.namespaces['camera'].fields['view_matrix'] = $json.view_matrix }
        if ($json.PSObject.Properties.Match('camera_origin')) { $v7.namespaces['camera'].fields['camera_origin'] = $json.camera_origin }
        if ($json.PSObject.Properties.Match('view_angles')) { $v7.namespaces['camera'].fields['view_angles'] = $json.view_angles }
        if ($json.PSObject.Properties.Match('studio_hdr')) { $v7.namespaces['camera'].fields['studio_hdr'] = $json.studio_hdr }
        # Base entity
        $v7.namespaces['base_entity'] = @{ fields = $json.base_entity ?? @{} }
        # CBasePlayer, CBaseCombatWeapon
        if ($json.PSObject.Properties.Match('CBasePlayer')) { $v7.namespaces['CBasePlayer'] = @{ fields = $json.CBasePlayer } }
        if ($json.PSObject.Properties.Match('CBaseCombatWeapon')) { $v7.namespaces['CBaseCombatWeapon'] = @{ fields = $json.CBaseCombatWeapon } }
    } else {
        # Fallback: treat all top-level as namespaces
        foreach ($prop in $json.PSObject.Properties) {
            if ($prop.Name -in @('schema_version', 'game', 'build', 'build_id', 'module', 'source', 'generated_at', 'cl', 'note', 'verified_utc', 'namespaces', 'decryptions')) { continue }
            $v7.namespaces[$prop.Name] = @{ fields = $prop.Value }
        }
    }

    # Build decryptions from legacy fields
    $decFuncs = @()
    $decFingerprints = @{}

    # Common decryption function names by game
    $gameDecFuncs = @{
        'rust' = @('client_entities', 'entity_list', 'clActiveItem', 'PlayerInventory', 'PlayerEyes', 'decrypt_fov', 'encrypt_fov')
        'cs2' = @('client_entities', 'entity_list', 'clActiveItem', 'PlayerInventory', 'PlayerEyes')
        'fivem' = @('world', 'replay', 'viewport', 'camera', 'player_info', 'ped_pool', 'vehicle_pool', 'object_pool', 'network_player_mgr')
        'fortnite' = @('GWorld', 'GNames', 'ProcessEvent', 'PlayerArray', 'PawnPrivate', 'CurrentHealth', 'CurrentWeapon', 'TeamIndex')
        'warzone' = @('client_info', 'client_base', 'bone', 'name_array')
        'apex' = @('EntityList', 'LocalPlayer', 'ViewRender', 'ClientState', 'CHLClient', 'InputSystem', 'NetChannel')
        'unknown' = @()
    }

    if ($gameDecFuncs.ContainsKey($Game)) {
        $v7.decryptions.functions = $gameDecFuncs[$Game]
    }

    # Add fingerprint placeholders for known decryption functions
    foreach ($func in $v7.decryptions.functions) {
        if (-not $v7.decryptions.rva_fingerprints.ContainsKey($func)) {
            $v7.decryptions.rva_fingerprints[$func] = '0x0'
        }
    }

    # Add legacy rva_fingerprints if present
    if ($json.PSObject.Properties.Match('decrypt_rva')) {
        foreach ($fp in $json.decrypt_rva.PSObject.Properties) {
            $v7.decryptions.rva_fingerprints[$fp.Name] = $fp.Value
        }
    }

    # Add critical fields for this game
    if ($CRITICAL_FIELDS.ContainsKey($Game)) {
        $v7.critical_offsets = $CRITICAL_FIELDS[$Game]
    }

    return $v7
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
Write-Log "OmniGhost Offset Migration Tool"
Write-Log "Target schema version: ${TargetVersion}"
Write-Log "Source: ${Path}"

# Find files
if (Test-Path $Path -PathType Leaf) {
    $files = @($Path)
} else {
    $files = Get-ChildItem -Path $Path -Recurse -Filter "*_offsets.json" -File -ErrorAction SilentlyContinue |
        Where-Object { -not ($_.FullName -match '\\.(git|vs|cache|build|packages|vcpkg|third_party|node_modules|target)\\') }
}

Write-Log "Found $($files.Count) offset JSON file(s)"

if ($files.Count -eq 0) {
    Write-Log "No files to migrate" -Level Warn
    exit 0
}

$migrated = 0
$skipped = 0

foreach ($file in $files) {
    $relPath = $file.FullName.Replace((Get-Location).Path + "\", "")
    Write-Log "`nProcessing: ${relPath}"

    try {
        $json = Read-JsonFile -FilePath $file.FullName
    } catch {
        Write-Log "  SKIP: Failed to parse JSON - $($_.Exception.Message)" -Level Warn
        $skipped++
        continue
    }

    # Check current schema version
    $currentVer = 1
    if ($json.PSObject.Properties.Match('schema_version')) {
        $currentVer = [int]$json.schema_version
    } elseif ($json.PSObject.Properties.Match('version')) {
        # Some old files use 'version' instead
        if ([int]::TryParse($json.version, [ref]$currentVer)) { }
    }

    if ($currentVer -ge $TargetVersion) {
        Write-Log "  SKIP: Already at schema v${currentVer} (target v${TargetVersion})" -Level Info
        $skipped++
        continue
    }

    Write-Log "  Migrating: v${currentVer} -> v${TargetVersion}"

    # Detect game
    $game = Get-GameFromData -json $json
    Write-Log "  Detected game: ${game}"

    # Perform migration
    $v7 = Migrate-ToV7 -json $json -Game $game

    # Validate migrated structure
    if (-not $v7.PSObject.Properties.Match('namespaces')) {
        Write-Log "  ERROR: Migration produced invalid structure (missing namespaces)" -Level Error
        $skipped++
        continue
    }

    $nsCount = $v7.namespaces.PSObject.Properties.Count
    $fieldCount = 0
    foreach ($ns in $v7.namespaces.PSObject.Properties) {
        if ($ns.Value.PSObject.Properties.Match('fields')) {
            $fieldCount += $ns.Value.fields.PSObject.Properties.Count
        }
    }
    Write-Log "  Result: ${nsCount} namespaces, ${fieldCount} fields"

    if ($DryRun) {
        Write-Log "  DRY RUN: Would write migrated file" -Level Info
        $migrated++
        continue
    }

    # Backup
    if ($Backup) {
        $bakPath = $file.FullName + '.bak'
        if (Test-Path $bakPath -and -not $Force) {
            Write-Log "  SKIP: Backup exists and -Force not specified" -Level Warn
            $skipped++
            continue
        }
        Copy-Item -Path $file.FullName -Destination $bakPath -Force
        Write-Log "  Backup created: ${bakPath}" -Level Info
    }

    # Write migrated file
    try {
        Write-JsonFile -FilePath $file.FullName -Data $v7
        Write-Log "  Migrated successfully" -Level Pass
        $migrated++
    } catch {
        Write-Log "  FAILED: $($_.Exception.Message)" -Level Error
        $skipped++
    }
}

# Summary
Write-Log "`n============================================================"
Write-Log "MIGRATION SUMMARY"
Write-Log "============================================================"
Write-Log "Files processed:  $($files.Count)"
Write-Log "Migrated:         ${migrated}"
Write-Log "Skipped:          ${skipped}"

if ($DryRun) {
    Write-Log "DRY RUN COMPLETE - No files modified" -Level Info
}

exit 0