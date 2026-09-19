#pragma once
// Minimal UnifiedAim surface for Publish builds.
// Full implementation is in unified_aim.h.full.bak — restore when modules are fixed.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ImDrawList;

namespace Gameplay::UnifiedAim {

struct Vec2 { float x = 0.f, y = 0.f; };
struct Vec3 { float x = 0.f, y = 0.f, z = 0.f; };

struct UnifiedConfig {
    bool enabled = false;
    float fov = 5.f;
    float smooth = 1.f;
};

struct UnifiedState {
    bool active = false;
    bool can_fire = false;
    uint64_t current_target = 0;
    char debug[128] = {};
};

struct AimContext {
    float dt = 0.f;
};

struct AimResult {
    bool has_target = false;
    float delta_x = 0.f;
    float delta_y = 0.f;
};

class UnifiedAimbot {
public:
    UnifiedAimbot();
    ~UnifiedAimbot();

    void SetConfig(const UnifiedConfig& config) { config_ = config; }
    const UnifiedConfig& GetConfig() const { return config_; }
    UnifiedConfig& GetMutableConfig() { return config_; }

    const UnifiedState& GetState() const { return state_; }
    bool IsActive() const { return state_.active; }
    bool CanFire() const { return state_.can_fire; }
    uint64_t GetCurrentTarget() const { return state_.current_target; }

    AimResult Update(const AimContext&);
    void ForceTarget(uint64_t);
    void CancelTarget();
    void ApplyProfile(const std::string&);
    void SaveProfile(const std::string&);
    void LoadProfile(const std::string&);
    std::vector<std::string> GetAvailableProfiles() const;
    void OnShotFired();
    void OnWeaponReload();
    void OnWeaponSwap();
    void UpdateMovementAssist(float);
    const char* GetDebugStatus() const;
    void DrawDebug(ImDrawList*);

private:
    UnifiedConfig config_{};
    UnifiedState state_{};
};

UnifiedAimbot& GetUnifiedAimbot();
std::unique_ptr<UnifiedAimbot> CreateAimbotForGame(const char*);

} // namespace Gameplay::UnifiedAim