#include "../../window.hpp"
#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "../../global_search.h"
#include "../../hotkeys.h"
#include "../../InputDevicesCard.h"
#include "config/app_settings.h"
#include "gameplay/dma_telemetry_log.h"
#include "../../../globals.h"
#include "../../../launcher/launcher_assets.h"
#include "../../brand_assets.h"
#include "../../../auth/local_auth_service.h"
#include "../../../licensing/license_service.h"
#include "platform/monitor_utils.h"
#include "Memory/Memory.h"
#include "Cs2/cs2_game.h"
#include "Fivem/game/game_setup.h"
#include "config/config_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <Windows.h>
#include <commdlg.h>
#include <string>
#include <vector>

namespace {
const char* ActiveGameTelemetryName() {
    switch (g_activeGame) {
    case ActiveGame::CS2: return "CS2";
    case ActiveGame::FiveM: return "FiveM";
    case ActiveGame::Warzone: return "Warzone";
    case ActiveGame::Valorant: return "Valorant";
    case ActiveGame::Fortnite: return "Fortnite";
    default: return "OmniGhost";
    }
}

void DrawTelemetryWindow() {
    // Telemetry is log-only: no ImGui popup window.
    // Toggle matches ESP-style switches; PERF lines go to logs.txt via SessionLog.
    CyberWidgets::ToggleSwitch(
        app_settings::T("Telemetria (logs.txt)", "Telemetry (logs.txt)"),
        &app_settings::config.telemetry_log_enabled);
    OmniGhost::Gameplay::DmaTelemetry::SetEnabled(app_settings::config.telemetry_log_enabled);
    if (app_settings::config.telemetry_log_enabled) {
        if (g_activeGame == ActiveGame::CS2)
            OmniGhost::Gameplay::DmaTelemetry::Tick("CS2");
        else if (g_activeGame == ActiveGame::FiveM)
            OmniGhost::Gameplay::DmaTelemetry::Tick("FIVEM");
        CyberWidgets::TextLine(
            app_settings::T(
                "A gravar PERF/WARN no logs.txt (sem janela). Análise offline.",
                "Writing PERF/WARN to logs.txt (no window). Offline analysis."),
            CyberWidgets::TextTone::Secondary);
    } else {
        CyberWidgets::TextLine(
            app_settings::T(
                "Ativa para registar leituras DMA no logs.txt.",
                "Enable to record DMA readings into logs.txt."),
            CyberWidgets::TextTone::Secondary);
    }
}


const char* MenuVkName(int vk) {
    switch (vk) {
    case 0: return "Nenhuma";
    case 0x01: return "LMB";
    case 0x02: return "RMB";
    case 0x04: return "MMB";
    case 0x05: return "X1";
    case 0x06: return "X2";
    case 0x08: return "Retrocesso";
    case 0x09: return "Tab";
    case 0x0D: return "Enter";
    case 0x10: return "Shift";
    case 0x11: return "Ctrl";
    case 0x12: return "Alt";
    case 0x14: return "CapsLock";
    case 0x1B: return "Esc";
    case 0x20: return "Space";
    case 0x21: return "PageUp";
    case 0x22: return "PageDown";
    case 0x23: return "End";
    case 0x24: return "Home";
    case 0x25: return "Esquerda";
    case 0x26: return "Up";
    case 0x27: return "Direita";
    case 0x28: return "Down";
    case 0x2C: return "PrintScr";
    case 0x2D: return "Insert";
    case 0x2E: return "Delete";
    case 0x70: return "F1"; case 0x71: return "F2"; case 0x72: return "F3";
    case 0x73: return "F4"; case 0x74: return "F5"; case 0x75: return "F6";
    case 0x76: return "F7"; case 0x77: return "F8"; case 0x78: return "F9";
    case 0x79: return "F10"; case 0x7A: return "F11"; case 0x7B: return "F12";
    default: break;
    }
    static char buf[16];
    if (vk >= 0x30 && vk <= 0x39) { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= 0x41 && vk <= 0x5A) { buf[0] = (char)vk; buf[1] = 0; return buf; }
    std::snprintf(buf, sizeof(buf), "VK 0x%02X", vk);
    return buf;
}

std::string CurrentProfileName() {
    const auto license = OmniGhost::Licensing::GetSnapshot();
    std::string name = license.remoteUsername;
    if (name.empty()) name = OmniGhost::Auth::LocalAuthService::Instance().CurrentEmail();
    if (const std::size_t at = name.find('@'); at != std::string::npos) name.resize(at);
    return name.empty() ? "OmniGhost User" : name;
}

bool ChooseProfileImage(HWND owner) {
    wchar_t selected[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = selected;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"Image files\0*.png;*.jpg;*.jpeg;*.bmp\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0Bitmap\0*.bmp\0\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    // The overlay itself is topmost.  Making the native file picker its owned
    // dialog keeps the picker above the menu/background instead of behind it.
    dialog.hwndOwner = owner;
    if (!GetOpenFileNameW(&dialog)) return false;
    return LauncherAssets::ImportProfileAvatar(selected);
}

void DrawProfileAvatar(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    const LauncherAssets::Texture texture = LauncherAssets::ProfileAvatar();
    ImTextureID image = texture.id ? texture.id : BrandAssets::GetLogoTexture();
    ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
    if (texture.id && texture.width > 0 && texture.height > 0) {
        if (texture.width > texture.height) {
            const float inset = (1.f - static_cast<float>(texture.height) / texture.width) * .5f;
            uv0.x = inset; uv1.x = 1.f - inset;
        } else if (texture.height > texture.width) {
            const float inset = (1.f - static_cast<float>(texture.width) / texture.height) * .5f;
            uv0.y = inset; uv1.y = 1.f - inset;
        }
    }
    if (image)
        draw->AddImageRounded(image, min, max, uv0, uv1, IM_COL32_WHITE, (max.x - min.x) * .5f);
}
} // namespace

static bool g_search_registered = false;

void DrawConfigs(Overlay* self)
{
    if (!g_search_registered) {
        GlobalSearch::RegisterItem("Black Background", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Black Level Opacity", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Performance Mode", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Auto Performance", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Auto Save", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Digital Rain", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Animation Intensity", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Reduce Motion", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("UI Scale", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Monitor Selection", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Show FPS", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Hotkey Overlay", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("VSync", "Configs", "Visual", []{});
        GlobalSearch::RegisterItem("Primary Color", "Configs", "Colors", []{});
        GlobalSearch::RegisterItem("Secondary Color", "Configs", "Colors", []{});
        GlobalSearch::RegisterItem("FPS Color", "Configs", "Colors", []{});
        GlobalSearch::RegisterItem("Theme", "Configs", "Colors", []{});
        GlobalSearch::RegisterItem("Language", "Configs", "Settings", []{});
        GlobalSearch::RegisterItem("Menu Bind", "Configs", "Settings", []{});
        GlobalSearch::RegisterItem("DMA Reinit", "Configs", "Devices", []{});
        GlobalSearch::RegisterItem("Return to Launcher", "Configs", "Settings", []{});
        g_search_registered = true;
    }
    
    static int settingsTab = 0;
    const char* tabLabels[] = {
        app_settings::T("GERAL##settings_general", "GENERAL##settings_general"),
        app_settings::T("APARÊNCIA##settings_appearance", "APPEARANCE##settings_appearance"),
        app_settings::T("TECLAS##settings_keys", "KEYS##settings_keys"),
        app_settings::T("AVANÇADO##settings_advanced", "ADVANCED##settings_advanced") };
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0.f, CyberTheme::Spacing::Sm);
        if (CyberWidgets::Button(tabLabels[i], settingsTab == i
                ? CyberWidgets::ButtonStyle::Primary : CyberWidgets::ButtonStyle::Ghost,
                ImVec2(132.f, 34.f)))
            settingsTab = i;
    }
    CyberWidgets::CardGap(CyberTheme::Spacing::Sm);

