#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "../../InputDevicesCard.h"
#include "../../Rust/rust_game.h"
#include "../../Rust/rust_config.h"
#include "../../Rust/rust_aim.h"
#include "aimbot/aim_type.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace {
const char* VkName(int vk) {
    if (vk <= 0) return "NENHUM";
    switch (vk) {
    case 1: return "Mouse Esquerdo"; case 2: return "Mouse Direito";
    case 4: return "Mouse Meio"; case 5: return "Mouse 4"; case 6: return "Mouse 5";
    case 0x10: return "Shift"; case 0x11: return "Ctrl"; case 0x12: return "Alt";
    case 0x20: return "Space"; default: break;
    }
    static char buf[32];
    if (vk >= 'A' && vk <= 'Z') { std::snprintf(buf, sizeof(buf), "%c", vk); return buf; }
    std::snprintf(buf, sizeof(buf), "VK 0x%02X", vk);
    return buf;
}

bool HotkeyCaptureButton(const char* id, int* vk) {
    static int* capturing = nullptr;
    static float blink = 0.f;
    static double ignore_until = 0.0;
    static uint8_t mask_at_start = 0;
    ImGui::PushID(id);
    const bool isCap = (capturing == vk);
    char label[64];
    if (isCap) {
        blink += ImGui::GetIO().DeltaTime;
        std::snprintf(label, sizeof(label), (int(blink * 5.f) % 2) ? "( ... )" : "(  .  )");
    } else std::snprintf(label, sizeof(label), "%s", VkName(*vk));
    const bool clicked = CyberWidgets::Button(
        label, isCap ? CyberWidgets::ButtonStyle::Primary : CyberWidgets::ButtonStyle::Secondary,
        ImVec2(160.f, 30.f));
    if (clicked) {
        if (isCap) capturing = nullptr;
        else {
            capturing = vk; blink = 0.f; ignore_until = ImGui::GetTime() + 0.28;
            mask_at_start = aim_type::ButtonMask();
        }
    }
    if (isCap && ImGui::GetTime() >= ignore_until) {
        const uint8_t rose = (uint8_t)(aim_type::ButtonMask() & ~mask_at_start);
        if (rose & 0x01) { *vk = 1; capturing = nullptr; }
        else if (rose & 0x02) { *vk = 2; capturing = nullptr; }
        else if (rose & 0x04) { *vk = 4; capturing = nullptr; }
        else if (rose & 0x08) { *vk = 5; capturing = nullptr; }
        else if (rose & 0x10) { *vk = 6; capturing = nullptr; }
        for (int s = 1; s < 256 && capturing; ++s) {
            if (s == 1 || s == 2) continue;
            if (GetAsyncKeyState(s) & 0x8000) { *vk = s; capturing = nullptr; break; }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { *vk = 0; capturing = nullptr; }
    }
    ImGui::PopID();
    return false;
}

void CategoryChip(const char* label, bool* on) {
    ImGui::PushID(label);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %s", *on ? "✓" : " ", label);
    if (CyberWidgets::Button(buf, *on ? CyberWidgets::ButtonStyle::Primary : CyberWidgets::ButtonStyle::Ghost, ImVec2(0, 28.f)))
        *on = !*on;
    ImGui::PopID();
}
} // namespace

