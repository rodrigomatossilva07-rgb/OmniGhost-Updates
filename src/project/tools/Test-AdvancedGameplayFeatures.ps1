[CmdletBinding()]
param([string]$ProjectDir = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $ProjectDir) { $ProjectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')) }

function Read-Source([string]$Relative) {
    [IO.File]::ReadAllText((Join-Path $ProjectDir $Relative))
}
function Require-Match([string]$Text, [string]$Pattern, [string]$Reason) {
    if ($Text -notmatch $Pattern) { throw $Reason }
}
function Require-Absent([string]$Text, [string]$Pattern, [string]$Reason) {
    if ($Text -match $Pattern) { throw $Reason }
}

$cs2Config = Read-Source 'src/games/Cs2/config/cs2_config.h'
$cs2Aim = Read-Source 'src/games/Cs2/aimbot/cs2_aim.cpp'
$cs2Esp = Read-Source 'src/games/Cs2/esp/cs2_esp.cpp'
$cs2Visibility = Read-Source 'src/games/Cs2/visibility/cs2_visibility_worker.cpp'
$cs2VisibilityHeader = Read-Source 'src/games/Cs2/visibility/cs2_visibility_worker.h'
$cs2Persistence = Read-Source 'src/config/config_games.cpp'
$configCore = Read-Source 'src/config/config_core.cpp'
$fivemAim = Read-Source 'src/games/Fivem/aimbot/aimbot.cpp'
$fivemEsp = Read-Source 'src/games/Fivem/esp/esp.cpp'

Require-Match $cs2Config 'EspLayoutAnchor' 'CS2 ESP builder anchors are missing.'
Require-Match $cs2Esp 'DrawOffscreenArrow' 'CS2 offscreen arrows are not connected to the real renderer.'
Require-Match $cs2Esp 'settings\.c4_carrier_esp\s*&&\s*!snapshot\.bomb\.planted' 'C4 carrier rendering does not respect the planted state.'
Require-Match $cs2Esp 'weapon_def\s*==\s*49' 'C4 carrier rendering is not based on validated weapon metadata.'
Require-Match $cs2Esp '##cs2_active_features' 'CS2 active feature watermark is missing.'
Require-Match $cs2Aim 'std::lerp\(cfg\.aim_fov_curve_value' 'CS2 distance FOV curve is not interpolated.'
Require-Match $cs2Aim 'bone_visibility_known_mask' 'CS2 aim does not reject unknown per-bone visibility.'
Require-Match $cs2Aim 'cfg\.aim_bone_priority' 'CS2 bone priority is not used by target selection.'
Require-Match ($cs2VisibilityHeader + $cs2Visibility) 'point_known_mask' 'CS2 visibility worker does not publish per-point knowledge.'
Require-Match ($cs2VisibilityHeader + $cs2Visibility) 'point_visible_mask' 'CS2 visibility worker does not publish per-point visibility.'
Require-Match $cs2Persistence 'cs2\.aim_fov_curve_distance\.' 'CS2 FOV curve is not persisted.'
Require-Match $cs2Persistence 'cs2\.layout_name_anchor' 'CS2 ESP layout is not persisted.'
Require-Match $configCore 'WebsiteRequest\("POST", "/api/launcher/configs"' 'Cloud presets are not sent through the authenticated website session.'
Require-Match $configCore 'DeserializeAll\(\*payload\)' 'Downloaded cloud presets are not validated before use.'
Require-Match $configCore 'DuplicateConfigFile' 'Local preset duplication is missing.'
Require-Match $fivemAim 'EffectiveFov' 'FiveM dynamic FOV is not connected to target selection.'
Require-Match $fivemAim 'sticky_aim' 'FiveM sticky target selection is missing.'
Require-Match $fivemAim 'trigger_burst' 'FiveM trigger burst timing is missing.'
Require-Match $fivemEsp 'DrawOffscreenArrow' 'FiveM offscreen arrows are not connected to the real renderer.'
Require-Match $fivemEsp '##fivem_active_features' 'FiveM active features panel is missing.'
Require-Absent $cs2Aim 'bone_visibility_known_mask.*?\|=' 'CS2 aim must not manufacture visibility knowledge.'

Write-Host '[PASS] Advanced CS2/FiveM gameplay features and config integration invariants.'
