[CmdletBinding()]
param([string]$ProjectDir = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $ProjectDir) { $ProjectDir = Split-Path -Parent $PSScriptRoot }
$cs2Root = Join-Path $ProjectDir 'src/games/Cs2'



$sourceFiles = Get-ChildItem -LiteralPath $cs2Root -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.inl') -and $_.FullName -notlike '*\esp\cs2_esp.cpp' } |
    Sort-Object FullName
$source = ($sourceFiles | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
$espSource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src/games/Cs2/esp/cs2_esp.cpp'))
$cameraSource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src/games/Cs2/runtime/cs2_camera.cpp'))
$runtimeSource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src/games/Cs2/runtime/cs2_runtime.cpp'))
$bonesSource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src/games/Cs2/bones/cs2_bones_pipeline.inl'))
$memoryIoSource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'third_party/DMALibrary/Memory/MemoryIO.cpp'))
$memoryLifecycleSource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'third_party/DMALibrary/Memory/MemoryLifecycle.cpp'))
$commonBuildProps = [IO.File]::ReadAllText((Join-Path $ProjectDir 'config/build/OmniGhost.Common.props'))
$windowRuntimeSource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src/window/window_runtime.cpp'))
$offsetSnapshot = Get-Content -Raw -LiteralPath (Join-Path $ProjectDir 'data/cs2_offsets.json') | ConvertFrom-Json

function Require-Match([string]$Text, [string]$Pattern, [string]$Reason) {
    if ($Text -notmatch $Pattern) { throw $Reason }
}

function Require-Condition([bool]$Condition, [string]$Reason) {
    if (-not $Condition) { throw $Reason }
}

Require-Match $memoryIoSource 'DmaSpinLockGuard\(DmaSpinLock& lock, AdoptLockTag\)' 'The non-blocking DMA lane has no adopt-lock guard.'
Require-Match $memoryIoSource 'DmaSpinLockGuard\(g_dmaSpinLock, DmaSpinLockGuard::AdoptLockTag\{\}\)' 'TryAcquireDmaTransportLock still risks locking the same transport twice.'
Require-Match $memoryLifecycleSource 'if \(IsCancellationRequested\(\)\)' 'DMA shutdown cancellation must reject new data-plane calls.'
Require-Match $memoryIoSource '(?s)auto dmaSpinLockGuard = AcquireDmaTransportLock\(\);.*?if \(IsCancellationRequested\(\)\) \{\s*ReleaseScatterUse\(handle\);\s*return false;' 'A queued scatter call can still start after shutdown cancellation.'
Require-Match $commonBuildProps '<OmniGhostDisableVmmSymbols[^>]*>true</OmniGhostDisableVmmSymbols>' 'VMM symbols must be disabled by default to prevent external symbol-store UI.'

$resolver = [regex]::Match($source,
    '(?s)static int ScatterResolvePawnHandles\(.*?\r?\n}\r?\n').Value
