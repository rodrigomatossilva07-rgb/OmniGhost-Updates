// Minimal stub implementation for UnifiedAim (matches minimal unified_aim.h)
// Used for Publish builds where full implementation is not included

#include "unified_aim.h"

namespace Gameplay::UnifiedAim {

UnifiedAimbot::UnifiedAimbot() = default;
UnifiedAimbot::~UnifiedAimbot() = default;

AimResult UnifiedAimbot::Update(const AimContext&) {
    return {};
}

void UnifiedAimbot::ForceTarget(uint64_t) {}
void UnifiedAimbot::CancelTarget() {}
void UnifiedAimbot::ApplyProfile(const std::string&) {}
void UnifiedAimbot::SaveProfile(const std::string&) {}
void UnifiedAimbot::LoadProfile(const std::string&) {}

std::vector<std::string> UnifiedAimbot::GetAvailableProfiles() const {
    return {};
}

void UnifiedAimbot::OnShotFired() {}
void UnifiedAimbot::OnWeaponReload() {}
void UnifiedAimbot::OnWeaponSwap() {}
void UnifiedAimbot::UpdateMovementAssist(float) {}

const char* UnifiedAimbot::GetDebugStatus() const {
    return state_.debug;
}

void UnifiedAimbot::DrawDebug(ImDrawList*) {}

UnifiedAimbot& GetUnifiedAimbot() {
    static UnifiedAimbot instance;
    return instance;
}

std::unique_ptr<UnifiedAimbot> CreateAimbotForGame(const char*) {
    return std::make_unique<UnifiedAimbot>();
}

} // namespace Gameplay::UnifiedAim