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

    CyberWidgets::SectionTitle(label);

    // Enable toggle
    changed |= CyberWidgets::ToggleSwitch("Ativar Triggerbot", &config.enabled);

    if (!config.enabled) {
        CyberWidgets::InlineMessage("Ative o triggerbot para configurar.", CyberWidgets::TextTone::Info);
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
    CyberWidgets::SeparatorText("Timing");
    
    if (config.mode == 1 || config.mode == 3) { // PreFire or PrePost
        changed |= CyberWidgets::DragFloat("Pre-Fire Delay (ms)", &config.pre_fire_ms, 1.0f, 0.0f, 500.0f, "%.0f ms");
    }
    if (config.mode == 2 || config.mode == 3) { // PostFire or PrePost
        changed |= CyberWidgets::DragFloat("Post-Fire Delay (ms)", &config.post_fire_ms, 1.0f, 0.0f, 500.0f, "%.0f ms");
    }
    changed |= CyberWidgets::DragFloat("Min Time Between Shots (ms)", &config.min_time_between_shots, 1.0f, 0.0f, 1000.0f, "%.0f ms");

    // Hitbox / Distance
    CyberWidgets::SeparatorText("Hitbox / Distância");
    changed |= CyberWidgets::DragFloat("Deadzone (px)", &config.deadzone_px, 0.5f, 0.0f, 50.0f, "%.1f px");
    changed |= CyberWidgets::DragFloat("Max Distance (m)", &config.max_distance_m, 1.0f, 1.0f, 500.0f, "%.0f m");

    // Options
    CyberWidgets::SeparatorText("Opções");
    changed |= CyberWidgets::ToggleSwitch("Visible Only", &config.visible_only);
    changed |= CyberWidgets::ToggleSwitch("Fire Without Aim Key", &config.fire_without_aim);
    changed |= CyberWidgets::ToggleSwitch("Require Target Lock", &config.require_target_lock);
    changed |= CyberWidgets::ToggleSwitch("Ignore Knocked", &config.ignore_knocked);
    changed |= CyberWidgets::ToggleSwitch("Ignore Teammates", &config.ignore_teammates);
    changed |= CyberWidgets::ToggleSwitch("Ignore NPCs", &config.ignore_npcs);

    // Humanization
    CyberWidgets::SeparatorText("Humanização");
    changed |= CyberWidgets::ToggleSwitch("Simular Reação Humana", &config.simulate_human_reaction);
    if (config.simulate_human_reaction) {
        changed |= CyberWidgets::DragFloat("Reaction Variance (ms)", &config.reaction_variance_ms, 0.5f, 0.0f, 50.0f, "%.1f ms");
    }
    changed |= CyberWidgets::DragFloat("Jitter (px)", &config.jitter_px, 0.1f, 0.0f, 5.0f, "%.1f px");
    changed |= CyberWidgets::DragFloat("Min Health %", &config.min_health_percent, 1.0f, 0.0f, 100.0f, "%.0f%%");

    // Burst settings
    if (config.mode == 4) { // Burst
        CyberWidgets::SeparatorText("Burst Mode");
        changed |= CyberWidgets::DragInt("Shots per Burst", &config.burst_shots, 1, 1, 10);
        changed |= CyberWidgets::DragFloat("Burst Interval (ms)", &config.burst_interval_ms, 1.0f, 10.0f, 500.0f, "%.0f ms");
        changed |= CyberWidgets::DragFloat("Burst Cooldown (ms)", &config.burst_cooldown_ms, 10.0f, 50.0f, 2000.0f, "%.0f ms");
    }

    // Weapon filter (compact)
    CyberWidgets::SeparatorText("Filtro de Armas");
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

    CyberWidgets::SectionTitle(label);

    changed |= CyberWidgets::ToggleSwitch("Ativar", &config.enabled);
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
        changed |= CyberWidgets::DragFloat("Pre-Fire (ms)", &config.pre_fire_ms, 1.0f, 0.0f, 500.0f, "%.0f");
    }
    if (config.mode == 2 || config.mode == 3) {
        changed |= CyberWidgets::DragFloat("Post-Fire (ms)", &config.post_fire_ms, 1.0f, 0.0f, 500.0f, "%.0f");
    }
    changed |= CyberWidgets::DragFloat("Cooldown (ms)", &config.min_time_between_shots, 1.0f, 0.0f, 1000.0f, "%.0f");
    changed |= CyberWidgets::DragFloat("Deadzone (px)", &config.deadzone_px, 0.5f, 0.0f, 50.0f, "%.1f");
    changed |= CyberWidgets::DragFloat("Max Dist (m)", &config.max_distance_m, 1.0f, 1.0f, 500.0f, "%.0f");

    CyberWidgets::ToggleSwitch("Visible Only", &config.visible_only);
    CyberWidgets::ToggleSwitch("No Aim Key", &config.fire_without_aim);

    ImGui::PopID();
    return changed;
}

void DrawTriggerbotDebug(const TriggerIntegration& integration) {
    const auto& state = integration.GetTriggerbot().GetState();
    
    CyberWidgets::SeparatorText("Debug");
    CyberWidgets::KeyValueRow("Fase", integration.GetPhaseName());
    CyberWidgets::KeyValueRow("Target Entity", state.target_entity ? std::to_string(state.target_entity).c_str() : "None");
    CyberWidgets::KeyValueRow("Shots in Burst", std::to_string(state.shots_in_burst).c_str());
}

} // namespace OmniGhost::Gameplay::UI