Require-Condition ($resolver.Length -gt 0) 'Pawn resolver not found.'
Require-Condition ($resolver -notmatch 'resolved\s*<\s*count\s*/\s*2') 'Sparse handle refresh still uses the total slot count.'
Require-Match $resolver '(?s)if \(alternate_stride.*?if \(IsUserPointer\(pawns\[i\]\)\) continue;' 'Alternate stride must preserve valid primary results.'
Require-Match $resolver 'static thread_local uintptr_t cachedChunks' 'Core and optional workers must not share mutable page caches.'
Require-Match $resolver 'mem\.ExecuteReadScatter\(scatter\)' 'Resolver must execute on its selected lane handle.'
Require-Match $source 'true, g_scatter_optional, client_base\);' 'Optional weapon resolution must use the optional worker scatter.'
Require-Condition ($source -notmatch 'unifiedConfig\s*=\s*CS2::UnifiedConfigAdapter::Get\(') 'Optional worker still reads mutable acquisition configuration.'



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
Require-Condition ($historyLocks -eq 4) 'Camera history publish, reset, selection and prediction must all lock the same mutex.'
Require-Condition ($cameraSource -notmatch 'newest\.view_matrix\[i\].*previous\.view_matrix') 'Projection matrix coefficients must never be extrapolated independently.'
Require-Match $cameraSource 'return newest;' 'Presentation must use the newest coherent camera matrix.'
Require-Match $runtimeSource 'previousAverage \+ \(interval - previousAverage\) \* 0\.20f' 'Published acquisition rate must use a stable moving average.'
Require-Match $source 'targetPeriodUs - workUs' 'The acquisition scheduler still adds work time on top of its target period.'
Require-Match $source '(?s)Keep the projection matrix current before the largest position batch.*?GiveCameraPriorityBeforeOptionalDma\(\);\s*if \(!mem\.ExecuteReadScatter\(g_scatter_full\)\)' 'The core position batch can still starve the camera lane.'
Require-Match $source "camera_period = std::chrono::microseconds\(4'167\)" 'The camera lane must retain its independent 240 Hz target.'
Require-Match $source "targetPeriodUs = cfgLease->performance_mode \? 12'000 : 6'944" 'The active world snapshot must target 144 Hz independently from presentation.'
Require-Match $source "level >= 3\) targetPeriodUs = \(std::max\)\(targetPeriodUs, 22'000\)" 'DMA pressure fallback must not collapse active acquisition to 25 Hz.'
Require-Match $windowRuntimeSource "kFramePeriod = std::chrono::microseconds\(6'944\)" 'The overlay must be capped at a stable 144 Hz for high-refresh presentation.'
Require-Match $source 'CS2::SampleClockMs\(\)' 'World, bone and camera samples must share the high-resolution monotonic clock.'
Require-Match $windowRuntimeSource 'std::this_thread::sleep_until\(next_frame_deadline_\)' 'The overlay limiter must use an absolute deadline to avoid cumulative drift.'
Require-Match $windowRuntimeSource 'timeBeginPeriod\(1\) == TIMERR_NOERROR' 'High-rate acquisition requires 1 ms Windows timer resolution.'
Require-Match $windowRuntimeSource 'timeEndPeriod\(1\)' 'Windows timer resolution must be restored during shutdown.'
Require-Condition ($bonesSource -notmatch 'BoneJointSnapshot48|s_use48Stride') 'Bone acquisition must not switch to an unvalidated speculative stride.'
Require-Match $bonesSource 'validBoneCount >= 8' 'A safe partial pose must remain drawable when one optional limb is unavailable.'
Require-Match $source '(?s)confirmed_dead\) \{\s*aliveIt->second\.confirmed_dead = false;.*?bone_cache\.erase\(pawn\)' 'Respawn must immediately release the dead-player bone cache gate.'

