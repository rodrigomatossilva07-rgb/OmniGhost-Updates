[CmdletBinding()]
param([string]$ProjectDir = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $ProjectDir) { $ProjectDir = Split-Path -Parent $PSScriptRoot }
$cs2Root = Join-Path $ProjectDir 'src/games/Cs2'
# The acquisition runtime is intentionally composed from focused .cpp/.inl
# modules. Audit the complete implementation instead of silently regressing to
# checks against the small composition root only.
$sourceFiles = Get-ChildItem -LiteralPath $cs2Root -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.inl') -and $_.FullName -notlike '*\esp\cs2_esp.cpp' } |
    Sort-Object FullName
$source = ($sourceFiles | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
$espSource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src/games/Cs2/esp/cs2_esp.cpp'))

function Require-Match([string]$Text, [string]$Pattern, [string]$Reason) {
    if ($Text -notmatch $Pattern) { throw $Reason }
}

function Require-Condition([bool]$Condition, [string]$Reason) {
    if (-not $Condition) { throw $Reason }
}

$resolver = [regex]::Match($source,
    '(?s)static int ScatterResolvePawnHandles\(.*?\r?\n}\r?\n').Value
Require-Condition ($resolver.Length -gt 0) 'Pawn resolver not found.'
Require-Condition ($resolver -notmatch 'resolved\s*<\s*count\s*/\s*2') 'Sparse handle refresh still uses the total slot count.'
Require-Match $resolver '(?s)if \(alternate_stride.*?if \(IsUserPointer\(pawns\[i\]\)\) continue;' 'Alternate stride must preserve valid primary results.'
Require-Match $resolver 'static thread_local uintptr_t cachedChunks' 'Core and optional workers must not share mutable page caches.'
Require-Match $resolver 'mem\.ExecuteReadScatter\(scatter\)' 'Resolver must execute on its selected lane handle.'
Require-Match $source 'true, g_scatter_optional, client_base\);' 'Optional weapon resolution must use the optional worker scatter.'
Require-Condition ($source -notmatch 'unifiedConfig\s*=\s*CS2::UnifiedConfigAdapter::Get\(') 'Optional worker still reads mutable acquisition configuration.'

# A single changed handle among 64 slots used to select the alternate stride
# and erase the valid primary result. Check sparse and mixed refresh fixtures.
foreach ($count in @(10, 32, 64)) {
    $primary = New-Object UInt64[] $count
    $alternate = New-Object UInt64[] $count
    $primary[0] = 0x100000
    for ($i = 0; $i -lt $count; ++$i) {
        if ($primary[$i] -ge 0x10000) { continue }
        $primary[$i] = $alternate[$i]
    }
    Require-Condition ($primary[0] -eq 0x100000) "Valid sparse result lost among $count slots."
}
$primary = [UInt64[]]@(0x100000, 0)
$alternate = [UInt64[]]@(0, 0x200000)
for ($i = 0; $i -lt $primary.Length; ++$i) {
    if ($primary[$i] -ge 0x10000) { continue }
    $primary[$i] = $alternate[$i]
}
Require-Condition ($primary[0] -eq 0x100000 -and $primary[1] -eq 0x200000) 'Mixed stride recovery lost a valid entity.'

Require-Match $source '(?s)targets\[i\]\.scene \+ offsets\.m_vecAbsOrigin,\s*sampledOrigins\[bi\]' 'Bone pose and origin must be read in the same batch.'
Require-Match $source 'std::memcpy\(r.origin, sampledOrigins\[bi\], sizeof\(r.origin\)\)' 'Bone translation still uses the queued, older origin.'
Require-Match $source '!boneReadOk \|\| !eligible\[bi\] \|\| !IsFinitePosition\(sampledOrigins\[bi\]\)' 'Failed bone reads must not publish a fresh pose.'
Require-Match $source '(?s)joint\[0\] == 0.f && joint\[1\] == 0.f && joint\[2\] == 0.f\)\s*continue;' 'Translating a cached pose must preserve invalid zero joints.'

$invalid = [double[]]@(0, 0, 0)
$valid = [double[]]@(100, 200, 60)
$shift = [double[]]@(10, -5, 2)
foreach ($joint in @($invalid, $valid)) {
    if ($joint[0] -eq 0 -and $joint[1] -eq 0 -and $joint[2] -eq 0) { continue }
    for ($axis = 0; $axis -lt 3; ++$axis) { $joint[$axis] += $shift[$axis] }
}
Require-Condition ($invalid[0] -eq 0 -and $invalid[1] -eq 0 -and $invalid[2] -eq 0) 'Invalid limb became a drawable world point.'
Require-Condition ($valid[0] -eq 110 -and $valid[1] -eq 195 -and $valid[2] -eq 62) 'Valid cached limb did not follow the new origin.'

Require-Match $source 'const bool track_velocity = frame_config.esp_enabled \|\|' 'ESP motion must acquire velocity independently from aim prediction.'
Require-Match $source '(?s)corePlayer.distance = .*?\* 0\.0254f;' 'Distance must be converted once from Source units to metres.'
Require-Condition ([Math]::Abs(1000 * 0.0254 - 25.4) -lt 0.000001) 'Distance conversion fixture failed.'
Require-Match $source 'candidate_count < kMaxSlots' 'Core acquisition is capped by a user metadata budget.'
Require-Match $source 'cachedControllerGeneration != currentGen' 'Controller topology cache has no session reset.'
Require-Match $source 'SelectCameraSnapshot\(targets.front\(\).position_sample_ms\)' 'Visibility camera must match the queued world sample.'
Require-Match $source 'Visibility::CameraOrigin\(' 'Visibility must solve the world camera origin from the projection matrix.'
Require-Match $source 'collision.IsLoadedFor\(world->map_name\)' 'Optional visibility must read map identity from an immutable snapshot.'
Require-Condition ($source -notmatch 'Trajectory::Vec3\(cam->view_matrix\[12\]') 'Visibility still uses the projection row as camera position.'

$historyLocks = [regex]::Matches($source, 'lock\(g_history_mutex\)').Count
Require-Condition ($historyLocks -eq 3) 'Camera history publish, reset and selection must all lock the same mutex.'

$drawPlayers = [regex]::Match($espSource, '(?s)void DrawPlayers\(const Runtime& snapshot, const Config& settings\) \{.*?\n\}').Value
Require-Condition ($drawPlayers.Length -gt 0) 'CS2 presentation loop not found.'
Require-Condition ([regex]::Matches($drawPlayers, 'AcquireCameraSnapshot\(').Count -eq 1) 'Render must acquire the camera once per frame.'
Require-Condition ($drawPlayers -match 'presentationSampleMs = frameCamera->timestamp_ms' -and
    $drawPlayers -match 'presentationDeltaMs\(positionSampleMs\)' -and
    $drawPlayers -match 'presentationDeltaMs\(boneOriginSampleMs\)') 'Position and bones must be aligned to one camera sample.'
Require-Condition ($drawPlayers -match 'std::min\)\(presentationSampleMs - sampleMs, uint64_t\{35\}\)' -and
    $drawPlayers -match 'return sampleMs \? deltaMs : 0.f') 'Presentation offset must be signed, bounded, and reject unknown timestamps.'
Require-Match $espSource '(?s)\{CS2::BoneSlot::Pelvis, CS2::BoneSlot::HipLeft\}.*?\{CS2::BoneSlot::HipLeft, CS2::BoneSlot::KneeLeft\}' 'Left leg must use the acquired hip joint between pelvis and knee.'
Require-Match $espSource '(?s)\{CS2::BoneSlot::Pelvis, CS2::BoneSlot::HipRight\}.*?\{CS2::BoneSlot::HipRight, CS2::BoneSlot::KneeRight\}' 'Right leg must use the acquired hip joint between pelvis and knee.'
Require-Condition ($espSource -notmatch 's_feetSmooth|s_boxHeightSmooth|s_headYSmooth') 'Screen-space box smoothing still delays camera movement.'
Require-Match $drawPlayers 'WorldToScreen\(headWorld, viewMatrix, boxHead\)' 'Box anchor must be projected from world coordinates each frame.'
Require-Condition ($espSource -notmatch 'SelectCameraSnapshot\(') 'Per-item camera history selection remains in the renderer.'
foreach ($pair in @(@(1000, 1016, 16), @(1016, 1000, -16), @(1000, 1100, 35), @(1100, 1000, -35))) {
    $delta = [Math]::Max(-35, [Math]::Min(35, [double]$pair[1] - [double]$pair[0]))
    Require-Condition ($delta -eq $pair[2]) "Signed presentation offset fixture failed for $($pair[0]) → $($pair[1]) ms."
}

Write-Host '[PASS] CS2 acquisition and presentation invariants/fixtures (no C++ build or DMA).'
