#include "triggerbot_ui.h"
#include "../window/widgets.h"
#include "../window/theme.h"
#include <imgui.h>

namespace OmniGhost::Gameplay::UI {

static const char* g_trigger_modes[] = {
    "Instant", "Pre-Fire", "Post-Fire", "Pre+Post", "Burst"
};

static const char* g_trigger_mode_tooltips[] = {
    "Dispara imediatamente ao entrar no hitbox",
    "Delay antes de atirar (antecipa movimento)",
    "Delay depois de atirar (controla recuo)",
    "Pre-Fire + Post-Fire",
    "Rajada controlada com cooldown"
};

bool DrawTriggerbotConfig(const char* label, TriggerIntegrationConfig& config, const std::string& game_prefix) {
    bool changed = false;
    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::Separator();

    // Enable toggle
    changed |= ImGui::Checkbox("Ativar Triggerbot", &config.enabled);

    if (!config.enabled) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Ative o triggerbot para configurar.");
        ImGui::PopID();
        return changed;
    }

    // Mode selector
    int mode_idx = config.mode;
    if (ImGui::Combo("Modo", &mode_idx, g_trigger_modes, IM_ARRAYSIZE(g_trigger_modes))) {
        config.mode = mode_idx;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", g_trigger_mode_tooltips[config.mode]);
    }

    // Timing section
    if (config.mode == 1 || config.mode == 3) { // PreFire or PrePost
        changed |= ImGui::DragFloat("Pre-Fire Delay (ms)", &config.pre_fire_ms, 1.0f, 0.0f, 500.0f, "%.0f ms");
    }
    if (config.mode == 2 || config.mode == 3) { // PostFire or PrePost
        changed |= ImGui::DragFloat("Post-Fire Delay (ms)", &config.post_fire_ms, 1.0f, 0.0f, 500.0f, "%.0f ms");
    }
    changed |= ImGui::DragFloat("Min Time Between Shots (ms)", &config.min_time_between_shots, 1.0f, 0.0f, 1000.0f, "%.0f ms");

    // Hitbox / Distance
    ImGui::SeparatorText("Hitbox / Distância");
    changed |= ImGui::DragFloat("Deadzone (px)", &config.deadzone_px, 0.5f, 0.0f, 50.0f, "%.1f px");
    changed |= ImGui::DragFloat("Max Distance (m)", &config.max_distance_m, 1.0f, 1.0f, 500.0f, "%.0f m");

    // Options
    ImGui::SeparatorText("Opções");
    changed |= ImGui::Checkbox("Visible Only", &config.visible_only);
    changed |= ImGui::Checkbox("Fire Without Aim Key", &config.fire_without_aim);
    changed |= ImGui::Checkbox("Require Target Lock", &config.require_target_lock);
    changed |= ImGui::Checkbox("Ignore Knocked", &config.ignore_knocked);
    changed |= ImGui::Checkbox("Ignore Teammates", &config.ignore_teammates);
    changed |= ImGui::Checkbox("Ignore NPCs", &config.ignore_npcs);

    // Humanization
    ImGui::SeparatorText("Humanização");
    changed |= ImGui::Checkbox("Simular Reação Humana", &config.simulate_human_reaction);
    if (config.simulate_human_reaction) {
        changed |= ImGui::DragFloat("Reaction Variance (ms)", &config.reaction_variance_ms, 0.5f, 0.0f, 50.0f, "%.1f ms");
    }
    changed |= ImGui::DragFloat("Jitter (px)", &config.jitter_px, 0.1f, 0.0f, 5.0f, "%.1f px");
    changed |= ImGui::DragFloat("Min Health %", &config.min_health_percent, 1.0f, 0.0f, 100.0f, "%.0f%%");

    // Burst settings
    if (config.mode == 4) { // Burst
        ImGui::SeparatorText("Burst Mode");
        changed |= ImGui::DragInt("Shots per Burst", &config.burst_shots, 1, 1, 10);
        changed |= ImGui::DragFloat("Burst Interval (ms)", &config.burst_interval_ms, 1.0f, 10.0f, 500.0f, "%.0f ms");
        changed |= ImGui::DragFloat("Burst Cooldown (ms)", &config.burst_cooldown_ms, 10.0f, 50.0f, 2000.0f, "%.0f ms");
    }

    // Weapon filter (compact)
    ImGui::SeparatorText("Filtro de Armas");
    static char weapon_filter_input[64] = "";
    ImGui::InputText("Weapon Mask (hex)", weapon_filter_input, sizeof(weapon_filter_input), ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Aplicar")) {
        uint64_t mask = 0;
        sscanf_s(weapon_filter_input, "%llx", &mask);
        config.weapon_mask = mask;
        for (int i = 0; i < 64; ++i) {
            config.weapon_allowed[i] = (mask & (1ULL << i)) != 0;
        }
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Todas")) {
        config.weapon_mask = ~0ULL;
        for (int i = 0; i < 64; ++i) config.weapon_allowed[i] = true;
        weapon_filter_input[0] = 0;
        changed = true;
    }

    ImGui::PopID();
    return changed;
}

bool DrawTriggerbotConfigCompact(const char* label, TriggerIntegrationConfig& config) {
    bool changed = false;
    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::Separator();

    changed |= ImGui::Checkbox("Ativar", &config.enabled);
    if (!config.enabled) {
        ImGui::PopID();
        return changed;
    }

    int mode_idx = config.mode;
    if (ImGui::Combo("Modo", &mode_idx, g_trigger_modes, IM_ARRAYSIZE(g_trigger_modes))) {
        config.mode = mode_idx;
        changed = true;
    }

    if (config.mode == 1 || config.mode == 3) {
        changed |= ImGui::DragFloat("Pre-Fire (ms)", &config.pre_fire_ms, 1.0f, 0.0f, 500.0f, "%.0f");
    }
    if (config.mode == 2 || config.mode == 3) {
        changed |= ImGui::DragFloat("Post-Fire (ms)", &config.post_fire_ms, 1.0f, 0.0f, 500.0f, "%.0f");
    }
    changed |= ImGui::DragFloat("Cooldown (ms)", &config.min_time_between_shots, 1.0f, 0.0f, 1000.0f, "%.0f");
    changed |= ImGui::DragFloat("Deadzone (px)", &config.deadzone_px, 0.5f, 0.0f, 50.0f, "%.1f");
    changed |= ImGui::DragFloat("Max Dist (m)", &config.max_distance_m, 1.0f, 1.0f, 500.0f, "%.0f");

    ImGui::Checkbox("Visible Only", &config.visible_only);
    ImGui::Checkbox("No Aim Key", &config.fire_without_aim);

    ImGui::PopID();
    return changed;
}

void DrawTriggerbotDebug(const TriggerIntegration& integration) {
    const auto& state = integration.GetTriggerbot().GetState();

    ImGui::SeparatorText("Debug");
    ImGui::Text("Fase: %s", integration.GetPhaseName());
    ImGui::Text("Target Entity: %s", state.target_entity ? std::to_string(state.target_entity).c_str() : "None");
    ImGui::Text("Shots in Burst: %d", state.shots_in_burst);
}

} // namespace OmniGhost::Gameplay::UI