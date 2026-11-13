#pragma once
#include <cstdint>

namespace OmniGhost::Constants {






inline constexpr int kFiveMMaxPeds = 110;


inline constexpr int kCs2MaxPlayers = 64;


inline constexpr int kWarzoneMaxEntities = 200;






inline constexpr uint32_t kEntityHandlePageMask = 0x7FFFu;
inline constexpr uint32_t kEntityHandleIndexMask = 0x1FFu;
inline constexpr int kEntityHandlePageShift = 9;


inline constexpr uintptr_t kCs2EntityIdentityStride = 0x70;
inline constexpr uintptr_t kCs2EntityIdentityStrideLegacy = 0x78;
inline constexpr uintptr_t kCs2EntityPageSize = 0x200;
inline constexpr uintptr_t kCs2EntityPageTableOffset = 0x10;






inline constexpr uintptr_t kCs2BoneArrayOffsetInModelState = 0x80;


inline constexpr uintptr_t kFiveMBoneMatrixOffset = 0x60;
inline constexpr uintptr_t kFiveMBoneFrag3258 = 0xFF8;
inline constexpr uintptr_t kFiveMBoneFrag3751 = 0x12B8;
inline constexpr uintptr_t kFiveMBoneSkelO1 = 0x78;
inline constexpr uintptr_t kFiveMBoneSkelStride = 0x40;
inline constexpr uintptr_t kFiveMBoneSkelTrans = 0xC;


inline constexpr size_t kBoneSlotCount = 20;
inline constexpr uint32_t kBoneFullMask = (1u << kBoneSlotCount) - 1;





inline constexpr float kLodDistFull = 30.0f;
inline constexpr float kLodDistHigh = 80.0f;
inline constexpr float kLodDistMedium = 150.0f;
inline constexpr float kLodDistLow = 200.0f;


inline constexpr float kDefaultMaxEspDistance = 200.0f;






inline constexpr int kCameraIntervalMs = 16;


inline constexpr int kFullScanIntervalMs = 18;


inline constexpr int kBonesIntervalMs = 24;


inline constexpr int kWeaponIntervalMs = 200;
inline constexpr int kWeaponFallbackIntervalMs = 3000;
inline constexpr int kNameIntervalMs = 1000;
inline constexpr int kTeamIntervalMs = 500;
inline constexpr int kEntityListIntervalMs = 1000;
inline constexpr int kEntityListFullProbeIntervalMs = 15000;


inline constexpr int kBoneReliabilityHoldMs = 160;






inline constexpr float kPressureEnterL1Work = 35.0f;
inline constexpr float kPressureEnterL1P95 = 32.0f;
inline constexpr float kPressureEnterL2Work = 55.0f;
inline constexpr float kPressureEnterL2P95 = 50.0f;
inline constexpr float kPressureEnterL3Work = 80.0f;
inline constexpr float kPressureEnterL3P95 = 70.0f;


inline constexpr float kPressureExitL3Work = 30.0f;
inline constexpr float kPressureExitL3P95 = 38.0f;
inline constexpr float kPressureExitL2Work = 26.0f;
inline constexpr float kPressureExitL2P95 = 30.0f;
inline constexpr float kPressureExitL1Work = 20.0f;
inline constexpr float kPressureExitL1P95 = 24.0f;


inline constexpr int kPressureHealthyScansToRecover = 12;


inline constexpr int kDelayPressure3 = 40;
inline constexpr int kDelayPressure2 = 28;
inline constexpr int kDelayPressure1 = 22;
inline constexpr int kDelayBase = 18;
inline constexpr int kDelayPerformance = 24;





inline constexpr float kOptionalBudgetMs = 6.0f;
inline constexpr float kOptionalBudgetPressure1 = 4.5f;
inline constexpr float kOptionalBudgetPressure2 = 3.5f;
inline constexpr float kOptionalBudgetPressure3 = 2.0f;





inline constexpr size_t kSnapshotSlots = 3;
inline constexpr size_t kRuntimeSnapshotSlots = 4;


inline constexpr uint64_t kBoneVisualMaxAgeMs = 160;
inline constexpr float kSnapshotMaxAgeMs = 120.0f;
inline constexpr float kCameraMaxAgeMs = 80.0f;





inline constexpr int kConfigSchemaVersion = 3;
inline constexpr size_t kMaxConfigName = 96;
inline constexpr size_t kMaxConfigBytes = 1024 * 1024;





namespace DmaPri {
    inline constexpr int OtherMetadata = 10;
    inline constexpr int WeaponNameTeam = 30;
    inline constexpr int Health = 50;
    inline constexpr int Bones = 60;
    inline constexpr int Position = 80;
    inline constexpr int Camera = 100;
}





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





inline constexpr int kUpdaterCheckIntervalHours = 24;





inline bool IsUserPointer(uintptr_t value) noexcept {
#if defined(_WIN64)

    return value >= 0x10000ull && value <= 0x7FFFFFFFFFFFFFull;
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

}
