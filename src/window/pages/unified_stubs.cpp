



#include "../widgets.h"
#include "../theme.h"
#include "../localization.h"
#include "gameplay/unified_aim.h"
#include "gameplay/triggerbot.h"
#include "gameplay/triggerbot_ui.h"
#include "gameplay/triggerbot_integration.h"
#include "aimbot/aimbot.h"
#include "config/cs2_config.h"

void DrawUnifiedAim() {
    ImGui::TextDisabled("Unified Aim configuration requires full build");
}

void DrawSoundESP() {
    ImGui::TextDisabled("Sound ESP configuration requires full build");
}

void DrawSpectatorList() {
    ImGui::TextDisabled("Spectator List configuration requires full build");
}

void DrawTriggerbot() {
    using namespace OmniGhost::Gameplay;
    
    static Triggerbot g_triggerbot;
    static TriggerIntegrationConfig g_config;
    static bool g_first_open = true;

    CyberWidgets::SectionTitle("TRIGGERBOT (Unificado)");

    // Game sync buttons
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));
    if (ImGui::Button(ICON_FA_DOWNLOAD " Sincronizar do Jogo Atual", ImVec2(-1, 0))) {
        auto game = OmniGhost::GameContext::Instance().GetActiveGame();
        if (game == OmniGhost::ActiveGame::FiveM) {
            g_config.enabled = aimbot::config.trigger_enabled;
            g_config.visible_only = aimbot::config.trigger_visibility_check;
            g_config.fire_without_aim = aimbot::config.trigger_always_on;
            g_config.mode = aimbot::config.trigger_burst ? 4 : 3;
            g_config.pre_fire_ms = aimbot::config.trigger_delay * 1000.0f;
            g_config.post_fire_ms = aimbot::config.trigger_cooldown * 1000.0f;
            g_config.min_time_between_shots = aimbot::config.trigger_cooldown * 1000.0f;
            g_config.deadzone_px = aimbot::config.trigger_fov;
            g_config.max_distance_m = aimbot::config.max_distance;
        } else if (game == OmniGhost::ActiveGame::CS2 || game == OmniGhost::ActiveGame::Rust) {
            g_config.enabled = CS2::config.trigger_enabled;
            g_config.visible_only = CS2::config.trigger_visibility_check;
            g_config.fire_without_aim = CS2::config.trigger_always_on;
            g_config.mode = 3;
            g_config.pre_fire_ms = CS2::config.trigger_delay_ms;
            g_config.post_fire_ms = 50.0f;
            g_config.min_time_between_shots = 50.0f;
            g_config.deadzone_px = 5.0f;
            g_config.max_distance_m = CS2::config.aim_max_dist;
        } else if (game == OmniGhost::ActiveGame::Fortnite) {
            // Fortnite uses unified config directly
        }
    }
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button(ICON_FA_UPLOAD " Aplicar ao Jogo Atual", ImVec2(-1, 0))) {
        auto game = OmniGhost::GameContext::Instance().GetActiveGame();
        if (game == OmniGhost::ActiveGame::FiveM) {
            aimbot::config.trigger_enabled = g_config.enabled;
            aimbot::config.trigger_visibility_check = g_config.visible_only;
            aimbot::config.trigger_always_on = g_config.fire_without_aim;
            aimbot::config.trigger_burst = (g_config.mode == 4);
            aimbot::config.trigger_delay = g_config.pre_fire_ms / 1000.0f;
            aimbot::config.trigger_cooldown = g_config.post_fire_ms / 1000.0f;
            aimbot::config.trigger_fov = g_config.deadzone_px;
        } else if (game == OmniGhost::ActiveGame::CS2 || game == OmniGhost::ActiveGame::Rust) {
            CS2::config.trigger_enabled = g_config.enabled;
            CS2::config.trigger_visibility_check = g_config.visible_only;
            CS2::config.trigger_always_on = g_config.fire_without_aim;
            CS2::config.trigger_delay_ms = static_cast<int>(g_config.pre_fire_ms);
        } else if (game == OmniGhost::ActiveGame::Fortnite) {
            // Fortnite uses unified config directly
        }
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Enable toggle
    if (CyberWidgets::ToggleSwitch("Ativar Triggerbot Unificado", &g_config.enabled)) {
        if (!g_config.enabled) g_triggerbot.Reset();
    }

    if (!g_config.enabled) {
        CyberWidgets::InlineMessage("Ative o triggerbot para configurar. Use os botões acima para sincronizar com o jogo atual.", CyberWidgets::TextTone::Info);
        return;
    }

    // Use the new UI helper
    if (UI::DrawTriggerbotConfig("Triggerbot Unificado", g_config)) {
        // Config changed
    }

    // Debug info
    const auto& state = g_triggerbot.GetState();
    CyberWidgets::SeparatorText("Debug");
    CyberWidgets::KeyValueRow("Fase", g_triggerbot.GetPhaseName());
    CyberWidgets::KeyValueRow("Target Entity", state.target_entity ? std::to_string(state.target_entity).c_str() : "None");
    CyberWidgets::KeyValueRow("Shots in Burst", std::to_string(state.shots_in_burst).c_str());

    // Update triggerbot
    g_triggerbot.SetConfig(TriggerIntegration::ConvertConfig(g_config));
}

void DrawRecoilControl() {
    ImGui::TextDisabled("Recoil Control configuration requires full build");
}

void DrawPrediction() {
    ImGui::TextDisabled("Prediction configuration requires full build");
}

void DrawVisibility() {
    ImGui::TextDisabled("Visibility configuration requires full build");
}

void DrawBoneSystem() {
    ImGui::TextDisabled("Bone System configuration requires full build");
}

void DrawSmoothCurves() {
    ImGui::TextDisabled("Smooth Curves configuration requires full build");
}

void DrawRecoilPatterns() {
    ImGui::TextDisabled("Recoil Patterns configuration requires full build");
}

void DrawEntityCache() {
    ImGui::TextDisabled("Entity Cache configuration requires full build");
}

void DrawProfileManager() {
    ImGui::TextDisabled("Profile Manager configuration requires full build");
}

void DrawOffsetManager() {
    ImGui::TextDisabled("Offset Manager configuration requires full build");
}

void DrawResolution() {
    ImGui::TextDisabled("Resolution configuration requires full build");
}

void DrawGameAdapter() {
    ImGui::TextDisabled("Game Adapter configuration requires full build");
}