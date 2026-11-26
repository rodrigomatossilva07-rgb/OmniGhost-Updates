#include "../../widgets.h"
#include "../../widgets_input.h"
#include "../../widgets_layout.h"
#include "gameplay/triggerbot_ui.h"

void DrawFortniteAim() {
    static OmniGhost::Gameplay::TriggerIntegrationConfig fortnite_trigger_config;

    CyberWidgets::SectionTitle("Aim");
    CyberWidgets::InlineMessage("Fortnite controls are being configured.", CyberWidgets::TextTone::Info);
    
    ImGui::Spacing();
    CyberWidgets::SectionTitle("Triggerbot (Unificado)");
    
    if (OmniGhost::Gameplay::UI::DrawTriggerbotConfig("Triggerbot Fortnite", fortnite_trigger_config, "Fortnite")) {
        // Config changed - would sync to Fortnite runtime here
    }
}