    if (settingsTab == 0) {
        CyberWidgets::BeginCard(Loc::Tr("profile.label"));
        const ImVec2 avatarMin = ImGui::GetCursorScreenPos();
        const ImVec2 avatarSize(CyberTheme::Px(76.f), CyberTheme::Px(76.f));
        ImGui::InvisibleButton("##change_profile_avatar", avatarSize);
        const bool avatarHovered = ImGui::IsItemHovered();
        if (avatarHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 avatarMax(avatarMin.x + avatarSize.x, avatarMin.y + avatarSize.y);
        DrawProfileAvatar(ImGui::GetWindowDrawList(), avatarMin, avatarMax);
        ImGui::GetWindowDrawList()->AddCircle(
            ImVec2(avatarMin.x + avatarSize.x * .5f, avatarMin.y + avatarSize.y * .5f), avatarSize.x * .5f,
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, avatarHovered ? .92f : .55f), 40, avatarHovered ? 2.f : 1.f);
        if (avatarHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const bool imported = ChooseProfileImage(self ? self->overlay : nullptr);
            CyberWidgets::Notify(imported
                ? app_settings::T("Foto de perfil atualizada", "Profile picture updated")
                : app_settings::T("Nenhuma imagem válida foi selecionada", "No valid image was selected"),
                imported ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Info);
        }
        ImGui::SameLine(0.f, CyberTheme::Spacing::Md);
        ImGui::BeginGroup();
        CyberWidgets::TextLine(CurrentProfileName().c_str(), CyberWidgets::TextTone::Primary);
        CyberWidgets::TextLine(app_settings::T("Clica na fotografia para escolher uma imagem do computador.",
                                              "Click the picture to choose an image from your computer."),
                                CyberWidgets::TextTone::Secondary);
        ImGui::EndGroup();
        CyberWidgets::EndCard();
        CyberWidgets::CardGap();

        CyberWidgets::BeginCardRow();
        const float half = CyberWidgets::CardRowHalfWidth();
        CyberWidgets::BeginCard(app_settings::T("GERAL", "GENERAL"), half);
        std::vector<const char*> langs;
        langs.reserve(Loc::LanguageCount());
        for (int i = 0; i < Loc::LanguageCount(); ++i)
            langs.push_back(Loc::LanguageName(i));
        int lang = static_cast<int>(app_settings::config.language);
        if (CyberWidgets::Combo(Loc::Tr("cfg.language"), &lang, langs.data(), static_cast<int>(langs.size()))) {
            app_settings::config.language = static_cast<app_settings::Language>(lang);
            std::string saveError;
            (void)app_settings::SaveGlobal(&saveError);
        }
        CyberWidgets::ToggleSwitch(Loc::Tr("cfg.auto_save"), &app_settings::config.auto_save);
        const char* performanceModes[] = { app_settings::T("Automático", "Automatic"), app_settings::T("Qualidade", "Quality"), app_settings::T("Desempenho", "Performance") };
        int performanceMode = app_settings::config.auto_performance ? 0
            : (app_settings::config.performance_mode ? 2 : 1);
        if (CyberWidgets::Combo(app_settings::T("Desempenho", "Performance"), &performanceMode, performanceModes, 3)) {
            app_settings::config.auto_performance = performanceMode == 0;
            app_settings::config.performance_mode = performanceMode == 2;
        }
        CyberWidgets::EndCard();

        CyberWidgets::NextCardColumn();
        CyberWidgets::BeginCard(app_settings::T("ECRÃ", "DISPLAY"), half);
    const char* uiScales[] = { "75%", "80%", "90%", "100%", "110%", "125%", "150%", "175%", "200%", "250%" };
    const float uiScaleValues[] = { 0.75f, 0.80f, 0.90f, 1.00f, 1.10f, 1.25f, 1.50f, 1.75f, 2.00f, 2.50f };
    int uiScaleIndex = 3; // 100% default
    float bestDistance = 100.0f;
    for (int i = 0; i < 10; ++i) {
        const float distance = std::fabs(app_settings::config.ui_scale - uiScaleValues[i]);
        if (distance < bestDistance) { bestDistance = distance; uiScaleIndex = i; }
    }
    if (CyberWidgets::Combo("UI Scale", &uiScaleIndex, uiScales, 10))
        app_settings::config.ui_scale = uiScaleValues[std::clamp(uiScaleIndex, 0, 9)];
    CyberWidgets::HelpMarker("A escala é combinada com o DPI do monitor. Em 125%, 150% ou 200% do Windows, o OmniGhost mantém texto e controlos nítidos.");

    const auto monitors = OmniGhost::Platform::EnumerateMonitors();
    std::vector<std::string> monitorLabels;
    std::vector<const char*> monitorItems;
    monitorLabels.reserve(monitors.size() + 1);
    monitorLabels.emplace_back("Automático / monitor atual");
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const auto& monitor = monitors[i];
        const int width = monitor.rect.right - monitor.rect.left;
        const int height = monitor.rect.bottom - monitor.rect.top;
        char label[96]{};
        std::snprintf(label, sizeof(label), "Monitor %zu · %dx%d%s", i + 1, width, height,
                      monitor.primary ? " · Principal" : "");
        monitorLabels.emplace_back(label);
    }
    monitorItems.reserve(monitorLabels.size());
    for (const auto& label : monitorLabels) monitorItems.push_back(label.c_str());
    int monitorSelection = app_settings::config.monitor_index + 1;
    monitorSelection = std::clamp(monitorSelection, 0, static_cast<int>(monitorItems.size()) - 1);
    if (CyberWidgets::Combo("Monitor", &monitorSelection, monitorItems.data(), static_cast<int>(monitorItems.size())))
        app_settings::config.monitor_index = monitorSelection - 1;
        CyberWidgets::EndCard();
        CyberWidgets::EndCardRow();

