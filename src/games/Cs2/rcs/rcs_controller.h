#pragma once

#include "rcs_config.h"
#include <cstdint>

namespace CS2::RCS {

struct RecoilSample {
    uintptr_t pawn = 0;
    int health = 0;
    int weaponDefinition = 0;
    int shotsFired = 0;
    float pitch = 0.f;
    float yaw = 0.f;
    uint64_t sampleMs = 0;
    uint64_t lastShotMs = 0;
};

enum class ResetReason {
    None,
    Disabled,
    InvalidPawn,
    Dead,
    InvalidSample,
    NotFiring,
    WeaponChanged,
    PawnChanged,
    ShotCounterReset,
    DuplicateSample,
    Discontinuity
};

struct Correction {
    float x = 0.f;
    float y = 0.f;
    bool produced = false;
    ResetReason reason = ResetReason::None;
    float deltaPitch = 0.f;
    float deltaYaw = 0.f;
    float pendingX = 0.f;
    float pendingY = 0.f;
};

class RCSController {
public:
    [[nodiscard]] Correction Update(const RCSConfig& config,
                                    const RecoilSample& sample,
                                    uint64_t nowMs) noexcept;
    void Reset(ResetReason reason = ResetReason::None) noexcept;
    [[nodiscard]] ResetReason LastReason() const noexcept { return lastReason_; }

private:
    uintptr_t pawn_ = 0;
    int weapon_ = 0;
    int shots_ = 0;
    uint64_t sampleMs_ = 0;
    float previousPitch_ = 0.f;
    float previousYaw_ = 0.f;
    float pendingX_ = 0.f;
    float pendingY_ = 0.f;
    bool initialized_ = false;
    ResetReason lastReason_ = ResetReason::None;
};

[[nodiscard]] const char* ToString(ResetReason reason) noexcept;

}
