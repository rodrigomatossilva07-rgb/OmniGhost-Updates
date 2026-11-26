#include "../../widgets.h"
#include "gameplay/triggerbot_ui.h"

void DrawFortniteAim() {
    static Gameplay::TriggerIntegrationConfig fortnite_trigger_config;

    CyberWidgets::SectionTitle("Aim");
    CyberWidgets::InlineMessage("Fortnite controls are being configured.", CyberWidgets::TextTone::Info);
    
    ImGui::Spacing();
    CyberWidgets::SeparatorText("Triggerbot (Unificado)");
    
    if (Gameplay::UI::DrawTriggerbotConfig("Triggerbot Fortnite", fortnite_trigger_config, "Fortnite")) {
        // Config changed - would sync to Fortnite runtime here
    }
}
