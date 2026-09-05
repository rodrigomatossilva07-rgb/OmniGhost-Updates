#include "animations.h"
#include "../config/app_settings.h"
#include <cmath>
#include <unordered_map>

namespace CyberAnimations {

    namespace {
        std::unordered_map<ImGuiID, float> g_hover;
    }

    void SetHover(ImGuiID id, bool hovered) {
        float& progress = g_hover[id];
        const float target = hovered ? 1.0f : 0.0f;
        const float motion = app_settings::AnimationScale();
        if (motion <= 0.0f) {
            progress = target;
            return;
        }
        const float dt = ImGui::GetIO().DeltaTime;
        const float amount = 1.0f - std::exp(-13.0f * dt * (0.65f + motion));
        progress += (target - progress) * amount;
    }

    float GetHover(ImGuiID id) {
        const auto it = g_hover.find(id);
        return it != g_hover.end() ? it->second : 0.0f;
    }

} // namespace CyberAnimations
