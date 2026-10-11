#pragma once
#include <cstdint>

namespace OmniGhost::Constants {

// ============================================================
// Entity Limits
// ============================================================

// FiveM: Maximum peds tracked per frame (engine limit + buffer)
inline constexpr int kFiveMMaxPeds = 110;

// CS2: Maximum players (10v10 + spectators)
inline constexpr int kCs2MaxPlayers = 64;

// Warzone: Maximum entities
inline constexpr int kWarzoneMaxEntities = 200;

// ============================================================
// Engine Handle Decoding (Source 2 / IW Engine)
// ============================================================

// Entity handle: [page(13 bits) | index(9 bits)]
inline constexpr uint32_t kEntityHandlePageMask = 0x7FFFu;
inline constexpr uint32_t kEntityHandleIndexMask = 0x1FFu;
inline constexpr int kEntityHandlePageShift = 9;

// CS2 Entity Identity stride (0x70 for CEntityIdentity, 0x78 legacy fallback)
inline constexpr uintptr_t kCs2EntityIdentityStride = 0x70;
inline constexpr uintptr_t kCs2EntityIdentityStrideLegacy = 0x78;
inline constexpr uintptr_t kCs2EntityPageSize = 0x200;
inline constexpr uintptr_t kCs2EntityPageTableOffset = 0x10;

// ============================================================
// Bone System
// ============================================================

// Bone array offset within model state (CS2)
inline constexpr uintptr_t kCs2BoneArrayOffsetInModelState = 0x80;

// FiveM bone matrix offsets (varies by build)
inline constexpr uintptr_t kFiveMBoneMatrixOffset = 0x60;      // CEntity visual matrix (fallback)
inline constexpr uintptr_t kFiveMBoneFrag3258 = 0xFF8;         // frag_inst / bone_matrix on 3258
inline constexpr uintptr_t kFiveMBoneFrag3751 = 0x12B8;        // 3751+
inline constexpr uintptr_t kFiveMBoneSkelO1 = 0x78;
inline constexpr uintptr_t kFiveMBoneSkelStride = 0x40;
inline constexpr uintptr_t kFiveMBoneSkelTrans = 0xC;

// Bone slot count (20 slots for full skeleton)
inline constexpr size_t kBoneSlotCount = 20;
inline constexpr uint32_t kBoneFullMask = (1u << kBoneSlotCount) - 1; // 0x0FFFFF

// ============================================================
// ESP Distance LOD Thresholds (meters)
// ============================================================

inline constexpr float kLodDistFull = 30.0f;    // All 20 bones
inline constexpr float kLodDistHigh = 80.0f;    // 5 bones (head, neck, shoulders, pelvis)
inline constexpr float kLodDistMedium = 150.0f; // 3 bones (head, neck, pelvis)
inline constexpr float kLodDistLow = 200.0f;    // 1 bone (head only)

// Default ESP max distance
inline constexpr float kDefaultMaxEspDistance = 200.0f;

// ============================================================
// DMA Acquisition Intervals (ms)
// ============================================================

// Camera: highest priority, display-rate aligned
inline constexpr int kCameraIntervalMs = 16;    // 62.5 Hz

// Core world: position/health/velocity
inline constexpr int kFullScanIntervalMs = 18;  // ~55 Hz

// Bones: LOD-based, medium frequency
inline constexpr int kBonesIntervalMs = 24;     // ~42 Hz

// Weapons/Names/Flags: low frequency
inline constexpr int kWeaponIntervalMs = 200;
inline constexpr int kWeaponFallbackIntervalMs = 3000;
inline constexpr int kNameIntervalMs = 1000;
inline constexpr int kTeamIntervalMs = 500;
inline constexpr int kEntityListIntervalMs = 1000;
inline constexpr int kEntityListFullProbeIntervalMs = 15000;

// Bone reliability hold (matches renderer visual age ceiling)
inline constexpr int kBoneReliabilityHoldMs = 160;

// ============================================================
// Pressure-Based Scheduling Thresholds
// ============================================================

// Enter thresholds (spike climbs fast)
inline constexpr float kPressureEnterL1Work = 35.0f;
inline constexpr float kPressureEnterL1P95 = 32.0f;
inline constexpr float kPressureEnterL2Work = 55.0f;
inline constexpr float kPressureEnterL2P95 = 50.0f;
inline constexpr float kPressureEnterL3Work = 80.0f;
inline constexpr float kPressureEnterL3P95 = 70.0f;

// Exit thresholds (hysteresis - sustained healthy window)
inline constexpr float kPressureExitL3Work = 30.0f;
inline constexpr float kPressureExitL3P95 = 38.0f;
inline constexpr float kPressureExitL2Work = 26.0f;
inline constexpr float kPressureExitL2P95 = 30.0f;
inline constexpr float kPressureExitL1Work = 20.0f;
inline constexpr float kPressureExitL1P95 = 24.0f;

// Recovery requires sustained healthy scans
inline constexpr int kPressureHealthyScansToRecover = 12; // ~250ms

// Delay stretch at pressure levels (ms)
inline constexpr int kDelayPressure3 = 40;
inline constexpr int kDelayPressure2 = 28;
inline constexpr int kDelayPressure1 = 22;
inline constexpr int kDelayBase = 18;
inline constexpr int kDelayPerformance = 24;

// ============================================================
// Optional Lane Budget
// ============================================================

inline constexpr float kOptionalBudgetMs = 6.0f;    // Base budget
inline constexpr float kOptionalBudgetPressure1 = 4.5f; // 75%
inline constexpr float kOptionalBudgetPressure2 = 3.5f; // ~58%
inline constexpr float kOptionalBudgetPressure3 = 2.0f; // Minimal

// ============================================================
// Snapshot Exchange
// ============================================================

inline constexpr size_t kSnapshotSlots = 3;        // Min slots for SP/MC
inline constexpr size_t kRuntimeSnapshotSlots = 4; // Extra slot for CS2

// Visual age ceilings (ms)
inline constexpr uint64_t kBoneVisualMaxAgeMs = 160;
inline constexpr int kBoneReliabilityHoldMs = 160;
inline constexpr float kSnapshotMaxAgeMs = 120.0f;
inline constexpr float kCameraMaxAgeMs = 80.0f;

// ============================================================
// Config Schema
// ============================================================

inline constexpr int kConfigSchemaVersion = 3;
inline constexpr size_t kMaxConfigName = 96;
inline constexpr size_t kMaxConfigBytes = 1024 * 1024; // 1MB

// ============================================================
// DMA Priority Levels (thread-local)
// ============================================================

namespace DmaPri {
    inline constexpr int OtherMetadata = 10;    // Flags, dropped items, spectators
    inline constexpr int WeaponNameTeam = 30;   // Weapon names, team, ammo
    inline constexpr int Health = 50;           // Health (often batched with position)
    inline constexpr int Bones = 60;            // Bone matrices
    inline constexpr int Position = 80;         // Position/velocity core world
    inline constexpr int Camera = 100;          // View matrix - highest priority
} // namespace DmaPri

// ============================================================
// Timeouts (ms)
// ============================================================

inline constexpr int kDmaInitTimeoutMs = 30000;
inline constexpr int kProcInfoTimeoutMs = 100000;
inline constexpr int kProcInfoZeroStallMs = 15000;
inline constexpr int kProcInfoHeartbeatMs = 5000;
inline constexpr int kProcInfoPollMs = 500;
inline constexpr int kProcInfoSettleMs = 1500;
inline constexpr int kVmmMaintenanceDrainMs = 2500;
inline constexpr int kDeviceReopenSettleMs = 250;
inline constexpr int kReadTimeoutMs = 5000;
inline constexpr int kScatterTimeoutMs = 5000;

// ============================================================
// Network/Updater
// ============================================================

inline constexpr int kUpdaterCheckIntervalHours = 24;
inline constexpr int kCloudflareStartTimeoutMs = 5000;
inline constexpr int kWebRadarPort = 8080;

// ============================================================
// Validation Helpers
// ============================================================

inline bool IsUserPointer(uintptr_t value) noexcept {
#if defined(_WIN64)
    return value >= 0x10000ull && value <= 0x00007FFFFFFFFFFFull;
#else
    return value >= 0x10000u && value <= 0x7FFF0000u;
#endif
}

inline bool IsValidEntityHandle(uint32_t handle) noexcept {
    return handle != 0 && handle != 0xFFFFFFFFu;
}

inline bool IsValidBoneIndex(int index) noexcept {
    return index >= 0 && index < static_cast<int>(kBoneSlotCount);
}

inline bool IsFiniteVec3(float x, float y, float z) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

} // namespace OmniGhost::Constants