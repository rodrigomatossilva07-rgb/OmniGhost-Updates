#include "../../window.hpp"
#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "global_search.h"
#include "hotkeys.h"
#include "../config/app_settings.h"
#include "../../../globals.h"
#include "../platform/monitor_utils.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../../Rust/rust_game.h"
#include "../../Cs2/cs2_game.h"
#include "../../Fivem/game/game_setup.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <Windows.h>
#include <string>
#include <vector>

namespace {
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
    
    const float half = (ImGui::GetContentRegionAvail().x - 12.f) * 0.5f;

    CyberWidgets::BeginCardRow();

    CyberWidgets::BeginCard(Loc::Tr("cfg.visual"), half);
    {
        const bool prev_bg = app_settings::config.black_background;
        CyberWidgets::ToggleSwitch(Loc::TrID("cfg.black_bg"), &app_settings::config.black_background);
        if (app_settings::config.black_background != prev_bg) {
            // Toggle is a shortcut for full on / full off
            app_settings::config.black_level = app_settings::config.black_background ? 100.f : 0.f;
        }
        CyberWidgets::SliderFloat("Opacidade do fundo preto", &app_settings::config.black_level, 0.f, 100.f, "%.0f");
        // Slider is the real control used by the renderer
        app_settings::config.black_background = (app_settings::config.black_level > 0.5f);
    }
    CyberWidgets::ToggleSwitch(Loc::TrID("cfg.perf"), &app_settings::config.performance_mode);
    CyberWidgets::HelpMarker("Reduz efeitos visuais e carga de animação. O modo automático ativa-se apenas quando o FPS fica baixo.");
    CyberWidgets::ToggleSwitch(Loc::TrID("cfg.auto_perf"), &app_settings::config.auto_performance);
    CyberWidgets::ToggleSwitch(Loc::TrID("cfg.auto_save"), &app_settings::config.auto_save);

    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("Movimento e ambiente");
    CyberWidgets::Badge("RECOMMENDED", CyberWidgets::TextTone::Accent);
    CyberWidgets::CardGap(CyberTheme::Spacing::Xs);
    const char* effectLevels[] = { "Off", "Subtle", "Full" };
    int rainLevel = static_cast<int>(app_settings::config.digital_rain_level);
    if (CyberWidgets::Combo("Digital Rain", &rainLevel, effectLevels, 3)) {
        rainLevel = std::clamp(rainLevel, 0, 2);
        app_settings::config.digital_rain_level = static_cast<app_settings::EffectLevel>(rainLevel);
        app_settings::config.matrix_rain = rainLevel != 0;
    }
    CyberWidgets::HelpMarker("Subtle usa cerca de 28% da densidade original e é o modo recomendado para um visual mais premium.");
    int animationLevel = static_cast<int>(app_settings::config.animation_intensity);
    if (CyberWidgets::Combo("Animation intensity", &animationLevel, effectLevels, 3))
        app_settings::config.animation_intensity = static_cast<app_settings::EffectLevel>(std::clamp(animationLevel, 0, 2));
    CyberWidgets::ToggleSwitch(Loc::Tr("launcher.reduce_motion"), &app_settings::config.reduce_motion);
    CyberWidgets::HelpMarker("Desativa deslocamentos, pulsos e transições não essenciais sem remover informação funcional.");

    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("Escala e ecrã");
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
    CyberWidgets::HelpMarker("Escolhe o monitor físico onde o overlay e o launcher devem ocupar o ecrã inteiro. Automático segue o monitor atual/principal.");

    CyberWidgets::ToggleSwitch(Loc::TrID("cfg.show_fps"), &app_settings::config.show_fps);
    CyberWidgets::ToggleSwitch(Loc::TrID("cfg.hotkey_overlay"), &app_settings::config.show_hotkey_overlay);
    CyberWidgets::ToggleSwitch(Loc::TrID("cfg.vsync"), &app_settings::config.vsync);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("cfg.colors"));
    CyberWidgets::ColorEditU32(Loc::TrID("cfg.color_primary"), &app_settings::config.color_primary);
    CyberWidgets::ColorEditU32(Loc::TrID("cfg.color_secondary"), &app_settings::config.color_secondary);
    CyberWidgets::ColorEditU32(Loc::TrID("cfg.color_fps"), &app_settings::config.color_fps);
    CyberWidgets::Separator();
    CyberWidgets::ThemeCombo(Loc::Tr("cfg.theme"));
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();

    CyberWidgets::BeginCard(Loc::Tr("cfg.settings"), half);
    {
        std::vector<const char*> langs;
        langs.reserve(Loc::LanguageCount());
        for (int i = 0; i < Loc::LanguageCount(); ++i)
            langs.push_back(Loc::LanguageName(i));
        int lang = static_cast<int>(app_settings::config.language);
        if (CyberWidgets::Combo(Loc::TrID("cfg.language"), &lang, langs.data(), static_cast<int>(langs.size())))
            app_settings::config.language = static_cast<app_settings::Language>(lang);
    }
    CyberWidgets::KeyValueRow(Loc::Tr("cfg.menu_bind"), MenuVkName(app_settings::config.menu_bind));
    static bool waiting_bind = false;
    if (waiting_bind) {
        CyberWidgets::TextLine(Loc::Tr("status.press_key"), CyberWidgets::TextTone::Warning);
        for (int vk = 1; vk < 256; ++vk) {
            if (vk == app_settings::config.menu_bind) continue;
            if (GetAsyncKeyState(vk) & 1) {
                app_settings::config.menu_bind = vk;
                waiting_bind = false;
                CyberWidgets::Notify(Loc::Tr("status.hotkey_set"), CyberWidgets::ToastType::Success);
                break;
            }
        }
    }
    else {
        if (CyberWidgets::CyberButton(Loc::TrID("cfg.rebind"), ImVec2(160, 32)))
            waiting_bind = true;
    }
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("DMA");
    CyberWidgets::TextLine(
        "Lobby→server (Rust) / mudar de cidade (FiveM) / re-attach sem fechar o menu",
        CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::GoldButton(Loc::Tr("device.reinit_dma"), ImVec2(160, 36))) {
        bool ok = false;
        std::string msg;
        if (g_activeGame == ActiveGame::Rust) {
            ok = Rust::ReinitDmaAsync();
            msg = ok ? "Rust Reinit DMA iniciado em background"
                     : (Rust::status.empty() ? "Rust Reinit DMA falhou" : Rust::status);
        } else if (g_activeGame == ActiveGame::CS2) {
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
    if (g_activeGame == ActiveGame::Rust && Rust::backend_busy.load()) {
        ImGui::SameLine();
        if (CyberWidgets::CyberButton("Cancelar reinicialização", ImVec2(210, 36))) {
            Rust::CancelBackendOperation();
            CyberWidgets::Notify("Cancelamento do motor do jogo solicitado", CyberWidgets::ToastType::Info);
        }
    }
    CyberWidgets::CardGap(12.0f);
    if (CyberWidgets::DangerButton("Voltar ao launcher", ImVec2(190, 36))) {
        self->RequestReturnToLauncher();
    }
    CyberWidgets::EndCard();

    // Hotkey configuration
    CyberWidgets::CardGap(12.0f);
    Hotkeys::DrawHotkeyConfig();

    CyberWidgets::EndCardRow();
}
