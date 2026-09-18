#include "../../menu_tab.h"
#include "../../Cs2/grenade/cs2_grenade_helper.h"
#include "../../Cs2/cs2_game.h"
#include "../../../window/widgets.h"
#include "../../../ImGui/imgui.h"
#include <chrono>
#include <string>

namespace {

CS2_Grenade::GrenadeHelper g_grenadeHelper;

} // namespace

void DrawCs2GrenadeHelper() {
    using namespace CS2_Grenade;
    
    // Initialize if not already done
    static bool initialized = false;
    if (!initialized) {
        if (g_grenadeHelper.Initialize()) {
            initialized = true;
        }
    }
    
    if (!initialized) {
        CyberWidgets::TextLine("Failed to initialize Grenade Helper", CyberWidgets::TextTone::Error);
        return;
    }
    
    // Main enable toggle
    CyberWidgets::ToggleSwitch("Enable Grenade Helper", &CS2::config.grenade_helper);
    
    if (!CS2::config.grenade_helper) {
        CyberWidgets::TextLine("Grenade Helper is disabled", CyberWidgets::TextTone::Secondary);
        return;
    }
    
    ImGui::Spacing();
    
    // Visual options
    if (ImGui::CollapsingHeader("Visual Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        CyberWidgets::ToggleSwitch("Show Trajectory", &CS2::config.grenade_helper_trajectory);
        CyberWidgets::ToggleSwitch("Show Bounce Markers", &CS2::config.grenade_helper_bounces);
        CyberWidgets::ToggleSwitch("Show Landing Marker", &CS2::config.grenade_helper_landing);
        CyberWidgets::ToggleSwitch("Show Landing Preview", &CS2::config.grenade_helper_preview);
        CyberWidgets::ToggleSwitch("Show Radar Marker", &CS2::config.grenade_helper_radar);
        CyberWidgets::ToggleSwitch("Show Debug Panel", &CS2::config.grenade_helper_debug);
        
        ImGui::Spacing();
        
        CyberWidgets::SliderFloat("Trajectory Thickness", &CS2::config.grenade_trajectory_thickness, 1.0f, 5.0f);
        CyberWidgets::SliderFloat("Bounce Marker Size", &CS2::config.grenade_bounce_size, 3.0f, 15.0f);
        CyberWidgets::SliderFloat("Landing Marker Size", &CS2::config.grenade_landing_size, 5.0f, 20.0f);
        
        ImGui::Spacing();
        
        if (ImGui::Button("Reset to Defaults")) {
        }
    }
    
    ImGui::Spacing();
    
    // Physics settings
    if (ImGui::CollapsingHeader("Physics Settings")) {
        auto& physics = g_grenadeHelper.GetPredictor().GetConfig();
        
        CyberWidgets::SliderFloat("Gravity", &physics.gravity, 400.0f, 1200.0f);
        CyberWidgets::SliderFloat("Timestep", &physics.timestep, 0.001f, 0.05f, "%.4f");
        CyberWidgets::SliderFloat("Elasticity", &physics.elasticity, 0.0f, 1.0f);
        CyberWidgets::SliderFloat("Friction", &physics.friction, 0.0f, 1.0f);
        CyberWidgets::SliderFloat("Stop Velocity", &physics.stopVelocity, 1.0f, 50.0f);
        CyberWidgets::SliderFloat("Player Velocity Contribution", &physics.playerVelocityContribution, 0.0f, 2.0f);
        CyberWidgets::SliderFloat("Max Simulation Time", &physics.maxSimulationTime, 5.0f, 30.0f);
        
        ImGui::Spacing();
        
        if (ImGui::TreeNode("Grenade Type Configs")) {
            for (int i = 1; i <= 5; ++i) {
                auto& typeConfig = physics.typeConfigs[i];
                const char* typeName = "";
                switch (i) {
                    case 1: typeName = "Flashbang"; break;
                    case 2: typeName = "HE"; break;
                    case 3: typeName = "Smoke"; break;
                    case 4: typeName = "Molotov"; break;
                    case 5: typeName = "Incendiary"; break;
                }
                
                if (ImGui::TreeNode(typeName)) {
                    CyberWidgets::SliderFloat("Initial Velocity", &typeConfig.initialVelocity, 500.0f, 2000.0f);
                    CyberWidgets::SliderFloat("Mass", &typeConfig.mass, 0.1f, 5.0f);
                    CyberWidgets::SliderFloat("Detonation Time", &typeConfig.detonationTime, 0.0f, 5.0f);
                    CyberWidgets::ToggleSwitch("Is Incendiary", &typeConfig.isIncendiary);
                    if (typeConfig.isIncendiary) {
                        CyberWidgets::SliderFloat("Fire Duration", &typeConfig.fireDuration, 1.0f, 20.0f);
                        CyberWidgets::SliderFloat("Fire Radius", &typeConfig.fireRadius, 50.0f, 500.0f);
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }
    }
    
    ImGui::Spacing();
    
    // Status
    if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto snapshot = g_grenadeHelper.GetSnapshot();
        
        if (g_grenadeHelper.IsActive()) {
            CyberWidgets::TextLine("Status: Active", CyberWidgets::TextTone::Success);
        } else {
            CyberWidgets::TextLine("Status: Inactive", CyberWidgets::TextTone::Secondary);
        }
        
        if (snapshot) {
            char buf[128];
            sprintf_s(buf, "Grenade Type: %s", CS2_Grenade::GrenadeTypeToString(snapshot->input.grenadeType).data());
            CyberWidgets::TextLine(buf);
            sprintf_s(buf, "Throw Mode: %s", CS2_Grenade::ThrowModeToString(snapshot->input.throwMode).data());
            CyberWidgets::TextLine(buf);
            sprintf_s(buf, "On Ground: %s", snapshot->input.onGround ? "Yes" : "No");
            CyberWidgets::TextLine(buf);
            sprintf_s(buf, "Preparing: %s", snapshot->input.preparingThrow ? "Yes" : "No");
            CyberWidgets::TextLine(buf);
            sprintf_s(buf, "Throw Strength: %.2f", snapshot->input.throwStrength);
            CyberWidgets::TextLine(buf);
        }
        
        const auto& debug = g_grenadeHelper.GetPredictor().GetLastDebugInfo();
        if (debug.simulationSteps > 0) {
            char buf[128];
            sprintf_s(buf, "Simulation Steps: %d", debug.simulationSteps);
            CyberWidgets::TextLine(buf);
            sprintf_s(buf, "Flight Time: %.3f s", debug.totalSimulationTime);
            CyberWidgets::TextLine(buf);
            sprintf_s(buf, "Bounces: %d", debug.bounces);
            CyberWidgets::TextLine(buf);
            sprintf_s(buf, "Sim Time: %.2f ms", debug.simulationTimeMs);
            CyberWidgets::TextLine(buf);
        }
    }
    
    ImGui::Spacing();
    
    // Actions
    if (ImGui::CollapsingHeader("Actions")) {
        if (ImGui::Button("Clear Cache")) {
            g_grenadeHelper.GetPredictor().ClearCache();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Physics to Defaults")) {
        }
    }
}