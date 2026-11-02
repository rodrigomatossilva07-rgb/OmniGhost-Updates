#include "rcs_controller.h"

#include <algorithm>
#include <cmath>

namespace CS2::RCS {
namespace {
float NormalizeDelta(float value) noexcept {
    if (!std::isfinite(value)) return 0.f;
    return std::clamp(std::remainder(value, 360.f), -180.f, 180.f);
}
}

void RCSController::Reset(ResetReason reason) noexcept {
    pawn_ = 0;
    weapon_ = 0;
    shots_ = 0;
    sampleMs_ = 0;
    previousPitch_ = previousYaw_ = 0.f;
    pendingX_ = pendingY_ = 0.f;
    initialized_ = false;
    lastReason_ = reason;
}

Correction RCSController::Update(const RCSConfig& config,
                                 const RecoilSample& sample,
                                 uint64_t nowMs) noexcept {
    const auto reject = [this](ResetReason reason) {
        Reset(reason);
        return Correction{0.f, 0.f, false, reason};
    };
    if (!config.enabled) return reject(ResetReason::Disabled);
    if (!sample.pawn) return reject(ResetReason::InvalidPawn);
    if (sample.health <= 0) return reject(ResetReason::Dead);
    if (!sample.sampleMs || nowMs < sample.sampleMs || nowMs - sample.sampleMs > 100u ||
        !std::isfinite(sample.pitch) || !std::isfinite(sample.yaw) ||
        std::fabs(sample.pitch) > 89.f || std::fabs(sample.yaw) > 180.f)
        return reject(ResetReason::InvalidSample);

    const int startShot = std::clamp(config.startAfterShots, 1, 32);
    const bool recentShot = sample.lastShotMs && nowMs >= sample.lastShotMs &&
        nowMs - sample.lastShotMs <= 180u;
    if (sample.shotsFired < startShot || !recentShot)
        return reject(ResetReason::NotFiring);

    ResetReason identityReason = ResetReason::None;
    if (initialized_) {
        if (pawn_ != sample.pawn) identityReason = ResetReason::PawnChanged;
        else if (weapon_ != sample.weaponDefinition) identityReason = ResetReason::WeaponChanged;
        else if (sample.shotsFired < shots_) identityReason = ResetReason::ShotCounterReset;
    }
    if (!initialized_ || identityReason != ResetReason::None) {
        Reset(identityReason);
        pawn_ = sample.pawn;
        weapon_ = sample.weaponDefinition;
        shots_ = sample.shotsFired;
        sampleMs_ = sample.sampleMs;
        previousPitch_ = sample.pitch;
        previousYaw_ = sample.yaw;
        initialized_ = true;
        return {0.f, 0.f, false, identityReason};
    }
    if (sample.sampleMs <= sampleMs_)
        return {0.f, 0.f, false, ResetReason::DuplicateSample};

    const float deltaPitch = NormalizeDelta(sample.pitch - previousPitch_);
    const float deltaYaw = NormalizeDelta(sample.yaw - previousYaw_);
    previousPitch_ = sample.pitch;
    previousYaw_ = sample.yaw;
    sampleMs_ = sample.sampleMs;
    shots_ = sample.shotsFired;
    if (!std::isfinite(deltaPitch) || !std::isfinite(deltaYaw) ||
        std::fabs(deltaPitch) > 25.f || std::fabs(deltaYaw) > 25.f)
        return reject(ResetReason::Discontinuity);

    const float weaponScale = std::clamp(config.ScaleForWeapon(sample.weaponDefinition), 0.f, 3.f);
    const float scale = std::clamp(config.recoilScale, .01f, 100.f) * weaponScale;
    pendingX_ += -deltaYaw * scale * std::clamp(config.horizontalStrength, 0.f, 2.f);


    // CS2 pitch decreases while recoil raises the view. Mouse Y uses the
    // opposite screen-space convention (positive moves down), therefore the
    // vertical compensation must negate the AimPunch delta.
    pendingY_ += -deltaPitch * scale * std::clamp(config.verticalStrength, 0.f, 2.f);

    const float response = 1.f - std::clamp(config.smoothing, 0.f, .95f);
    const float maxStep = std::clamp(config.maxCorrection, .25f, 127.f);
    const float x = std::clamp(pendingX_ * response, -maxStep, maxStep);
    const float y = std::clamp(pendingY_ * response, -maxStep, maxStep);
    if (!std::isfinite(x) || !std::isfinite(y))
        return reject(ResetReason::InvalidSample);
    pendingX_ = std::clamp(pendingX_ - x, -maxStep * 4.f, maxStep * 4.f);
    pendingY_ = std::clamp(pendingY_ - y, -maxStep * 4.f, maxStep * 4.f);
    lastReason_ = ResetReason::None;
    return {x, y, std::fabs(x) >= .001f || std::fabs(y) >= .001f,
            ResetReason::None, deltaPitch, deltaYaw, pendingX_, pendingY_};
}

const char* ToString(ResetReason reason) noexcept {
    switch (reason) {
    case ResetReason::None: return "active";
    case ResetReason::Disabled: return "disabled";
    case ResetReason::InvalidPawn: return "invalid pawn";
    case ResetReason::Dead: return "local player dead";
    case ResetReason::InvalidSample: return "invalid/stale recoil sample";
    case ResetReason::NotFiring: return "not firing";
    case ResetReason::WeaponChanged: return "weapon changed";
    case ResetReason::PawnChanged: return "pawn changed";
    case ResetReason::ShotCounterReset: return "shot counter reset";
    case ResetReason::DuplicateSample: return "duplicate sample";
    case ResetReason::Discontinuity: return "recoil discontinuity";
    }
    return "unknown";
}

}