void DrawRustAim() {
    CyberWidgets::BeginCardRow(2);

    // ── Left: global aim ─────────────────────────────────────────
    CyberWidgets::BeginCard("Configurações globais");
    CyberWidgets::ToggleSwitch("Ativado", &Rust::config.aim_enabled);
    CyberWidgets::ToggleSwitch("Transicoes suaves entre bones", &Rust::config.aim_bone_transitions);
    CyberWidgets::ToggleSwitch("Sincronizar com FPS do jogo", &Rust::config.aim_sync_fps);

    ImGui::TextUnformatted("Teclas de segurar");
    ImGui::SameLine(180.f); HotkeyCaptureButton("r_aim1", &Rust::config.aim_bind);
    ImGui::SameLine(); HotkeyCaptureButton("r_aim2", &Rust::config.aim_bind2);
    ImGui::SameLine(); HotkeyCaptureButton("r_aim3", &Rust::config.aim_bind3);
    if (Rust::config.aim_bind <= 0) Rust::config.aim_bind = 0x02;

    CyberWidgets::SliderFloat("Intervalo de mira (ms)", &Rust::config.aim_interval_ms, 0.f, 40.f, "%.0f ms");
    CyberWidgets::ToggleSwitch("Desenhar circulo de FOV", &Rust::config.aim_draw_fov);
    ImGui::ColorEdit4("Cor FOV", Rust::config.col_fov, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::ToggleSwitch("Desenhar ponto de predicao", &Rust::config.aim_draw_prediction);
    CyberWidgets::ToggleSwitch("Linha ate ao alvo", &Rust::config.aim_draw_line);
    CyberWidgets::SliderFloat("Tamanho ponto predicao", &Rust::config.prediction_point_size, 0.5f, 6.f, "%.2f px");
    CyberWidgets::EndCard();

    // ── Right: filters ───────────────────────────────────────────
    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("FILTROS");
    CyberWidgets::ToggleSwitch("Ignorar time", &Rust::config.aim_ignore_team);
    CyberWidgets::ToggleSwitch("Ignorar knockados", &Rust::config.aim_ignore_knocked);
    CyberWidgets::ToggleSwitch("Ignorar NPCs", &Rust::config.aim_ignore_npc);
    CyberWidgets::ToggleSwitch("Ignorar feridos", &Rust::config.aim_ignore_wounded);
    CyberWidgets::ToggleSwitch("Ignorar dormindo", &Rust::config.aim_ignore_sleepers);
    CyberWidgets::ToggleSwitch("Ignorar helicopteros", &Rust::config.aim_ignore_helicopters);
    CyberWidgets::ToggleSwitch("Ignorar drones", &Rust::config.aim_ignore_drones);
    CyberWidgets::ToggleSwitch("Team check (ESP)", &Rust::config.team_check);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    // Weapon category chips
    CyberWidgets::BeginCard("Categorias de arma");
    CategoryChip("Global", &Rust::config.aim_cat_global);
    ImGui::SameLine(); CategoryChip("Rifles", &Rust::config.aim_cat_rifles);
    ImGui::SameLine(); CategoryChip("SMGs", &Rust::config.aim_cat_smgs);
    ImGui::SameLine(); CategoryChip("Pistolas", &Rust::config.aim_cat_pistols);
    ImGui::SameLine(); CategoryChip("LMGs", &Rust::config.aim_cat_lmgs);
    ImGui::SameLine(); CategoryChip("Shotguns", &Rust::config.aim_cat_shotguns);
    ImGui::SameLine(); CategoryChip("Arcos", &Rust::config.aim_cat_bows);
    ImGui::TextDisabled("Filtro por classe aplica-se quando o nome da arma estiver disponivel.");
    CyberWidgets::EndCard();

    CyberWidgets::BeginCardRow(2);
    CyberWidgets::BeginCard("Configuração global");
    CyberWidgets::ToggleSwitch("Ativar predicao", &Rust::config.aim_prediction);
    CyberWidgets::SliderFloat("FOV", &Rust::config.aim_fov, 10.f, 400.f, "%.0f px");
    CyberWidgets::SliderFloat(Loc::Tr("aim.smooth"), &Rust::config.aim_smooth, 0.f, 100.f, "%.0f");
    ImGui::TextDisabled("0 = cola na mira  |  100 = sem puxar  |  20-40 = track humano");
    CyberWidgets::SliderFloat("Distancia max", &Rust::config.aim_max_dist, 20.f, 500.f, "%.0f m");
    CyberWidgets::SliderFloat("Deadzone", &Rust::config.aim_deadzone, 0.f, 12.f, "%.1f");
    CyberWidgets::SliderFloat("Sticky (ms)", &Rust::config.sticky_ms, 0.f, 400.f, "%.0f");
    CyberWidgets::ToggleSwitch("Humanize", &Rust::config.aim_humanize);
    {
        const char* bones[] = { "Cabeca", "Peito", "Pelve", "Aleatorio" };
        CyberWidgets::Combo("Hitbox", &Rust::config.aim_bone, bones, 4);
    }
    CyberWidgets::SliderFloat("Prediction strength", &Rust::config.prediction_strength, 0.f, 1.5f, "%.2f");
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("Tecla de override");
    ImGui::TextWrapped(
        "Enquanto a tecla estiver premida, o aimbot usa smooth/FOV alternativos "
        "(ex.: configuração base + ajuste temporário com override).");
    ImGui::TextUnformatted("Tecla override");
    ImGui::SameLine(160.f);
    HotkeyCaptureButton("r_override", &Rust::config.aim_override_key);
    CyberWidgets::SliderFloat("Smooth override", &Rust::config.aim_override_smooth, 0.f, 100.f, "%.0f");
    CyberWidgets::SliderFloat("FOV override", &Rust::config.aim_override_fov, 10.f, 500.f, "%.0f px");
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("TRIGGER");
    CyberWidgets::ToggleSwitch("Disparo automático", &Rust::config.trigger_enabled);
    ImGui::TextUnformatted("Tecla de ativação");
    ImGui::SameLine(160.f);
    HotkeyCaptureButton("r_trig", &Rust::config.trigger_bind);
    CyberWidgets::InputInt("Atraso (ms)", &Rust::config.trigger_delay_ms);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    InputDevicesCard::Draw();

    CyberWidgets::BeginCard("ESTADO");
    ImGui::Text("Entrada: %s", aim_type::StatusText());
    ImGui::Text("Mira: %s", Rust_Aim::DebugStatus());
    ImGui::Text("Jogadores no runtime: %d · em jogo: %s",
        Rust::runtime.player_count, Rust::runtime.in_game ? "sim" : "não");
    CyberWidgets::EndCard();
}
