#include "imgui.h"
#include "src/games/Rust/rust_config.h"
#include "../../widgets.h"

void DrawRustAim() {
    ImGui::TextUnformatted("Mira — Rust");
    ImGui::Separator();
    CyberWidgets::ToggleSwitch("Ativar Aim", &Rust::config.aim_enabled);
    CyberWidgets::SliderFloat("FOV", &Rust::config.aim_fov, 10.f, 300.f, "%.0f");
    CyberWidgets::SliderFloat("Smooth", &Rust::config.aim_smooth, 1.f, 20.f, "%.1f");
    CyberWidgets::ToggleSwitch("So visivel", &Rust::config.aim_visible_only);
    ImGui::TextDisabled("Aimbot em integracao (input Makcu/KMBox partilhado).");
}
