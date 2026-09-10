#include "onboarding.h"
#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "localization.h"
#include "hardware_monitor.h"
#include "hotkeys.h"
#include "../config/app_settings.h"
#include <algorithm>
#include <cstdio>

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

    void DrawHardwareRow(const char* label, const char* role,
                         const HardwareMonitor::DeviceStatus& status,
                         bool optional) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        constexpr float height = 54.0f;
        const ImVec4 stateColor = status.connected
            ? CyberTheme::Colors.Success
            : (status.port == "Checking" ? CyberTheme::Colors.Warning : CyberTheme::Colors.TextDisabled);
        const char* state = status.connected
            ? "Detected"
            : (status.port == "Checking" ? "Checking" : "Not detected");

        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
            CyberTheme::U32(CyberTheme::Colors.Card), CyberTheme::Radius::Sm);
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
            CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.65f), CyberTheme::Radius::Sm);
        dl->AddCircleFilled(ImVec2(pos.x + 17.0f, pos.y + height * 0.5f), 4.0f,
            CyberTheme::U32(stateColor));
        dl->AddText(ImVec2(pos.x + 31.0f, pos.y + 10.0f), CyberTheme::U32(CyberTheme::Colors.Text), label);
        dl->AddText(ImVec2(pos.x + 31.0f, pos.y + 30.0f), CyberTheme::U32(CyberTheme::Colors.TextDisabled), role);
        const ImVec2 stateSize = ImGui::CalcTextSize(state);
        dl->AddText(ImVec2(pos.x + width - stateSize.x - 14.0f, pos.y + 19.0f),
            CyberTheme::U32(stateColor), state);
        if (optional) {
            const char* optionalText = "OPTIONAL";
            const ImVec2 optionalSize = ImGui::CalcTextSize(optionalText);
            dl->AddText(ImVec2(pos.x + width - stateSize.x - optionalSize.x - 28.0f, pos.y + 19.0f),
                CyberTheme::U32(CyberTheme::Colors.TextDisabled), optionalText);
        }
        ImGui::Dummy(ImVec2(width, height + 8.0f));
    }

    void DrawWizard() {
        if (!g_active) return;
        
        const WizardStep& step = GetStepInfo(g_current_step);
        int step_idx = static_cast<int>(g_current_step);
        int total_steps = static_cast<int>(Step::Count) - 1; // Exclude Complete
        
        // Keep the wizard visually separate from the launcher. The old popup
        // inherited a nearly-black transparent background, which made its text
        // look as though it belonged to the page underneath.
        ImGui::OpenPopup("##onboarding_wizard");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700, 560), ImGuiCond_Appearing);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.99f));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 188));
        ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.34f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Lg);
        
        if (ImGui::BeginPopupModal("##onboarding_wizard", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 win_pos = ImGui::GetWindowPos();
            ImVec2 win_size = ImGui::GetWindowSize();
            
            // Progress bar at top
            float progress = static_cast<float>(step_idx) / total_steps;
            ImVec2 prog_pos(win_pos.x + 28, win_pos.y + 22);
            ImVec2 prog_end(win_pos.x + win_size.x - 28, prog_pos.y + 3);
            dl->AddRectFilled(prog_pos, prog_end, CyberTheme::U32(CyberTheme::Colors.Panel), 2.0f);
            dl->AddRectFilled(prog_pos, ImVec2(prog_pos.x + (prog_end.x - prog_pos.x) * progress, prog_pos.y + 4),
                CyberTheme::U32(CyberTheme::Colors.Gold), 2.0f);
            
            // Step indicator
            ImGui::SetCursorPos(ImVec2(28.0f, 42.0f));
            ImGui::TextColored(CyberTheme::Colors.TextDisabled, "SETUP  %d / %d", step_idx + 1, total_steps);

            // Stable numbered badge: the previous emoji glyphs are not present
            // in the product font and therefore rendered as question marks.
            const ImVec2 badge = ImVec2(win_pos.x + 52.0f, win_pos.y + 112.0f);
            dl->AddCircleFilled(badge, 24.0f, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.16f));
            dl->AddCircle(badge, 24.0f, CyberTheme::U32(CyberTheme::Colors.Gold), 0, 1.0f);
            char stepNumber[8];
            std::snprintf(stepNumber, sizeof(stepNumber), "%02d", step_idx + 1);
            const ImVec2 numberSize = ImGui::CalcTextSize(stepNumber);
            dl->AddText(ImVec2(badge.x - numberSize.x * 0.5f, badge.y - numberSize.y * 0.5f),
                CyberTheme::U32(CyberTheme::Colors.Gold), stepNumber);

            ImGui::SetCursorPos(ImVec2(96.0f, 83.0f));
            ImFont* title_font = CyberFonts::GetTitleFont();
            if (title_font) {
                ImGui::PushFont(title_font);
            }
            ImGui::TextUnformatted(step.title.c_str());
            if (title_font) {
                ImGui::PopFont();
            }
            ImGui::SetCursorPos(ImVec2(96.0f, 121.0f));
            ImGui::PushTextWrapPos(win_pos.x + win_size.x - 40.0f);
            ImGui::TextColored(CyberTheme::Colors.TextDisabled, "%s", step.description.c_str());
            ImGui::PopTextWrapPos();

            ImGui::SetCursorPos(ImVec2(28.0f, 164.0f));
            dl->AddLine(ImGui::GetCursorScreenPos(), ImVec2(win_pos.x + win_size.x - 28.0f, ImGui::GetCursorScreenPos().y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.75f));
            ImGui::Dummy(ImVec2(0.0f, 12.0f));

            ImGui::BeginChild("##onboarding_content", ImVec2(0.0f, win_size.y - 270.0f), false);
            ImGui::SetCursorPosX(28.0f);
            DrawStepContent(step);
            ImGui::EndChild();

            ImGui::SetCursorPos(ImVec2(28.0f, win_size.y - 66.0f));
            dl->AddLine(ImGui::GetCursorScreenPos(), ImVec2(win_pos.x + win_size.x - 28.0f, ImGui::GetCursorScreenPos().y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.75f));
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            
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
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        
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
                CyberWidgets::TextLine("Detection runs in the background. It does not open a DMA session.", CyberWidgets::TextTone::Secondary);
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                HardwareMonitor::Update();
                DrawHardwareRow("DMA / FPGA", "Required when starting a game", HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::DMA), false);
                DrawHardwareRow("MAKCU", "Input device", HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::Makcu), true);
                CyberWidgets::TextLine("You can continue without MAKCU. DMA is opened only after selecting a game.", CyberWidgets::TextTone::Secondary);
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
