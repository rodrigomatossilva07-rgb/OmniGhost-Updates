#include "../widgets.h"
#include "../theme.h"
#include "../localization.h"
#include "gameplay/unified_aim.h"
#include "gameplay/triggerbot.h"
#include "gameplay/triggerbot_ui.h"
#include "gameplay/triggerbot_integration.h"
#include "aimbot/aimbot.h"
#include "config/cs2_config.h"
#include <imgui.h>

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

    ImGui::TextUnformatted("TRIGGERBOT (Unificado)");
    ImGui::Separator();

    // Game sync buttons
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));
    if (ImGui::Button("Baixar do Jogo Atual", ImVec2(-1, 0))) {
        // Note: GameContext access would need proper includes
        // This is a stub - actual implementation would sync from game configs
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button("Aplicar ao Jogo Atual", ImVec2(-1, 0))) {
        // Note: GameContext access would need proper includes
        // This is a stub - actual implementation would sync to game configs
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Enable toggle
    if (ImGui::Checkbox("Ativar Triggerbot Unificado", &g_config.enabled)) {
        if (!g_config.enabled) g_triggerbot.Reset();
    }

    if (!g_config.enabled) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Ative o triggerbot para configurar. Use os botoes acima para sincronizar com o jogo atual.");
        return;
    }

    // Use the new UI helper
    if (UI::DrawTriggerbotConfig("Triggerbot Unificado", g_config)) {
        // Config changed
    }

    // Debug info
    const auto& state = g_triggerbot.GetState();
    ImGui::SeparatorText("Debug");
    ImGui::Text("Fase: %s", g_triggerbot.GetPhaseName());
    ImGui::Text("Target Entity: %s", state.target_entity ? std::to_string(state.target_entity).c_str() : "None");
    ImGui::Text("Shots in Burst: %d", state.shots_in_burst);

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