        CyberWidgets::CardGap();
        CyberWidgets::BeginCard(app_settings::T("CONFIGURAÇÃO E PERFIL", "CONFIGURATION AND PROFILE"));
        CyberWidgets::KeyValueRow(app_settings::T("Perfil atual", "Current profile"), config_manager::CurrentGameProfileName());
        if (CyberWidgets::Button("DEFAULT", CyberWidgets::ButtonStyle::Secondary, ImVec2(110, 34)))
            config_manager::ApplyGameProfile(config_manager::GameProfile::Default);
        ImGui::SameLine();
        if (CyberWidgets::Button("MINIMAL", CyberWidgets::ButtonStyle::Secondary, ImVec2(110, 34)))
            config_manager::ApplyGameProfile(config_manager::GameProfile::Minimal);
        ImGui::SameLine();
        if (CyberWidgets::Button("VISUAL", CyberWidgets::ButtonStyle::Secondary, ImVec2(110, 34)))
            config_manager::ApplyGameProfile(config_manager::GameProfile::Visual);
        ImGui::SameLine();
        if (CyberWidgets::Button(app_settings::T("GUARDAR ATUAL", "SAVE CURRENT"), CyberWidgets::ButtonStyle::Primary, ImVec2(150, 34)))
            config_manager::SaveCustomGameProfile();
        CyberWidgets::EndCard();
    } else if (settingsTab == 1) {
        CyberWidgets::BeginCard(app_settings::T("APARÊNCIA", "APPEARANCE"));
        CyberWidgets::SliderFloat(app_settings::T("Escurecimento do fundo", "Background darkness"), &app_settings::config.black_level, 0.f, 100.f, "%.0f%%");
        app_settings::config.black_background = app_settings::config.black_level > .5f;
        const char* effectLevels[] = { app_settings::T("Desligado", "Off"), app_settings::T("Subtil", "Subtle"), app_settings::T("Completo", "Full") };
        int rainLevel = static_cast<int>(app_settings::config.digital_rain_level);
        if (CyberWidgets::Combo(Loc::Tr("cfg.matrix"), &rainLevel, effectLevels, 3)) {
            rainLevel = std::clamp(rainLevel, 0, 2);
            app_settings::config.digital_rain_level = static_cast<app_settings::EffectLevel>(rainLevel);
            app_settings::config.matrix_rain = rainLevel != 0;
        }
        int animationLevel = static_cast<int>(app_settings::config.animation_intensity);
        if (CyberWidgets::Combo(app_settings::T("Intensidade das animações", "Animation intensity"), &animationLevel, effectLevels, 3))
            app_settings::config.animation_intensity = static_cast<app_settings::EffectLevel>(std::clamp(animationLevel, 0, 2));
        CyberWidgets::ToggleSwitch(app_settings::T("Movimento reduzido", "Reduce motion"), &app_settings::config.reduce_motion);
        CyberWidgets::ToggleSwitch(Loc::Tr("cfg.show_fps"), &app_settings::config.show_fps);
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("IDENTIDADE");
        CyberWidgets::TextLine("OMNIGHOST · Preto, dourado e precisão.",
                               CyberWidgets::TextTone::Accent);
        CyberWidgets::EndCard();
    } else if (settingsTab == 2) {
        CyberWidgets::BeginCard(app_settings::T("TECLAS", "KEYS"));
        CyberWidgets::KeyValueRow(Loc::Tr("cfg.menu_bind"), MenuVkName(app_settings::config.menu_bind));
        static bool waiting_bind = false;
        if (waiting_bind) {
            CyberWidgets::TextLine(app_settings::T("Prime qualquer tecla…", "Press any key…"), CyberWidgets::TextTone::Warning);
            for (int vk = 1; vk < 256; ++vk) {
                if (vk != app_settings::config.menu_bind && (GetAsyncKeyState(vk) & 1)) {
                    app_settings::config.menu_bind = vk;
                    waiting_bind = false;
                    break;
                }
            }
        } else if (CyberWidgets::CyberButton(Loc::Tr("cfg.rebind"), ImVec2(160, 32))) {
            waiting_bind = true;
        }
        CyberWidgets::EndCard();
        CyberWidgets::CardGap();
        Hotkeys::DrawHotkeyConfig();
    } else {
        CyberWidgets::BeginCard(app_settings::T("AVANÇADO", "ADVANCED"));
        CyberWidgets::ToggleSwitch(Loc::Tr("cfg.vsync"), &app_settings::config.vsync);
        CyberWidgets::ToggleSwitch(Loc::Tr("cfg.hotkey_overlay"), &app_settings::config.show_hotkey_overlay);
        CyberWidgets::ToggleSwitch(app_settings::T("Detalhes técnicos", "Technical details"), &app_settings::config.show_advanced);
        CyberWidgets::Separator();
        DrawTelemetryWindow();
        CyberWidgets::EndCard();
        CyberWidgets::CardGap();
        InputDevicesCard::Draw();
        CyberWidgets::CardGap();
        CyberWidgets::BeginCard(app_settings::T("MANUTENÇÃO", "MAINTENANCE"));
        CyberWidgets::Separator();
        CyberWidgets::SectionTitle("DMA");
        CyberWidgets::TextLine("Reinicia a ligação sem fechar o Control Center.",
                               CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::GoldButton(Loc::Tr("device.reinit_dma"), ImVec2(160, 36))) {
        bool ok = false;
        std::string msg;
        if (g_activeGame == ActiveGame::CS2) {
            ok = CS2::ReinitDma();
            msg = ok ? "CS2 Reinit DMA OK" : (CS2::status.empty() ? "CS2 Reinit DMA falhou" : CS2::status);
        } else if (g_activeGame == ActiveGame::FiveM) {
            ok = FiveM::ReinitDma();
            msg = ok ? "FiveM Reinit DMA OK" : "FiveM Reinit DMA falhou";
        } else if (g_activeGame == ActiveGame::Warzone) {
            mem.InvalidateProcess();
            msg = "Warzone: process bind limpo; volta ao launcher para reattach";
            ok = true;
        } else {
            // Generic: invalidate process bind only
            mem.InvalidateProcess();
            msg = "Associação ao processo limpa — troca para um jogo com motor DMA";
            ok = true;
        }
        CyberWidgets::Notify(msg.c_str(),
            ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    CyberWidgets::CardGap(12.0f);
    if (CyberWidgets::DangerButton(app_settings::T("Voltar ao launcher", "Return to launcher"), ImVec2(190, 36))) {
        self->RequestReturnToLauncher();
    }
        CyberWidgets::EndCard();
    }
}
