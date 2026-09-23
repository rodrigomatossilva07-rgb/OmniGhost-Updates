#include "onboarding.h"
#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "brand_assets.h"
#include "localization.h"
#include "hardware_monitor.h"
#include "hotkeys.h"
#include "../config/app_settings.h"
#include "../platform/app_paths.h"
#include "../platform/embedded_resources.h"
#include <Windows.h>
#include <Shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace Onboarding {

    namespace {
        Step g_current_step = Step::Welcome;
        bool g_active = false;
        std::vector<WizardStep> g_steps;
        
        void InitSteps() {
            g_steps.clear();
            
            // Welcome
            g_steps.push_back({
                Step::Welcome,
                "Welcome to OmniGhost",
                "Configure your environment in just a few steps.",
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
                "Checking connected hardware and input devices.",
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
    }

    void Shutdown() {
        g_active = false;
    }

    bool ShouldRunOnboarding() {
        return !app_settings::config.onboarding_completed ||
            !app_settings::config.license_accepted;
    }

    void Start() {
        if (!ShouldRunOnboarding()) return;
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
        if (!app_settings::config.license_accepted) {
            CyberWidgets::Notify("Accept the License Agreement before finishing setup", CyberWidgets::ToastType::Warning);
            return;
        }
        app_settings::config.onboarding_completed = true;
        std::string save_error;
        if (!app_settings::SaveGlobal(&save_error)) {
            app_settings::config.onboarding_completed = false;
            CyberWidgets::Notify(save_error.empty() ? "Could not save setup. Please try again."
                : save_error.c_str(), CyberWidgets::ToastType::Error);
            return;
        }
        g_active = false;
        CyberWidgets::Notify("Onboarding completed!", CyberWidgets::ToastType::Success);
    }

    void Skip() {
        Complete();
    }

    void DrawStepContent(const WizardStep& step);

    namespace {
        constexpr float kContentInset = 30.0f;

        void DrawCheck(ImDrawList* dl, const ImVec2& center, ImU32 color) {
            dl->AddLine(ImVec2(center.x - 4.0f, center.y), ImVec2(center.x - 1.0f, center.y + 3.0f), color, 1.7f);
            dl->AddLine(ImVec2(center.x - 1.0f, center.y + 3.0f), ImVec2(center.x + 5.0f, center.y - 4.0f), color, 1.7f);
        }

        void DrawStepIndicator(Step current, const ImVec2& origin, float width) {
            static const char* labels[] = { "WELCOME", "LICENSE", "HARDWARE" };
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int active = static_cast<int>(current);
            const float segment = width / 3.0f;

            for (int index = 0; index < 3; ++index) {
                const float x = origin.x + segment * index + 7.0f;
                const bool complete = index < active;
                const bool isCurrent = index == active;
                const ImVec4 tone = complete || isCurrent ? CyberTheme::Colors.Gold : CyberTheme::Colors.TextDisabled;
                const ImVec2 dot(x, origin.y + 7.0f);

                if (index > 0) {
                    const ImU32 lineColor = index <= active
                        ? CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.72f)
                        : CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.85f);
                    dl->AddLine(ImVec2(x - segment + 16.0f, dot.y), ImVec2(x - 13.0f, dot.y), lineColor, 1.0f);
                }

                if (isCurrent) {
                    dl->AddCircleFilled(dot, 8.0f, CyberTheme::WithAlpha(CyberTheme::Colors.GoldGlow, 0.22f));
                    dl->AddCircleFilled(dot, 4.0f, CyberTheme::U32(CyberTheme::Colors.Gold));
                } else {
                    dl->AddCircleFilled(dot, 4.0f, complete
                        ? CyberTheme::U32(CyberTheme::Colors.Gold)
                        : CyberTheme::WithAlpha(CyberTheme::Colors.PanelHover, 0.95f));
                    if (complete) DrawCheck(dl, dot, CyberTheme::U32(CyberTheme::Colors.Background));
                }
                dl->AddText(ImVec2(x + 12.0f, origin.y), CyberTheme::U32(tone), labels[index]);
            }
        }

        void DrawFeatureGlyph(ImDrawList* dl, const ImVec2& center, int kind, ImU32 color) {
            if (kind == 0) {
                dl->AddCircle(center, 6.0f, color, 12, 1.2f);
                dl->AddLine(ImVec2(center.x - 9.0f, center.y), ImVec2(center.x + 9.0f, center.y), color, 1.2f);
            } else if (kind == 1) {
                dl->AddRect(ImVec2(center.x - 6.0f, center.y - 5.0f), ImVec2(center.x + 6.0f, center.y + 5.0f), color, 2.0f, 0, 1.2f);
                dl->AddLine(ImVec2(center.x - 10.0f, center.y - 5.0f), ImVec2(center.x - 7.0f, center.y - 5.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x - 10.0f, center.y + 5.0f), ImVec2(center.x - 7.0f, center.y + 5.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x + 7.0f, center.y - 5.0f), ImVec2(center.x + 10.0f, center.y - 5.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x + 7.0f, center.y + 5.0f), ImVec2(center.x + 10.0f, center.y + 5.0f), color, 1.2f);
            } else if (kind == 2) {
                dl->AddLine(ImVec2(center.x, center.y - 9.0f), ImVec2(center.x - 6.0f, center.y + 1.0f), color, 1.5f);
                dl->AddLine(ImVec2(center.x - 6.0f, center.y + 1.0f), ImVec2(center.x + 1.0f, center.y + 1.0f), color, 1.5f);
                dl->AddLine(ImVec2(center.x + 1.0f, center.y + 1.0f), ImVec2(center.x - 1.0f, center.y + 9.0f), color, 1.5f);
                dl->AddLine(ImVec2(center.x - 1.0f, center.y + 9.0f), ImVec2(center.x + 7.0f, center.y - 2.0f), color, 1.5f);
            } else {
                dl->AddCircle(center, 3.0f, color, 12, 1.2f);
                dl->AddLine(ImVec2(center.x, center.y - 9.0f), ImVec2(center.x, center.y - 5.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x, center.y + 5.0f), ImVec2(center.x, center.y + 9.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x - 9.0f, center.y), ImVec2(center.x - 5.0f, center.y), color, 1.2f);
                dl->AddLine(ImVec2(center.x + 5.0f, center.y), ImVec2(center.x + 9.0f, center.y), color, 1.2f);
            }
        }

        void DrawFeatureCard(const char* id, const char* title, const char* description, int icon) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            constexpr float height = 59.0f;
            ImGui::InvisibleButton(id, ImVec2(width, height));
            const bool hovered = ImGui::IsItemHovered();
            const ImVec4 surface = hovered ? CyberTheme::Colors.CardHover : CyberTheme::Colors.Card;
            const ImU32 accent = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, hovered ? 0.82f : 0.54f);
            dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), CyberTheme::U32(surface), CyberTheme::Radius::Sm);
            dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), CyberTheme::WithAlpha(CyberTheme::Colors.Border, hovered ? 1.0f : 0.65f), CyberTheme::Radius::Sm);
            dl->AddLine(ImVec2(pos.x, pos.y + 10.0f), ImVec2(pos.x, pos.y + height - 10.0f), accent, 1.5f);
            DrawFeatureGlyph(dl, ImVec2(pos.x + 27.0f, pos.y + height * 0.5f), icon, accent);
            dl->AddText(ImVec2(pos.x + 52.0f, pos.y + 11.0f), CyberTheme::U32(CyberTheme::Colors.Text), title);
            dl->AddText(ImVec2(pos.x + 52.0f, pos.y + 31.0f), CyberTheme::U32(CyberTheme::Colors.TextDisabled), description);
            ImGui::Dummy(ImVec2(0.0f, 7.0f));
        }

        void DrawStatusBadge(ImDrawList* dl, const ImVec2& right, const char* label, const ImVec4& color) {
            const ImVec2 text = ImGui::CalcTextSize(label);
            const ImVec2 min(right.x - text.x - 28.0f, right.y);
            const ImVec2 max(right.x, right.y + 22.0f);
            dl->AddRectFilled(min, max, CyberTheme::WithAlpha(color, 0.13f), 5.0f);
            dl->AddRect(min, max, CyberTheme::WithAlpha(color, 0.34f), 5.0f);
            dl->AddCircleFilled(ImVec2(min.x + 10.0f, min.y + 11.0f), 3.0f, CyberTheme::U32(color));
            dl->AddText(ImVec2(min.x + 17.0f, min.y + 5.0f), CyberTheme::U32(color), label);
        }

        bool OpenLicenseAgreement() {
            constexpr std::string_view kLicenseResource = "legal/OmniGhost_EULA_and_Terms_of_Use.pdf";
            const auto bytes = OmniGhost::LoadEmbeddedResource(kLicenseResource);
            if (!bytes || bytes->empty()) {
                CyberWidgets::Notify("Could not load the License Agreement", CyberWidgets::ToastType::Error);
                return false;
            }

            const std::filesystem::path directory = OmniGhost::Paths::LocalData() / L"documents";
            const std::filesystem::path destination = directory / L"OmniGhost_EULA_and_Terms_of_Use.pdf";
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error) {
                CyberWidgets::Notify("Could not prepare the License Agreement", CyberWidgets::ToastType::Error);
                return false;
            }

            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            if (!output) {
                CyberWidgets::Notify("Could not save the License Agreement", CyberWidgets::ToastType::Error);
                return false;
            }
            output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
            output.close();
            if (!output) {
                CyberWidgets::Notify("Could not save the License Agreement", CyberWidgets::ToastType::Error);
                return false;
            }

            const auto result = ShellExecuteW(nullptr, L"open", destination.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32) {
                CyberWidgets::Notify("Could not open the License Agreement", CyberWidgets::ToastType::Error);
                return false;
            }
            return true;
        }
    }

    void DrawHardwareRow(const char* label, const char* role,
                         const HardwareMonitor::DeviceStatus& status,
                         bool optional) {
       ImDrawList* dl = ImGui::GetWindowDrawList();
       const ImVec2 pos = ImGui::GetCursorScreenPos();
       const float width = ImGui::GetContentRegionAvail().x;
        constexpr float height = 82.0f;
       const ImVec4 stateColor = status.connected
           ? CyberTheme::Colors.Success
           : (status.port == "Checking" ? CyberTheme::Colors.Warning : CyberTheme::Colors.TextDisabled);
       const char* state = status.connected
            ? "CONNECTED"
            : (status.port == "Checking" ? "SEARCHING" : "NOT FOUND");

       dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
           CyberTheme::U32(CyberTheme::Colors.Card), CyberTheme::Radius::Sm);
       dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
           CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.65f), CyberTheme::Radius::Sm);
        DrawFeatureGlyph(dl, ImVec2(pos.x + 28.0f, pos.y + 29.0f), optional ? 1 : 3, CyberTheme::U32(CyberTheme::Colors.Gold));
        dl->AddText(ImVec2(pos.x + 52.0f, pos.y + 16.0f), CyberTheme::U32(CyberTheme::Colors.Text), label);
        dl->AddText(ImVec2(pos.x + 52.0f, pos.y + 39.0f), CyberTheme::U32(CyberTheme::Colors.TextDisabled), role);
        const char* badge = optional ? "OPTIONAL" : "REQUIRED";
        const ImVec4 badgeColor = optional ? CyberTheme::Colors.TextDisabled : CyberTheme::Colors.Gold;
        DrawStatusBadge(dl, ImVec2(pos.x + width - 14.0f, pos.y + 14.0f), state, stateColor);
        if (status.port == "Checking") {
            const float pulse = 4.0f + (std::sin(static_cast<float>(ImGui::GetTime()) * 5.0f) + 1.0f) * 2.0f;
            dl->AddCircle(ImVec2(pos.x + width - 14.0f - ImGui::CalcTextSize(state).x - 18.0f, pos.y + 25.0f),
                pulse, CyberTheme::WithAlpha(CyberTheme::Colors.Warning, 0.35f), 16, 1.0f);
        }
        const ImVec2 badgeSize = ImGui::CalcTextSize(badge);
        dl->AddText(ImVec2(pos.x + width - badgeSize.x - 14.0f, pos.y + 51.0f), CyberTheme::U32(badgeColor), badge);
        ImGui::Dummy(ImVec2(width, height + 9.0f));
    }

    void DrawWizard() {
        if (!g_active) return;
        
        const WizardStep& step = GetStepInfo(g_current_step);
        int step_idx = static_cast<int>(g_current_step);
        int total_steps = static_cast<int>(Step::Count) - 1; // Exclude Complete
        
        ImGui::OpenPopup("##onboarding_wizard");
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 center = viewport->GetCenter();
        const ImVec2 work_size = viewport->WorkSize;
        const ImVec2 wizard_size(
            std::min(760.0f, std::max(1.0f, work_size.x - 32.0f)),
            std::min(600.0f, std::max(1.0f, work_size.y - 32.0f)));
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(wizard_size, ImGuiCond_Appearing);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.99f));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 188));
        ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.26f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Lg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        if (ImGui::BeginPopupModal("##onboarding_wizard", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 win_pos = ImGui::GetWindowPos();
            ImVec2 win_size = ImGui::GetWindowSize();
            CyberTheme::DrawRadialAccent(dl,
                ImVec2(win_pos.x + win_size.x * 0.72f, win_pos.y + 16.0f), 220.0f, 0.045f);
            CyberTheme::DrawSubtleNoise(dl, win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y), 0.015f);

            ImGui::SetCursorPos(ImVec2(kContentInset, 22.0f));
            DrawStepIndicator(g_current_step, ImGui::GetCursorScreenPos(), win_size.x - kContentInset * 2.0f);

            const float logo_space = BrandAssets::GetLogoTexture() ? 57.0f : 0.0f;
            if (BrandAssets::GetLogoTexture()) {
                const float scale = std::min(42.0f / static_cast<float>(BrandAssets::GetLogoWidth()),
                                             42.0f / static_cast<float>(BrandAssets::GetLogoHeight()));
                const ImVec2 logo_size(BrandAssets::GetLogoWidth() * scale, BrandAssets::GetLogoHeight() * scale);
                ImGui::SetCursorPos(ImVec2(kContentInset, 64.0f));
                ImGui::Image(BrandAssets::GetLogoTexture(), logo_size);
            }

            ImGui::SetCursorPos(ImVec2(kContentInset + logo_space, 64.0f));
            ImGui::TextColored(CyberTheme::Colors.Gold, "OMNIGHOST  /  SETUP");
            ImGui::SetCursorPos(ImVec2(kContentInset + logo_space, 84.0f));
            ImFont* title_font = CyberFonts::GetTitleFont();
            if (title_font) {
                ImGui::PushFont(title_font);
            }
            ImGui::TextUnformatted(step.title.c_str());
            if (title_font) {
                ImGui::PopFont();
            }
            ImGui::SetCursorPos(ImVec2(kContentInset + logo_space, 117.0f));
            ImGui::PushTextWrapPos(win_pos.x + win_size.x - kContentInset);
            ImGui::TextColored(CyberTheme::Colors.TextDisabled, "%s", step.description.c_str());
            ImGui::PopTextWrapPos();

            ImGui::SetCursorPos(ImVec2(kContentInset, 153.0f));
            dl->AddLine(ImGui::GetCursorScreenPos(), ImVec2(win_pos.x + win_size.x - 28.0f, ImGui::GetCursorScreenPos().y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.75f));

            ImGui::SetCursorPos(ImVec2(kContentInset, 169.0f));
            const float footer_height = 70.0f;
            ImGui::BeginChild("##onboarding_content", ImVec2(win_size.x - kContentInset * 2.0f, win_size.y - 169.0f - footer_height), false);
            DrawStepContent(step);
            ImGui::EndChild();

            ImGui::SetCursorPos(ImVec2(kContentInset, win_size.y - footer_height));
            dl->AddLine(ImGui::GetCursorScreenPos(), ImVec2(win_pos.x + win_size.x - 28.0f, ImGui::GetCursorScreenPos().y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.75f));
            ImGui::SetCursorPos(ImVec2(kContentInset, win_size.y - 52.0f));
            const float btn_width = step_idx >= total_steps - 1 ? 156.0f : 136.0f;
            if (step_idx > 0) {
                if (CyberWidgets::Button("<- Back", CyberWidgets::ButtonStyle::Secondary, ImVec2(110.0f, 38.0f))) {
                    PreviousStep();
                }
            }

            ImGui::SetCursorPos(ImVec2(win_size.x - kContentInset - btn_width, win_size.y - 52.0f));
            bool is_last = step_idx >= total_steps - 1;
            const bool license_accepted = app_settings::config.license_accepted;
            if (is_last) {
                if (CyberWidgets::Button("Finish Setup ->", CyberWidgets::ButtonStyle::Primary, ImVec2(btn_width, 38.0f))) {
                    Complete();
                }
            } else {
                const bool enabled = g_current_step != Step::LicenseAgreement || license_accepted;
                if (CyberWidgets::Button("Continue ->", CyberWidgets::ButtonStyle::Primary, ImVec2(btn_width, 38.0f), enabled)) {
                    NextStep();
                }
            }
            
            if (!g_active)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        
    }

    void DrawStepContent(const WizardStep& step) {
        switch (step.id) {
            case Step::Welcome: {
                ImGui::TextColored(CyberTheme::Colors.Gold, "DMA SYSTEM  /  HARDWARE ACCELERATED");
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                DrawFeatureCard("##feature_realtime", "Real-time workspace", "Clear controls and live system feedback", 0);
                DrawFeatureCard("##feature_input", "Precision input", "Compatible with supported external devices", 1);
                DrawFeatureCard("##feature_library", "Multi-game library", "Game-specific settings in one place", 2);
                DrawFeatureCard("##feature_profiles", "Profiles & preferences", "Keep your configuration organised", 3);
                break;
            }
            case Step::LicenseAgreement: {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 card_pos = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                constexpr float card_height = 132.0f;
                dl->AddRectFilled(card_pos, ImVec2(card_pos.x + width, card_pos.y + card_height),
                    CyberTheme::U32(CyberTheme::Colors.Card), CyberTheme::Radius::Sm);
                dl->AddRect(card_pos, ImVec2(card_pos.x + width, card_pos.y + card_height),
                    CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.75f), CyberTheme::Radius::Sm);
                DrawFeatureGlyph(dl, ImVec2(card_pos.x + 28.0f, card_pos.y + 29.0f), 1,
                    CyberTheme::U32(CyberTheme::Colors.Gold));
                dl->AddText(ImVec2(card_pos.x + 52.0f, card_pos.y + 17.0f),
                    CyberTheme::U32(CyberTheme::Colors.Text), "OmniGhost License Agreement");
                dl->AddText(ImVec2(card_pos.x + 52.0f, card_pos.y + 42.0f),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), "Terms of use, limitations and conditions.");
                dl->AddText(ImVec2(card_pos.x + 52.0f, card_pos.y + 62.0f),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), "Opens in your default PDF viewer.");

                ImGui::SetCursorPos(ImVec2(14.0f, 90.0f));
                if (CyberWidgets::Button("View Agreement ->", CyberWidgets::ButtonStyle::Secondary,
                    ImVec2(150.0f, 30.0f))) {
                    OpenLicenseAgreement();
                }
                ImGui::SameLine();
                if (CyberWidgets::Button("License Agreement ->", CyberWidgets::ButtonStyle::Ghost,
                    ImVec2(166.0f, 30.0f))) {
                    OpenLicenseAgreement();
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16.0f);

                const ImVec2 accept_pos = ImGui::GetCursorScreenPos();
                constexpr float accept_height = 46.0f;
                const bool accepted = app_settings::config.license_accepted;
                ImGui::InvisibleButton("##license_acceptance", ImVec2(width, accept_height));
                if (ImGui::IsItemClicked()) {
                    app_settings::config.license_accepted = !accepted;
                }
                const bool current = app_settings::config.license_accepted;
                const ImVec4 control_color = current ? CyberTheme::Colors.Gold : CyberTheme::Colors.Border;
                dl->AddRectFilled(accept_pos, ImVec2(accept_pos.x + width, accept_pos.y + accept_height),
                    CyberTheme::WithAlpha(current ? CyberTheme::Colors.Gold : CyberTheme::Colors.Card, current ? 0.12f : 1.0f), CyberTheme::Radius::Sm);
                dl->AddRect(accept_pos, ImVec2(accept_pos.x + width, accept_pos.y + accept_height),
                    CyberTheme::WithAlpha(control_color, current ? 0.7f : 0.95f), CyberTheme::Radius::Sm);
                const ImVec2 check_pos(accept_pos.x + 20.0f, accept_pos.y + accept_height * 0.5f);
                dl->AddRectFilled(ImVec2(check_pos.x - 8.0f, check_pos.y - 8.0f), ImVec2(check_pos.x + 8.0f, check_pos.y + 8.0f),
                    current ? CyberTheme::U32(CyberTheme::Colors.Gold) : CyberTheme::U32(CyberTheme::Colors.Panel), 4.0f);
                if (current) DrawCheck(dl, check_pos, CyberTheme::U32(CyberTheme::Colors.Background));
                dl->AddText(ImVec2(accept_pos.x + 42.0f, accept_pos.y + 15.0f), CyberTheme::U32(CyberTheme::Colors.Text),
                    "I have read and accept the License Agreement");
                break;
            }
            case Step::HardwareDetection: {
                ImGui::TextColored(CyberTheme::Colors.TextDisabled, "Hardware status  /  scanning connected interfaces");
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                HardwareMonitor::Update();
                const auto& dma = HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::DMA);
                const auto& makcu = HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::Makcu);
                DrawHardwareRow("DMA / FPGA", "Primary hardware interface", dma, false);
                DrawHardwareRow("MAKCU", "External input controller", makcu, true);
                const int detected = (dma.connected ? 1 : 0) + (makcu.connected ? 1 : 0);
                ImGui::TextColored(CyberTheme::Colors.TextDisabled, "%d device%s detected. Detection runs in the background.", detected, detected == 1 ? "" : "s");
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
        app_settings::config.onboarding_completed = completed &&
            app_settings::config.license_accepted;
        app_settings::SaveGlobal(nullptr);
    }

    bool IsCompleted() {
        return !ShouldRunOnboarding();
    }

    void Reset() {
        g_active = false;
        g_current_step = Step::Welcome;
        app_settings::config.onboarding_completed = false;
        app_settings::SaveGlobal(nullptr);
    }

} // namespace Onboarding
