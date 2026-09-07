#include "onboarding.h"
#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "localization.h"
#include "hardware_monitor.h"
#include "hotkeys.h"
#include "../config/app_settings.h"
#include <algorithm>

namespace Onboarding {

    namespace {
        Step g_current_step = Step::Welcome;
        bool g_active = false;
        bool g_completed = false;
        std::vector<WizardStep> g_steps;
        
        void InitSteps() {
            g_steps.clear();
            
            // Welcome
            g_steps.push_back({
                Step::Welcome,
                "Welcome to OmniGhost",
                "Thank you for choosing OmniGhost. This wizard will help you set up everything you need to get started.",
                "👋",
                false,
                []() { return true; },
                []() {},
                []() {}
            });
            
            // License Agreement
            g_steps.push_back({
                Step::LicenseAgreement,
                "License Agreement",
                "Please review and accept the license agreement to continue.",
                "📜",
                false,
                []() { return app_settings::config.license_accepted; },
                []() {},
                []() {}
            });
            
            // Hardware Detection
            g_steps.push_back({
                Step::HardwareDetection,
                "Hardware Detection",
                "We'll detect your DMA hardware and input devices. Make sure they're connected.",
                "🔧",
                false,
                []() { 
                    HardwareMonitor::Initialize();
                    return HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::DMA).connected ||
                           HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::Makcu).connected ||
                           HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::KMBoxNet).connected ||
                           HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::Ferrum).connected;
                },
                []() { HardwareMonitor::Initialize(); },
                []() {}
            });
            
            // Game Selection
            g_steps.push_back({
                Step::GameSelection,
                "Select Your Game",
                "Choose which game you want to configure first. You can add more later.",
                "🎮",
                false,
                []() { return app_settings::config.last_selected_game != -1; },
                []() {},
                []() {}
            });
            
            // Theme Selection
            g_steps.push_back({
                Step::ThemeSelection,
                "Choose Your Theme",
                "Pick a visual theme that matches your style. You can change this anytime.",
                "🎨",
                true,
                []() { return true; },
                []() {},
                []() {}
            });
            
            // Hotkey Setup
            g_steps.push_back({
                Step::HotkeySetup,
                "Configure Hotkeys",
                "Set up your preferred keyboard shortcuts for quick access to features.",
                "⌨️",
                true,
                []() { return true; },
                []() { Hotkeys::Initialize(); },
                []() {}
            });
            
            // DMA Setup
            g_steps.push_back({
                Step::DMASetup,
                "DMA Configuration",
                "Configure your DMA device connection settings for optimal performance.",
                "💾",
                false,
                []() { return HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::DMA).connected; },
                []() {},
                []() {}
            });
            
            // Complete
            g_steps.push_back({
                Step::Complete,
                "Setup Complete!",
                "You're all set! OmniGhost is ready to use. Press Insert to open the menu anytime.",
                "✨",
                false,
                []() { return true; },
                []() { Complete(); },
                []() {}
            });
        }
    }

    void Initialize() {
        if (g_steps.empty()) InitSteps();
        g_completed = app_settings::config.onboarding_completed;
    }

    void Shutdown() {
        g_active = false;
    }

    bool ShouldRunOnboarding() {
        return !g_completed && !app_settings::config.onboarding_completed;
    }

    void Start() {
        if (g_completed) return;
        g_active = true;
        g_current_step = Step::Welcome;
    }

    Step GetCurrentStep() {
        return g_current_step;
    }

    const WizardStep& GetStepInfo(Step step) {
        int idx = static_cast<int>(step);
        if (idx >= 0 && idx < static_cast<int>(Step::Count)) {
            return g_steps[idx];
        }
        static WizardStep empty = { Step::Count, "", "", "", false, nullptr, nullptr, nullptr };
        return empty;
    }

    bool NextStep() {
        const WizardStep& current = GetStepInfo(g_current_step);
        if (current.validator && !current.validator()) {
            CyberWidgets::Notify("Please complete the current step first", CyberWidgets::ToastType::Warning);
            return false;
        }
        
        if (current.onExit) current.onExit();
        
        int next = static_cast<int>(g_current_step) + 1;
        if (next < static_cast<int>(Step::Count)) {
            g_current_step = static_cast<Step>(next);
            const WizardStep& next_step = GetStepInfo(g_current_step);
            if (next_step.onEnter) next_step.onEnter();
            return true;
        }
        return false;
    }

    bool PreviousStep() {
        int prev = static_cast<int>(g_current_step) - 1;
        if (prev >= 0) {
            const WizardStep& current = GetStepInfo(g_current_step);
            if (current.onExit) current.onExit();
            
            g_current_step = static_cast<Step>(prev);
            const WizardStep& prev_step = GetStepInfo(g_current_step);
            if (prev_step.onEnter) prev_step.onEnter();
            return true;
        }
        return false;
    }

    bool GoToStep(Step step) {
        if (static_cast<int>(step) >= 0 && static_cast<int>(step) < static_cast<int>(Step::Count)) {
            const WizardStep& current = GetStepInfo(g_current_step);
            if (current.onExit) current.onExit();
            
            g_current_step = step;
            const WizardStep& next_step = GetStepInfo(g_current_step);
            if (next_step.onEnter) next_step.onEnter();
            return true;
        }
        return false;
    }

    void Complete() {
        g_active = false;
        g_completed = true;
        app_settings::config.onboarding_completed = true;
        app_settings::SaveGlobal(nullptr);
        CyberWidgets::Notify("Onboarding completed!", CyberWidgets::ToastType::Success);
    }

    void Skip() {
        Complete();
    }

    void DrawStepContent(const WizardStep& step);

    void DrawWizard() {
        if (!g_active) return;
        
        const WizardStep& step = GetStepInfo(g_current_step);
        int step_idx = static_cast<int>(g_current_step);
        int total_steps = static_cast<int>(Step::Count) - 1; // Exclude Complete
        
        // Modal window
        ImGui::OpenPopup("##onboarding_wizard");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Appearing);
        
        if (ImGui::BeginPopupModal("##onboarding_wizard", &g_active, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 win_pos = ImGui::GetWindowPos();
            ImVec2 win_size = ImGui::GetWindowSize();
            
            // Progress bar at top
            float progress = static_cast<float>(step_idx) / total_steps;
            ImVec2 prog_pos(win_pos.x + 20, win_pos.y + 20);
            ImVec2 prog_end(win_pos.x + win_size.x - 20, prog_pos.y + 4);
            dl->AddRectFilled(prog_pos, prog_end, CyberTheme::U32(CyberTheme::Colors.Panel), 2.0f);
            dl->AddRectFilled(prog_pos, ImVec2(prog_pos.x + (prog_end.x - prog_pos.x) * progress, prog_pos.y + 4),
                CyberTheme::U32(CyberTheme::Colors.Gold), 2.0f);
            
            // Step indicator
            ImGui::SetCursorPosY(40);
            ImGui::TextColored(CyberTheme::Colors.TextDisabled, "Step %d of %d", step_idx + 1, total_steps);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Icon and title
            ImVec2 icon_pos = ImGui::GetCursorScreenPos();
            ImFont* title_font = CyberFonts::GetTitleFont();
            if (title_font) {
                dl->AddText(title_font, 48.0f, icon_pos, CyberTheme::U32(CyberTheme::Colors.Gold), step.icon.c_str());
                ImGui::Dummy(ImVec2(60, 60));
            }
            
            ImGui::SetCursorPosX(80);
            ImGui::BeginGroup();
            if (title_font) {
                dl->AddText(title_font, 28.0f, ImGui::GetCursorScreenPos(), CyberTheme::U32(CyberTheme::Colors.Text), step.title.c_str());
            } else {
                ImGui::Text("%s", step.title.c_str());
            }
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480);
            ImGui::TextWrapped("%s", step.description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Step-specific content
            DrawStepContent(step);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Navigation buttons
            float btn_width = 120.0f;
            if (step_idx > 0) {
                if (CyberWidgets::Button("Back", CyberWidgets::ButtonStyle::Secondary, ImVec2(btn_width, 40))) {
                    PreviousStep();
                }
                ImGui::SameLine();
            }
            
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - btn_width * 2 - 20, 0));
            ImGui::SameLine();
            
            bool is_last = step_idx >= total_steps - 1;
            if (is_last) {
                if (CyberWidgets::GoldButton("Finish", ImVec2(btn_width, 40))) {
                    Complete();
                }
            } else {
                if (CyberWidgets::GoldButton("Next", ImVec2(btn_width, 40))) {
                    NextStep();
                }
            }
            
            if (step.optional) {
                ImGui::SameLine();
                if (CyberWidgets::Button("Skip", CyberWidgets::ButtonStyle::Ghost, ImVec2(80, 40))) {
                    NextStep();
                }
            }
            
            ImGui::EndPopup();
        }
        
        if (!g_active) {
            ImGui::CloseCurrentPopup();
        }
    }

    void DrawStepContent(const WizardStep& step) {
        switch (step.id) {
            case Step::Welcome: {
                CyberWidgets::TextLine("OmniGhost is a premium DMA-based gaming enhancement suite.", CyberWidgets::TextTone::Secondary);
                CyberWidgets::TextLine("Features:", CyberWidgets::TextTone::Accent);
                CyberWidgets::TextLine("• Real-time ESP with player, vehicle, and world visualization");
                CyberWidgets::TextLine("• Precision aim assist with multiple input device support");
                CyberWidgets::TextLine("• FiveM, CS2, Rust, Warzone, Valorant, and Fortnite support");
                CyberWidgets::TextLine("• Advanced configuration with profiles and presets");
                CyberWidgets::TextLine("• Hardware monitoring with real-time metrics");
                break;
            }
            case Step::LicenseAgreement: {
                static bool accepted = false;
                ImGui::Checkbox("I accept the license agreement", &accepted);
                if (accepted != app_settings::config.license_accepted) {
                    app_settings::config.license_accepted = accepted;
                }
                ImGui::Spacing();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480);
                ImGui::TextWrapped("By accepting, you agree to use OmniGhost responsibly and in accordance with the terms of service. This software is intended for educational and research purposes only.");
                ImGui::PopTextWrapPos();
                break;
            }
            case Step::HardwareDetection: {
                CyberWidgets::TextLine("Detecting hardware...", CyberWidgets::TextTone::Accent);
                ImGui::Spacing();
                
                HardwareMonitor::Update();
                
                for (int i = 0; i < static_cast<int>(HardwareMonitor::DeviceType::Count); ++i) {
                    const auto& status = HardwareMonitor::GetDeviceStatus(static_cast<HardwareMonitor::DeviceType>(i));
                    if (status.enabled || status.connected) {
                        std::string health = HardwareMonitor::GetDeviceHealthString(static_cast<HardwareMonitor::DeviceType>(i));
                        CyberWidgets::TextTone tone = CyberWidgets::TextTone::Success;
                        if (health == "Disconnected" || health == "Stale") tone = CyberWidgets::TextTone::Error;
                        else if (health == "High Latency" || health == "Elevated Latency") tone = CyberWidgets::TextTone::Warning;
                        CyberWidgets::StatusBadge(status.name.c_str(), status.connected);
                        if (status.connected) {
                            char lat[64];
                            std::snprintf(lat, sizeof(lat), "Latency: %.1fms", HardwareMonitor::GetAverageLatency(static_cast<HardwareMonitor::DeviceType>(i)));
                            CyberWidgets::KeyValueRow("Latency", lat);
                        }
                    }
                }
                break;
            }
            case Step::GameSelection: {
                CyberWidgets::TextLine("Select your primary game:", CyberWidgets::TextTone::Secondary);
                ImGui::Spacing();
                
                const char* games[] = { "FiveM (GTA V RP)", "Counter-Strike 2", "Rust", "Warzone", "Valorant", "Fortnite" };
                static int selected = 0;
                if (CyberWidgets::Combo("Game", &selected, games, 6)) {
                    app_settings::config.last_selected_game = selected;
                }
                break;
            }
            case Step::ThemeSelection: {
                CyberWidgets::TextLine("Choose your preferred theme:", CyberWidgets::TextTone::Secondary);
                ImGui::Spacing();
                CyberWidgets::ThemeCombo("Theme");
                ImGui::Spacing();
                CyberWidgets::TextLine("Accent Color:", CyberWidgets::TextTone::Secondary);
                const auto& presets = CyberTheme::GetAccentPresets();
                const char* preset_names[9];
                for (int i = 0; i < 9; ++i) preset_names[i] = presets[i].name;
                int preset_idx = static_cast<int>(CyberTheme::GetAccentPreset());
                if (CyberWidgets::Combo("Accent", &preset_idx, preset_names, 9)) {
                    CyberTheme::SetAccentPreset(static_cast<CyberTheme::AccentPreset>(preset_idx));
                }
                break;
            }
            case Step::HotkeySetup: {
                CyberWidgets::TextLine("Configure your keyboard shortcuts:", CyberWidgets::TextTone::Secondary);
                ImGui::Spacing();
                CyberWidgets::TextLine("Press the button next to each action to set a new hotkey.", CyberWidgets::TextTone::Secondary);
                ImGui::Spacing();
                
                // Show key hotkeys
                const Hotkeys::Action key_actions[] = {
                    Hotkeys::Action::ToggleMenu,
                    Hotkeys::Action::ToggleESP,
                    Hotkeys::Action::ToggleAim,
                    Hotkeys::Action::PanicButton
                };
                
                for (auto action : key_actions) {
                    const auto& hk = Hotkeys::GetHotkey(action);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "%s: %s", hk.label.c_str(), Hotkeys::GetHotkeyDisplayString(hk).c_str());
                    CyberWidgets::TextLine(buf, CyberWidgets::TextTone::Primary);
                }
                break;
            }
            case Step::DMASetup: {
                CyberWidgets::TextLine("DMA Device Configuration", CyberWidgets::TextTone::Accent);
                ImGui::Spacing();
                CyberWidgets::TextLine("If your DMA device is not detected, please:", CyberWidgets::TextTone::Secondary);
                CyberWidgets::TextLine("1. Ensure the DMA device is properly connected via PCIe");
                CyberWidgets::TextLine("2. Install the appropriate drivers (LeechCore/Vmm)");
                CyberWidgets::TextLine("3. Run the DMA validation tool from the Diagnostics page");
                CyberWidgets::TextLine("4. Restart OmniGhost after connecting the device");
                break;
            }
            case Step::Complete: {
                CyberWidgets::TextLine("🎉 OmniGhost is ready to use!", CyberWidgets::TextTone::Success);
                ImGui::Spacing();
                CyberWidgets::TextLine("Quick tips:", CyberWidgets::TextTone::Accent);
                CyberWidgets::TextLine("• Press INSERT to open/close the menu");
                CyberWidgets::TextLine("• Use the global search (top-right) to find settings quickly");
                CyberWidgets::TextLine("• Check the Status page for real-time hardware metrics");
                CyberWidgets::TextLine("• Configure your ESP/Aim settings per game");
                CyberWidgets::TextLine("• Save profiles for different playstyles");
                break;
            }
            default:
                break;
        }
    }

    bool IsActive() {
        return g_active;
    }

    void SetCompleted(bool completed) {
        g_completed = completed;
        app_settings::config.onboarding_completed = completed;
        app_settings::SaveGlobal(nullptr);
    }

    bool IsCompleted() {
        return g_completed || app_settings::config.onboarding_completed;
    }

    void Reset() {
        g_completed = false;
        g_active = false;
        g_current_step = Step::Welcome;
        app_settings::config.onboarding_completed = false;
        app_settings::SaveGlobal(nullptr);
    }

} // namespace Onboarding