$expectedBoneMapping = [ordered]@{
    Head = '0x7'; Neck = '0x6'; SpineUpper = '0x17'; SpineMiddle = '0x4';
    SpineLower = '0x3'; Pelvis = '0x1'; ClavicleLeft = '0x8'; ShoulderLeft = '0x9';
    ElbowLeft = '0xA'; HandLeft = '0xB'; ClavicleRight = '0xC'; ShoulderRight = '0xD';
    ElbowRight = '0xE'; HandRight = '0xF'; HipLeft = '0x11'; KneeLeft = '0x12';
    AnkleLeft = '0x13'; HipRight = '0x14'; KneeRight = '0x15'; AnkleRight = '0x16'
}
foreach ($entry in $expectedBoneMapping.GetEnumerator()) {
    Require-Condition ($offsetSnapshot.bone_mapping.($entry.Key) -eq $entry.Value) `
        "CS2 anatomical bone mapping is stale at $($entry.Key)."
}

$drawPlayers = [regex]::Match($espSource, '(?s)void DrawPlayers\(const Runtime& snapshot, const Config& settings\) \{.*?\n\}').Value
Require-Condition ($drawPlayers.Length -gt 0) 'CS2 presentation loop not found.'
Require-Condition ([regex]::Matches($drawPlayers, 'PredictCameraSnapshot\(nowMs\)').Count -eq 1) 'Render must obtain one latency-compensated camera snapshot per frame.'
Require-Condition ($drawPlayers -match 'presentationSampleMs = frameCamera\.timestamp_ms' -and
    $drawPlayers -match 'presentationDeltaMs\(positionSampleMs\)' -and
    $drawPlayers -match 'presentationDeltaMs\(boneOriginSampleMs\)') 'Position and bones must be aligned to one camera sample.'
Require-Condition ($drawPlayers -match 'std::min\)\(presentationSampleMs - sampleMs, uint64_t\{35\}\)' -and
    $drawPlayers -match 'return sampleMs \? deltaMs : 0.f') 'Presentation offset must be signed, bounded, and reject unknown timestamps.'
Require-Match $espSource '(?s)\{CS2::BoneSlot::Pelvis, CS2::BoneSlot::HipLeft\}.*?\{CS2::BoneSlot::HipLeft, CS2::BoneSlot::KneeLeft\}' 'Left leg must use the acquired hip joint between pelvis and knee.'
Require-Match $espSource '(?s)\{CS2::BoneSlot::Pelvis, CS2::BoneSlot::HipRight\}.*?\{CS2::BoneSlot::HipRight, CS2::BoneSlot::KneeRight\}' 'Right leg must use the acquired hip joint between pelvis and knee.'
Require-Condition ($espSource -notmatch 's_feetSmooth|s_boxHeightSmooth|s_headYSmooth') 'Screen-space box smoothing still delays camera movement.'
Require-Match $drawPlayers 'WorldSegmentToScreenClipped\(headWorld, predictedPosition, viewMatrix, boxHead, feet\)' 'Close-range boxes must clip the head-to-feet segment against the camera near plane.'
Require-Match $drawPlayers 'projectedBones < 2 \|\| bottom - top < 8\.f' 'Point-blank ESP must recover from projectable bone bounds when the body segment crosses the camera.'
Require-Match $espSource 'settings\.visibility_colors && player\.visibility_known' 'Visibility colors must work independently from visible-only filtering.'
Require-Match $drawPlayers 'visiblePlayersOnly && \(!player\.visibility_known \|\| !player\.visible\)' 'Visible-only mode must hide unknown and occluded players when colors are disabled.'
Require-Match $espSource 'AddCircle\(head,[\s\S]{0,180}?visibilityColor\(settings\.col_head\)' 'The head circle does not use the resolved visible/occluded color.'
Require-Match $source 'unifiedFlags\.visibility_colors = frame_config\.visibility_colors' 'ESP visibility colors do not request collision visibility data.'
Require-Match $source 'unifiedFlags\.visible_check = frame_config\.visible_check' 'ESP visibility checking is not propagated to optional acquisition.'
Require-Match $source 'frame_config\.trigger_enabled && frame_config\.trigger_visibility_check' 'Trigger visibility checking does not request spotted-state acquisition independently.'
Require-Match $source 'frame_config\.aim_visible_bones_only \|\| frame_config\.aim_auto_bone' 'Automatic bone selection must request per-bone visibility data.'
Require-Match $bonesSource 'std::array<std::size_t, 7> kAimSlots' 'Per-bone visibility acquisition must cover all seven aim points.'
Require-Match $bonesSource 'BoneSlot::Pelvis[\s\S]{0,180}?BoneSlot::KneeLeft[\s\S]{0,180}?BoneSlot::KneeRight' 'Pelvis and both legs are missing from aim visibility acquisition.'
Require-Match $source 'if \(BoneVisible\(p, preferred\)\) return preferred;' 'Automatic bone selection does not validate visibility of its preferred point.'
Require-Match $source 'alternative < 7; \+\+alternative' 'Automatic bone selection cannot recover to another visible point inside the FOV.'
Require-Condition ($espSource -notmatch 'SelectCameraSnapshot\(') 'Per-item camera history selection remains in the renderer.'
Require-Match $drawPlayers 'nowMs - worldSampleMs > 1000u' 'A short DMA stall still blanks the complete ESP frame.'
Require-Match $drawPlayers 'if \(!stable\.name\[0\] && cached\.name\[0\]\)' 'A partial optional scan can still blank a cached player name.'
Require-Match $bonesSource 'r\.full_bones_ok = completePose' 'Partial bone samples must retain an explicit completeness flag.'
$failedCoreBlock = [regex]::Match($source,
    '(?s)if \(candidate_count > 0 && validCoreSamples == 0\) \{.*?g_publish_runtime_this_scan = false;\s*return;\s*\}').Value
Require-Condition ($failedCoreBlock.Length -gt 0 -and
    $failedCoreBlock -notmatch 'runtime\.players\.clear\(\)|PublishRuntimeSnapshot\(\)') 'A failed core batch still publishes an empty player list.'
foreach ($pair in @(@(1000, 1016, 16), @(1016, 1000, -16), @(1000, 1100, 35), @(1100, 1000, -35))) {
    $delta = [Math]::Max(-35, [Math]::Min(35, [double]$pair[1] - [double]$pair[0]))
    Require-Condition ($delta -eq $pair[2]) "Signed presentation offset fixture failed for $($pair[0]) → $($pair[1]) ms."
}

Write-Host '[PASS] CS2 acquisition and presentation invariants/fixtures (no C++ build or DMA).'
