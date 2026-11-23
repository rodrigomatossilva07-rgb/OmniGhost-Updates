#include "onboarding.h"
#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "brand_assets.h"
#include "localization.h"
#include "../platform/ihardware_manager.h"
#include "../platform/file_integrity.h"
#include "hotkeys.h"
#include "animations.h"
#include "../config/app_settings.h"
#include "../platform/app_paths.h"
#include "../platform/embedded_resources.h"
#include <Windows.h>
#include <Shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace Onboarding {

    namespace {
        Step g_current_step = Step::Welcome;
        bool g_active = false;
        bool g_acceptance = false;
        bool g_refresh_requested = false;
        double g_step_started = 0.0;
        double g_last_refresh = 0.0;
        double g_last_result = 0.0;
        double g_finish_started = 0.0;
        bool g_finish_pending = false;
        const char* g_notice = nullptr;
        OmniGhost::Platform::HardwareState g_hardware;
        std::vector<WizardStep> g_steps;

        constexpr std::string_view kLicenseResource = "legal/OmniGhost_EULA_and_Terms_of_Use.pdf";

        const char* Tr(const char* en, const char* pt) {
            return app_settings::config.language == app_settings::Language::PT ? pt : en;
        }

        const std::string& AgreementDigest() {
            static const std::string digest = [] {
                std::string value, error;
                const auto bytes = OmniGhost::LoadEmbeddedResource(kLicenseResource);
                if (!bytes || bytes->empty() ||
                    !OmniGhost::Platform::Sha256Bytes(bytes->data(), bytes->size(), value, error))
                    value.clear();
                return value;
            }();
            return digest;
        }

        bool HardwareCheckFinished() {
            return g_hardware.dmaFinished;
        }

        void ChangeStep(Step step) {
            g_current_step = step;
            g_step_started = ImGui::GetTime();
            if (step == Step::HardwareDetection) g_last_refresh = g_step_started;
            g_notice = nullptr;
        }

        void InitSteps() {
            g_steps.clear();


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


            g_steps.push_back({
                Step::LicenseAgreement,
                "License Agreement",
                "Please review and accept the license agreement to continue.",
                "📜",
                false,
                []() { return g_acceptance && !AgreementDigest().empty(); },
                []() {},
                []() {}
            });


            g_steps.push_back({
                Step::HardwareDetection,
                "Hardware Detection",
                "Checking connected hardware and input devices.",
                "🔧",
                false,
                HardwareCheckFinished,
                []() {},
                []() {}
            });


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
            !app_settings::config.license_accepted || AgreementDigest().empty() ||
            app_settings::config.license_agreement_digest != AgreementDigest();
    }

    void Start() {
        if (!ShouldRunOnboarding()) return;
        g_active = true;
        g_acceptance = false;
        g_finish_pending = false;
        g_refresh_requested = false;
        g_last_refresh = ImGui::GetTime();
        ChangeStep(Step::Welcome);
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
        if (!g_active || g_finish_pending || g_current_step >= Step::HardwareDetection)
            return false;
        const WizardStep& current = GetStepInfo(g_current_step);
        if (current.validator && !current.validator()) {
            CyberWidgets::Notify(Tr("Complete the current step first.", "Conclui primeiro a etapa atual."),
                                 CyberWidgets::ToastType::Warning);
            return false;
        }

        if (current.onExit) current.onExit();

        int next = static_cast<int>(g_current_step) + 1;
        if (next < static_cast<int>(Step::Count)) {
            ChangeStep(static_cast<Step>(next));
            const WizardStep& next_step = GetStepInfo(g_current_step);
            if (next_step.onEnter) next_step.onEnter();
            return true;
        }
        return false;
    }

    bool PreviousStep() {
        if (!g_active || g_finish_pending) return false;
        int prev = static_cast<int>(g_current_step) - 1;
        if (prev >= 0) {
            const WizardStep& current = GetStepInfo(g_current_step);
            if (current.onExit) current.onExit();

            ChangeStep(static_cast<Step>(prev));
            const WizardStep& prev_step = GetStepInfo(g_current_step);
            if (prev_step.onEnter) prev_step.onEnter();
            return true;
        }
        return false;
    }

    bool GoToStep(Step step) {
        if (static_cast<int>(step) == static_cast<int>(g_current_step) - 1) return PreviousStep();
        if (static_cast<int>(step) == static_cast<int>(g_current_step) + 1) return NextStep();
        return false;
    }

    void Complete() {
        if (!g_active || g_finish_pending || g_current_step != Step::HardwareDetection ||
            !g_acceptance || AgreementDigest().empty() || !HardwareCheckFinished())
            return;
        g_notice = nullptr;
        g_finish_pending = true;
        g_finish_started = ImGui::GetTime();
    }

    bool UpdateHardware(const OmniGhost::Platform::HardwareState& state) {
        if (state.dmaFinished && (!g_hardware.dmaFinished || g_last_result == 0.0))
            g_last_result = ImGui::GetTime();
        g_hardware = state;
        if (!g_active || g_finish_pending || g_current_step != Step::HardwareDetection ||
            state.stage != OmniGhost::Platform::HardwareState::ProbeStage::Complete || !state.makcuFinished)
            return false;
        const double now = ImGui::GetTime();
        if (!g_refresh_requested && now - g_last_refresh < 12.0) return false;
        g_refresh_requested = false;
        g_last_refresh = now;
        return true;
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
            const char* labels[] = { Tr("WELCOME", "BOAS-VINDAS"), Tr("LICENSE", "ACORDO"), "HARDWARE" };
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int active = (std::min)(static_cast<int>(current), 2);
            const float segment = width / 3.0f;

            dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + 52.0f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Card, 0.72f), CyberTheme::Radius::Sm);
            dl->AddRect(origin, ImVec2(origin.x + width, origin.y + 52.0f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.58f), CyberTheme::Radius::Sm);

            for (int index = 0; index < 3; ++index) {
                const float x = origin.x + segment * (index + 0.5f);
                const bool complete = index < active;
                const bool isCurrent = index == active;
                const ImVec4 tone = complete || isCurrent ? CyberTheme::Colors.Gold : CyberTheme::Colors.TextDisabled;
                const ImVec2 dot(x, origin.y + 17.0f);

                if (index > 0) {
                    const float left = x - segment + 14.0f;
                    const float right = x - 14.0f;
                    dl->AddLine(ImVec2(left, dot.y), ImVec2(right, dot.y),
                        CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.85f), 1.5f);
                    if (index <= active) {
                        const float fill = index == active && app_settings::MotionEnabled()
                            ? std::clamp(static_cast<float>((ImGui::GetTime() - g_step_started) / 0.22), 0.f, 1.f)
                            : 1.f;
                        dl->AddLine(ImVec2(left, dot.y), ImVec2(left + (right - left) * fill, dot.y),
                            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.72f), 1.5f);
                    }
                }

                if (isCurrent) {
                    float halo = 12.0f;
                    float haloAlpha = 0.24f;
                    if (app_settings::MotionEnabled() &&
                        app_settings::config.animation_intensity == app_settings::EffectLevel::Full) {
                        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.8f);
                        halo += pulse * 4.0f;
                        haloAlpha += pulse * 0.10f;
                    }
                    dl->AddCircleFilled(dot, halo, CyberTheme::WithAlpha(CyberTheme::Colors.GoldGlow, haloAlpha));
                    dl->AddCircleFilled(dot, 8.0f, CyberTheme::U32(CyberTheme::Colors.Gold));
                } else {
                    dl->AddCircleFilled(dot, 8.0f, complete
                        ? CyberTheme::U32(CyberTheme::Colors.Gold)
                        : CyberTheme::WithAlpha(CyberTheme::Colors.PanelHover, 0.95f));
                    if (complete) DrawCheck(dl, dot, CyberTheme::U32(CyberTheme::Colors.Background));
                }
                if (!complete) {
                    const std::string number = std::to_string(index + 1);
                    const ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
                    dl->AddText(ImVec2(dot.x - numberSize.x * 0.5f, dot.y - numberSize.y * 0.5f),
                        isCurrent ? CyberTheme::U32(CyberTheme::Colors.Background)
                                  : CyberTheme::U32(CyberTheme::Colors.TextDisabled), number.c_str());
                }
                const ImVec2 labelSize = ImGui::CalcTextSize(labels[index]);
                dl->AddText(ImVec2(x - labelSize.x * 0.5f, origin.y + 32.0f), CyberTheme::U32(tone), labels[index]);
            }
        }

        void DrawFeatureGlyph(ImDrawList* dl, const ImVec2& center, int kind, ImU32 color) {
            dl->AddCircleFilled(center, 17.0f,
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.075f), 28);
            dl->AddCircle(center, 17.0f,
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.20f), 28, 1.0f);
            if (kind == 0) {
                dl->AddCircle(center, 6.0f, color, 20, 1.5f);
                dl->AddCircleFilled(center, 2.2f, color, 12);
                dl->AddLine(ImVec2(center.x - 10.0f, center.y), ImVec2(center.x - 7.0f, center.y), color, 1.5f);
                dl->AddLine(ImVec2(center.x + 7.0f, center.y), ImVec2(center.x + 10.0f, center.y), color, 1.5f);
            } else if (kind == 1) {
                dl->AddRect(ImVec2(center.x - 6.0f, center.y - 5.0f), ImVec2(center.x + 6.0f, center.y + 5.0f), color, 2.0f, 0, 1.5f);
                dl->AddLine(ImVec2(center.x - 10.0f, center.y - 5.0f), ImVec2(center.x - 7.0f, center.y - 5.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x - 10.0f, center.y + 5.0f), ImVec2(center.x - 7.0f, center.y + 5.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x + 7.0f, center.y - 5.0f), ImVec2(center.x + 10.0f, center.y - 5.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x + 7.0f, center.y + 5.0f), ImVec2(center.x + 10.0f, center.y + 5.0f), color, 1.2f);
            } else if (kind == 2) {
                dl->AddLine(ImVec2(center.x, center.y - 9.0f), ImVec2(center.x - 6.0f, center.y + 1.0f), color, 1.7f);
                dl->AddLine(ImVec2(center.x - 6.0f, center.y + 1.0f), ImVec2(center.x + 1.0f, center.y + 1.0f), color, 1.5f);
                dl->AddLine(ImVec2(center.x + 1.0f, center.y + 1.0f), ImVec2(center.x - 1.0f, center.y + 9.0f), color, 1.5f);
                dl->AddLine(ImVec2(center.x - 1.0f, center.y + 9.0f), ImVec2(center.x + 7.0f, center.y - 2.0f), color, 1.5f);
            } else {
                dl->AddCircle(center, 3.0f, color, 16, 1.5f);
                dl->AddLine(ImVec2(center.x, center.y - 9.0f), ImVec2(center.x, center.y - 5.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x, center.y + 5.0f), ImVec2(center.x, center.y + 9.0f), color, 1.2f);
                dl->AddLine(ImVec2(center.x - 9.0f, center.y), ImVec2(center.x - 5.0f, center.y), color, 1.2f);
                dl->AddLine(ImVec2(center.x + 5.0f, center.y), ImVec2(center.x + 9.0f, center.y), color, 1.2f);
            }
        }

        void DrawFeatureCard(const char* title, const char* description, int icon) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float textWidth = (std::max)(1.f, width - 76.f);
            const float titleHeight = ImGui::CalcTextSize(title, nullptr, false, textWidth).y;
            const float descriptionHeight = ImGui::CalcTextSize(description, nullptr, false, textWidth).y;
            const float height = (std::max)(63.f, 26.f + titleHeight + descriptionHeight);
            const ImU32 accent = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.64f);
            dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), CyberTheme::U32(CyberTheme::Colors.Card), CyberTheme::Radius::Sm);
            dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.8f), CyberTheme::Radius::Sm);
            dl->AddLine(ImVec2(pos.x, pos.y + 12.0f), ImVec2(pos.x, pos.y + height - 12.0f), accent, 2.0f);
            DrawFeatureGlyph(dl, ImVec2(pos.x + 31.0f, pos.y + height * 0.5f), icon, accent);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + 62.f, pos.y + 11.f),
                CyberTheme::U32(CyberTheme::Colors.Text), title, nullptr, textWidth);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + 62.f, pos.y + 14.f + titleHeight),
                CyberTheme::U32(CyberTheme::Mix(CyberTheme::Colors.TextDisabled, CyberTheme::Colors.Text, 0.22f)),
                description, nullptr, textWidth);
            ImGui::Dummy(ImVec2(width, height + 4.f));
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

        bool OpenAgreementPdf() {
            const auto bytes = OmniGhost::LoadEmbeddedResource(kLicenseResource);
            if (!bytes || bytes->empty() || AgreementDigest().empty()) return false;

            const std::filesystem::path directory = OmniGhost::Paths::LocalData() / L"documents";
            const auto destination = directory / ("OmniGhost_EULA_" + AgreementDigest() + ".pdf");
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error) return false;
            std::string actualDigest, hashError;
            if (!OmniGhost::Platform::Sha256File(destination, actualDigest, hashError) ||
                actualDigest != AgreementDigest()) {
                auto temporary = destination;
                temporary += L".tmp";
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
                output.close();
                if (!output || !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    std::filesystem::remove(temporary, error);
                    return false;
                }
            }

            const auto result = ShellExecuteW(nullptr, L"open", destination.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return reinterpret_cast<INT_PTR>(result) > 32;
        }
    }

    bool OpenLicenseAgreement() {
        if (OpenAgreementPdf()) return true;
        const char* message = Tr("Could not open the agreement. Check that a PDF viewer is installed and try again.",
                                 "Não foi possível abrir o acordo. Verifica se tens um leitor de PDF e tenta novamente.");
        if (g_active) g_notice = message;
        else CyberWidgets::Notify(message, CyberWidgets::ToastType::Error);
        return false;
    }

    void DrawHardwareRow(const char* label, const char* role, bool detected,
                         bool finished, bool timedOut, bool optional) {
       ImDrawList* dl = ImGui::GetWindowDrawList();
       const ImVec2 pos = ImGui::GetCursorScreenPos();
       const float width = ImGui::GetContentRegionAvail().x;
       constexpr float height = 82.0f;
       const ImVec4 stateColor = !finished ? CyberTheme::Colors.Warning :
           timedOut ? CyberTheme::Colors.Error :
           detected ? CyberTheme::Colors.Success : CyberTheme::Colors.TextDisabled;
       const char* state = !finished ? Tr("DETECTING", "A DETETAR") :
           timedOut ? Tr("TIMEOUT", "TEMPO ESGOTADO") :
           detected ? (optional ? Tr("CONNECTED", "LIGADO") : Tr("DETECTED", "DETETADO")) :
           Tr("DISCONNECTED", "DESLIGADO");

       dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
           CyberTheme::U32(CyberTheme::Colors.Card), CyberTheme::Radius::Sm);
       dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
           CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.65f), CyberTheme::Radius::Sm);
        DrawFeatureGlyph(dl, ImVec2(pos.x + 28.0f, pos.y + 29.0f), optional ? 1 : 3, CyberTheme::U32(CyberTheme::Colors.Gold));
        dl->AddText(ImVec2(pos.x + 52.0f, pos.y + 16.0f), CyberTheme::U32(CyberTheme::Colors.Text), label);
        dl->AddText(ImVec2(pos.x + 52.0f, pos.y + 39.0f), CyberTheme::U32(CyberTheme::Colors.TextDisabled), role);
        const char* badge = optional ? Tr("OPTIONAL", "OPCIONAL") : Tr("REQUIRED IN GAME", "NECESSÁRIO EM JOGO");
        const ImVec4 badgeColor = optional ? CyberTheme::Colors.TextDisabled : CyberTheme::Colors.Gold;
        DrawStatusBadge(dl, ImVec2(pos.x + width - 14.0f, pos.y + 14.0f), state, stateColor);
        if (!finished && app_settings::MotionEnabled()) {
            const float pulse = 4.0f + (std::sin(static_cast<float>(ImGui::GetTime()) * 5.0f) + 1.0f) * 2.0f;
            dl->AddCircle(ImVec2(pos.x + width - 14.0f - ImGui::CalcTextSize(state).x - 18.0f, pos.y + 25.0f),
                pulse, CyberTheme::WithAlpha(CyberTheme::Colors.Warning, 0.35f), 16, 1.0f);
        }
        const ImVec2 badgeSize = ImGui::CalcTextSize(badge);
        dl->AddText(ImVec2(pos.x + width - badgeSize.x - 14.0f, pos.y + 51.0f), CyberTheme::U32(badgeColor), badge);
        ImGui::Dummy(ImVec2(width, height + 9.0f));
    }

    namespace {
        void SaveCompletedSetup() {
            auto& settings = app_settings::config;
            const bool previousCompleted = settings.onboarding_completed;
            const bool previousAccepted = settings.license_accepted;
            const auto previousDigest = settings.license_agreement_digest;
            const auto previousDate = settings.license_accepted_at;
            settings.onboarding_completed = true;
            settings.license_accepted = true;
            settings.license_agreement_digest = AgreementDigest();
            settings.license_accepted_at = std::to_string(std::time(nullptr));
            if (!app_settings::SaveGlobal(nullptr)) {
                settings.onboarding_completed = previousCompleted;
                settings.license_accepted = previousAccepted;
                settings.license_agreement_digest = previousDigest;
                settings.license_accepted_at = previousDate;
                g_notice = Tr("Could not save setup. Try again.", "Não foi possível guardar a configuração. Tenta novamente.");
                g_finish_pending = false;
                return;
            }
            g_active = false;
            g_finish_pending = false;
            CyberWidgets::Notify(Tr("Setup complete", "Configuração concluída"), CyberWidgets::ToastType::Success);
        }

        void DrawNavigationIcon(bool back, bool check, bool enabled) {
            const ImVec2 low = ImGui::GetItemRectMin();
            const ImVec2 high = ImGui::GetItemRectMax();
            const ImVec2 center(back ? low.x + 13.f : high.x - 13.f, (low.y + high.y) * 0.5f);
            const ImU32 color = CyberTheme::U32(!enabled ? CyberTheme::Colors.TextDisabled :
                back ? CyberTheme::Colors.Text : CyberTheme::Colors.Background);
            auto* draw = ImGui::GetWindowDrawList();
            if (check) {
                DrawCheck(draw, center, color);
                return;
            }
            const float direction = back ? -1.f : 1.f;
            const ImVec2 tip(center.x + 5.f * direction, center.y);
            draw->AddLine(ImVec2(center.x - 5.f * direction, center.y), tip, color, 1.5f);
            draw->AddLine(tip, ImVec2(center.x + direction, center.y - 4.f), color, 1.5f);
            draw->AddLine(tip, ImVec2(center.x + direction, center.y + 4.f), color, 1.5f);
        }
    }

    void DrawWizard() {
        if (!g_active) return;

        const WizardStep& step = GetStepInfo(g_current_step);
        const bool isLast = g_current_step == Step::HardwareDetection;

        ImGui::OpenPopup("##onboarding_wizard");
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 center = viewport->GetCenter();
        const ImVec2 work_size = viewport->WorkSize;
        const ImVec2 wizard_size(
            std::min(780.0f, std::max(1.0f, work_size.x - 40.0f)),
            std::min(610.0f, std::max(1.0f, work_size.y - 40.0f)));
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(wizard_size, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.99f));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 188));
        ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.26f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Lg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        if (ImGui::BeginPopupModal("##onboarding_wizard", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 win_pos = ImGui::GetWindowPos();
            ImVec2 win_size = ImGui::GetWindowSize();
            dl->AddRectFilledMultiColor(win_pos,
                ImVec2(win_pos.x + win_size.x, win_pos.y + 178.0f),
                CyberTheme::WithAlpha(CyberTheme::Colors.CardHover, 0.92f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.98f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.98f),
                CyberTheme::WithAlpha(CyberTheme::Colors.CardHover, 0.92f));
            CyberTheme::DrawRadialAccent(dl,
                ImVec2(win_pos.x + win_size.x * 0.72f, win_pos.y + 16.0f), 220.0f, 0.045f);
            CyberTheme::DrawSubtleNoise(dl, win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y), 0.015f);

            ImGui::SetCursorPos(ImVec2(kContentInset, 20.0f));
            DrawStepIndicator(g_current_step, ImGui::GetCursorScreenPos(), win_size.x - kContentInset * 2.0f);

            const float logo_space = BrandAssets::GetLogoTexture() ? 57.0f : 0.0f;
            if (BrandAssets::GetLogoTexture()) {
                const float scale = std::min(42.0f / static_cast<float>(BrandAssets::GetLogoWidth()),
                                             42.0f / static_cast<float>(BrandAssets::GetLogoHeight()));
                const ImVec2 logo_size(BrandAssets::GetLogoWidth() * scale, BrandAssets::GetLogoHeight() * scale);
                ImGui::SetCursorPos(ImVec2(kContentInset, 88.0f));
                ImGui::Image(BrandAssets::GetLogoTexture(), logo_size);
            }

            ImGui::SetCursorPos(ImVec2(kContentInset + logo_space, 88.0f));
            ImGui::TextColored(CyberTheme::Colors.Gold, "%s", Tr("OMNIGHOST  /  SETUP", "OMNIGHOST  /  CONFIGURAÇÃO"));
            ImGui::SetCursorPos(ImVec2(kContentInset + logo_space, 108.0f));
            ImFont* title_font = CyberFonts::GetTitleFont();
            if (title_font) {
                ImGui::PushFont(title_font);
            }
            const char* localizedTitle = g_current_step == Step::Welcome ? "Bem-vindo ao OmniGhost" :
                isLast ? "Deteção de hardware" : "Acordo de licença";
            ImGui::TextUnformatted(Tr(step.title.c_str(), localizedTitle));
            if (title_font) {
                ImGui::PopFont();
            }
            ImGui::SetCursorPos(ImVec2(kContentInset + logo_space, 141.0f));
            ImGui::PushTextWrapPos(win_size.x - kContentInset);
            const char* localizedDescription = g_current_step == Step::Welcome ? "Configura o teu ambiente em poucos passos." :
                isLast ? "A verificar o hardware ligado e os dispositivos de entrada." :
                         "Lê e aceita o acordo de licença para continuar.";
            ImGui::TextColored(CyberTheme::Colors.TextDisabled, "%s", Tr(step.description.c_str(), localizedDescription));
            ImGui::PopTextWrapPos();

            ImGui::SetCursorPos(ImVec2(kContentInset, 176.0f));
            dl->AddLine(ImGui::GetCursorScreenPos(), ImVec2(win_pos.x + win_size.x - 28.0f, ImGui::GetCursorScreenPos().y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.75f));

            constexpr float contentTop = 186.f;
            constexpr float footerHeight = 72.f;
            const float contentHeight = (std::max)(1.f, win_size.y - contentTop - footerHeight - 5.f);
            ImGui::SetCursorPos(ImVec2(kContentInset, contentTop));
            ImGui::PushID(static_cast<int>(g_current_step));
            ImGui::BeginChild("##onboarding_content", ImVec2(win_size.x - kContentInset * 2.f, contentHeight));
            const float progress = app_settings::MotionEnabled() ?
                std::clamp(static_cast<float>((ImGui::GetTime() - g_step_started) / 0.18), 0.35f, 1.f) : 1.f;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, progress);
            DrawStepContent(step);
            if (g_notice) {
                ImGui::PushStyleColor(ImGuiCol_Text, CyberTheme::Colors.Error);
                ImGui::TextWrapped("%s", g_notice);
                ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0.f, 4.f));
            if (ImGui::GetScrollY() + 1.f < ImGui::GetScrollMaxY()) {
                const ImVec2 child = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                const ImVec2 tip(child.x + size.x - 21.f, child.y + size.y - 6.f);
                auto* contentDraw = ImGui::GetWindowDrawList();
                const ImU32 gold = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.75f);
                contentDraw->AddLine(ImVec2(tip.x - 4.f, tip.y - 4.f), tip, gold, 1.5f);
                contentDraw->AddLine(tip, ImVec2(tip.x + 4.f, tip.y - 4.f), gold, 1.5f);
            }
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopID();

            ImGui::SetCursorPos(ImVec2(kContentInset, win_size.y - footerHeight));
            dl->AddLine(ImGui::GetCursorScreenPos(), ImVec2(win_pos.x + win_size.x - kContentInset, ImGui::GetCursorScreenPos().y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.75f));
            ImGui::SetCursorPos(ImVec2(kContentInset, win_size.y - 53.f));
            const float halfFooterWidth = (std::max)(1.f, (win_size.x - kContentInset * 2.f - 12.f) * 0.5f);
            bool back = false;
            if (g_current_step != Step::Welcome) {
                back = CyberWidgets::Button(Tr("Back", "Voltar"), CyberWidgets::ButtonStyle::Secondary,
                    ImVec2((std::min)(120.f, halfFooterWidth), 38.f), !g_finish_pending);
                DrawNavigationIcon(true, false, !g_finish_pending);
            }

            const float buttonWidth = (std::min)(120.f, halfFooterWidth);
            ImGui::SetCursorPos(ImVec2(win_size.x - kContentInset - buttonWidth, win_size.y - 53.f));
            const bool enabled = !g_finish_pending && (isLast ? HardwareCheckFinished() && !g_refresh_requested :
                g_current_step != Step::LicenseAgreement || (g_acceptance && !AgreementDigest().empty()));
            const bool forward = CyberWidgets::LoadingButton(
                isLast ? (buttonWidth >= 110.f ? Tr("Finish Setup", "Terminar") : Tr("Finish", "Terminar"))
                       : Tr("Continue", "Continuar"),
                Tr("Finishing setup...", "A guardar..."), g_finish_pending,
                CyberWidgets::ButtonStyle::Primary, ImVec2(buttonWidth, 38.f), enabled);
            if (!g_finish_pending) DrawNavigationIcon(false, isLast, enabled);

            if (back) PreviousStep();
            else if (enabled && (forward || (g_current_step == Step::Welcome &&
                     ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive()))) {
                if (isLast) Complete();
                else NextStep();
            }
            if (g_finish_pending && ImGui::GetTime() - g_finish_started >= 0.12)
                SaveCompletedSetup();

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
                DrawFeatureCard(Tr("Real-time workspace", "Área de trabalho em tempo real"),
                    Tr("Clear controls and live system feedback", "Controlos claros e informação atualizada"), 0);
                DrawFeatureCard(Tr("Precision input", "Controlos de precisão"),
                    Tr("Compatible with supported external devices", "Compatível com dispositivos externos suportados"), 1);
                DrawFeatureCard(Tr("Multi-game library", "Biblioteca de jogos"),
                    Tr("Game-specific settings in one place", "Definições de cada jogo reunidas num só local"), 2);
                DrawFeatureCard(Tr("Profiles & preferences", "Perfis e preferências"),
                    Tr("Keep your configuration organised", "Mantém as tuas configurações organizadas"), 3);
                break;
            }
            case Step::LicenseAgreement: {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 card_pos = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                constexpr float card_height = 144.0f;
                dl->AddRectFilled(card_pos, ImVec2(card_pos.x + width, card_pos.y + card_height),
                    CyberTheme::U32(CyberTheme::Colors.Card), CyberTheme::Radius::Sm);
                dl->AddRect(card_pos, ImVec2(card_pos.x + width, card_pos.y + card_height),
                    CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.75f), CyberTheme::Radius::Sm);
                DrawFeatureGlyph(dl, ImVec2(card_pos.x + 28.0f, card_pos.y + 29.0f), 1,
                    CyberTheme::U32(CyberTheme::Colors.Gold));
                dl->AddText(ImVec2(card_pos.x + 52.0f, card_pos.y + 17.0f),
                    CyberTheme::U32(CyberTheme::Colors.Text), Tr("OmniGhost License Agreement", "Acordo de licença do OmniGhost"));
                dl->AddText(ImVec2(card_pos.x + 52.0f, card_pos.y + 42.0f),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), Tr("Terms of use, limitations and conditions.", "Termos de utilização, limites e condições."));
                dl->AddText(ImVec2(card_pos.x + 52.0f, card_pos.y + 62.0f),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), Tr("Opens in your default PDF viewer.", "Abre no teu leitor de PDF predefinido."));

                ImGui::SetCursorPos(ImVec2(14.0f, 96.0f));
                const bool available = !AgreementDigest().empty();
                if (CyberWidgets::Button(Tr("Open License Agreement", "Abrir acordo de licença"), CyberWidgets::ButtonStyle::Secondary,
                    ImVec2(202.0f, 34.0f), available)) {
                    g_notice = nullptr;
                    OpenLicenseAgreement();
                }
                ImGui::SetCursorPosY(card_height + 8.f);
                if (!available) {
                    ImGui::TextWrapped("%s", Tr("The license agreement is unavailable in this build.",
                                               "O acordo de licença está indisponível nesta versão."));
                    break;
                }

                const ImVec2 accept_pos = ImGui::GetCursorScreenPos();
                const char* label = Tr("I have read and accept the License Agreement", "Li e aceito o acordo de licença");
                const float wrapWidth = (std::max)(1.f, width - 54.f);
                const float labelHeight = ImGui::CalcTextSize(label, nullptr, false, wrapWidth).y;
                const float accept_height = (std::max)(46.f, labelHeight + 22.f);
                if (ImGui::Selectable("##license_acceptance", g_acceptance, 0, ImVec2(width, accept_height)))
                    g_acceptance = !g_acceptance;
                const bool current = g_acceptance;
                const ImVec4 control_color = current ? CyberTheme::Colors.Gold : CyberTheme::Colors.Border;
                dl->AddRectFilled(accept_pos, ImVec2(accept_pos.x + width, accept_pos.y + accept_height),
                    CyberTheme::WithAlpha(current ? CyberTheme::Colors.Gold : CyberTheme::Colors.Card, current ? 0.12f : 1.0f), CyberTheme::Radius::Sm);
                dl->AddRect(accept_pos, ImVec2(accept_pos.x + width, accept_pos.y + accept_height),
                    CyberTheme::WithAlpha(control_color, ImGui::IsItemFocused() ? 1.f : (current ? 0.7f : 0.95f)),
                    CyberTheme::Radius::Sm);
                const ImVec2 check_pos(accept_pos.x + 20.0f, accept_pos.y + accept_height * 0.5f);
                dl->AddRectFilled(ImVec2(check_pos.x - 8.0f, check_pos.y - 8.0f), ImVec2(check_pos.x + 8.0f, check_pos.y + 8.0f),
                    current ? CyberTheme::U32(CyberTheme::Colors.Gold) : CyberTheme::U32(CyberTheme::Colors.Panel), 4.0f);
                if (current) DrawCheck(dl, check_pos, CyberTheme::U32(CyberTheme::Colors.Background));
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(accept_pos.x + 42.f, accept_pos.y + (accept_height - labelHeight) * 0.5f),
                    CyberTheme::U32(CyberTheme::Colors.Text), label, nullptr, wrapWidth);
                break;
            }
            case Step::HardwareDetection: {
                DrawHardwareRow("DMA / FPGA", Tr("Windows device detection", "Deteção de dispositivo no Windows"),
                    g_hardware.dmaAvailable, g_hardware.dmaFinished, g_hardware.dmaTimedOut, false);
                DrawHardwareRow("MAKCU", Tr("External input controller", "Controlador externo"),
                    g_hardware.makcuAvailable, g_hardware.makcuFinished, g_hardware.makcuTimedOut, true);
                const int count = (g_hardware.dmaFinished && g_hardware.dmaAvailable ? 1 : 0) +
                    (g_hardware.makcuFinished && g_hardware.makcuAvailable ? 1 : 0);
                ImGui::TextColored(count > 0 ? CyberTheme::Colors.Success : CyberTheme::Colors.TextDisabled,
                    count == 1 ? Tr("%d device detected", "%d dispositivo detetado") :
                                 Tr("%d devices detected", "%d dispositivos detetados"), count);
                if (g_hardware.dmaFinished && g_last_result > 0.0)
                    ImGui::TextDisabled(Tr("Last DMA check: %.0f s ago", "Última verificação de DMA: há %.0f s"),
                                        (std::max)(0.0, ImGui::GetTime() - g_last_result));
                ImGui::PushTextWrapPos();
                if (!g_hardware.dmaFinished)
                    ImGui::TextWrapped("%s", Tr("Detection runs in the background.", "A deteção decorre em segundo plano."));
                else if (!g_hardware.dmaAvailable)
                    ImGui::TextWrapped("%s", Tr("Connect the DMA device and refresh. You can finish setup and connect it before playing.",
                                               "Liga o dispositivo DMA e volta a verificar. Podes terminar a configuração e ligá-lo antes de jogar."));
                else
                    ImGui::TextWrapped("%s", Tr("Windows detected the DMA device. Its connection is checked when a game starts.",
                                               "O Windows detetou o dispositivo DMA. A ligação é verificada ao iniciar um jogo."));
                if (g_hardware.dmaTimedOut || g_hardware.makcuTimedOut)
                    ImGui::TextWrapped("%s", Tr("A device check timed out. Check the connection and refresh detection.",
                                               "Uma verificação excedeu o tempo limite. Confirma a ligação e repete a deteção."));
                ImGui::PopTextWrapPos();
                if (CyberWidgets::Button(Tr("Refresh Detection", "Repetir deteção"), CyberWidgets::ButtonStyle::Secondary,
                        ImVec2(166.f, 34.f), g_hardware.dmaFinished && !g_refresh_requested && !g_finish_pending))
                    g_refresh_requested = true;
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
        if (completed) Complete();
        else Reset();
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

}
