#include "imgui.h"
#include "../../../Rust/rust_config.h"
#include "../../../Rust/rust_game.h"
#include "../../widgets.h"

void DrawRustVisuals() {
    ImGui::TextUnformatted("Visual / ESP — Rust");
    ImGui::Separator();
    CyberWidgets::ToggleSwitch("Ativar ESP", &Rust::config.esp_enabled);
    if (!Rust::config.esp_enabled)
        ImGui::TextDisabled("ESP desligado — podes configurar na mesma.");
    CyberWidgets::ToggleSwitch("Caixa", &Rust::config.esp_box);
    CyberWidgets::ToggleSwitch("Nome", &Rust::config.esp_name);
    CyberWidgets::ToggleSwitch("Vida", &Rust::config.esp_health);
    CyberWidgets::ToggleSwitch("Distancia", &Rust::config.esp_distance);
    CyberWidgets::ToggleSwitch("Esqueleto", &Rust::config.esp_skeleton);
    CyberWidgets::ToggleSwitch("Ignorar sleepers", &Rust::config.esp_ignore_sleepers);
    CyberWidgets::ToggleSwitch("Ignorar NPC", &Rust::config.esp_ignore_npc);
    CyberWidgets::SliderFloat("Distancia max", &Rust::config.esp_max_distance, 50.f, 1000.f, "%.0f m");
    ImGui::ColorEdit4("Cor caixa", Rust::config.col_box, ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit4("Cor nome", Rust::config.col_name, ImGuiColorEditFlags_NoInputs);
}

void DrawRustSystem() {
    ImGui::TextUnformatted("Sistema — Rust");
    ImGui::Separator();
    ImGui::Text("Estado: %s", Rust::StatusText());
    ImGui::Text("PID: %lu", (unsigned long)Rust::runtime.pid);
    ImGui::Text("GameAssembly: 0x%llX", (unsigned long long)Rust::runtime.game_assembly);
    ImGui::Text("Offsets: %s (%s)", Rust::offsets.source, Rust::offsets.version);
    ImGui::Text("BN TypeInfo: 0x%llX", (unsigned long long)Rust::offsets.BaseNetworkable_TypeInfo);
    ImGui::Text("Camera TypeInfo: 0x%llX", (unsigned long long)Rust::offsets.MainCamera_TypeInfo);
    ImGui::Text("Entidades: %d | Players: %d", Rust::runtime.entity_count, Rust::runtime.player_count);
    ImGui::Text("Matrix: %s", Rust::runtime.matrix_ok ? "OK" : "-");
    ImGui::Text("Frames: %llu", (unsigned long long)Rust::runtime.frames);
    if (ImGui::Button("Recarregar offsets"))
        Rust::LoadOffsetsFromJson(nullptr);
    ImGui::SameLine();
    if (ImGui::Button("Reattach")) {
        Rust::Shutdown();
        Rust::Attach();
    }
}
