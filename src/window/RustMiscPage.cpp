#include "../globals.h"
#include "../config/app_settings.h"
#include "widgets.h"
#include "theme.h"
#include "../../Rust/rust_game.h"
#include "../../Rust/rust_config.h"
#include <algorithm>
#include <cstdio>

void DrawRustMisc() {
    CyberWidgets::BeginCard("Estado do Rust");
    const auto backend = Rust::GetBackendSnapshot();
    CyberWidgets::KeyValueRow("Motor", Rust::BackendStateName());
    CyberWidgets::StatusBadge("FPGA", Rust::DevicePhaseName()[0] != '\0');
    CyberWidgets::KeyValueRow("Processo", Rust::ProcessPhaseName());
    CyberWidgets::KeyValueRow("Execução", Rust::RuntimePhaseName());
    if (backend.procinfo_progress >= 0) {
        const float progress = (float)(std::clamp)(backend.procinfo_progress, 0, 100) / 100.0f;
        char overlay[64]{};
        std::snprintf(overlay, sizeof(overlay), "ProcInfo %d%%", backend.procinfo_progress);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), overlay);
        ImGui::Text("Estado ProcInfo=%d · DTB=%llu bytes · operação=%llums",
            backend.procinfo_state,
            (unsigned long long)backend.procinfo_dtb_size,
            (unsigned long long)backend.operation_elapsed_ms);
        ImGui::Text("Recuperação VMM=%s · manutenção=%s · leituras bloqueadas=%llu",
            backend.procinfo_recovery_recommended ? "recomendada" : "não",
            backend.vmm_maintenance ? "ativa" : "não",
            (unsigned long long)backend.blocked_data_calls);
    }
    ImGui::TextWrapped("%s", Rust::status.c_str());
    ImGui::TextWrapped("Execução: %s", Rust::runtime.status);
    ImGui::Text("Pronto: %s · Em jogo: %s · Ocupado: %s",
        Rust::ready ? "sim" : "não",
        Rust::runtime.in_game ? "sim" : "não",
        Rust::backend_busy.load() ? "sim" : "não");
    ImGui::Text("GA: 0x%llX local: 0x%llX team=%d",
        (unsigned long long)Rust::runtime.game_assembly,
        (unsigned long long)Rust::runtime.local_player,
        Rust::runtime.local_team);
    ImGui::Text("Jogadores: %d · matriz=%s · lista=%s · autoteste=%s",
        Rust::runtime.player_count,
        Rust::runtime.matrix_ok ? "OK" : "—",
        Rust::runtime.list_ok ? "OK" : "—",
        Rust::runtime.self_test_ok ? "OK" : "—");

    if (Rust::offsets.loaded)
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.f),
            "Offsets: OK (origem=%s)", Rust::offsets.source);
    else
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.f), "Offsets: em falta");

    if (Rust::runtime_phase.load() == (int)Rust::RuntimePhase::Unavailable) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.f),
            "Execução indisponível (ex.: sala → servidor). O FPGA pode continuar pronto; usa Reconectar DMA.");
    }

    if (CyberWidgets::GoldButton("Recarregar offsets JSON", ImVec2(210, 32))) {
        // Local JSON snapshot only.
        if (Rust::ReloadOffsets())
            CyberWidgets::Notify("Offsets JSON OK", CyberWidgets::ToastType::Success);
        else
            CyberWidgets::Notify("Falha rust_offsets.json", CyberWidgets::ToastType::Warning);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Autoteste", ImVec2(120, 32))) {
        Rust::SelfTest();
        CyberWidgets::Notify(
            Rust::runtime.self_test_ok ? "APROVADO" : "FALHOU",
            Rust::runtime.self_test_ok ? CyberWidgets::ToastType::Success
                                       : CyberWidgets::ToastType::Warning);
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCardRow(2);
    CyberWidgets::BeginCard("Controlo de recoil");
    CyberWidgets::ToggleSwitch("Controle de Recoil", &Rust::config.no_recoil);
    {
        const char* modes[] = { "Durante Aimbot", "Sempre" };
        CyberWidgets::Combo("Modo", &Rust::config.recoil_mode, modes, 2);
    }
    float recoilX = static_cast<float>(Rust::config.recoil_x);
    float recoilY = static_cast<float>(Rust::config.recoil_y);
    if (CyberWidgets::SliderFloat("Recoil X", &recoilX, 0.f, 100.f, "%.0f%%")) Rust::config.recoil_x = static_cast<int>(recoilX);
    if (CyberWidgets::SliderFloat("Recoil Y", &recoilY, 0.f, 100.f, "%.0f%%")) Rust::config.recoil_y = static_cast<int>(recoilY);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("Utilitários");
    CyberWidgets::ToggleSwitch("Homem-aranha", &Rust::config.spider_man);
    CyberWidgets::ToggleSwitch("ESP do inventário", &Rust::config.inventory_esp);
    CyberWidgets::ToggleSwitch("Noites claras", &Rust::config.bright_nights);
    CyberWidgets::ToggleSwitch("Visibilidade por raycast", &Rust::config.vischeck_raycast);
    CyberWidgets::ToggleSwitch("Wireframes de diagnóstico", &Rust::config.vischeck_debug_wire);
    CyberWidgets::ToggleSwitch("Modo de desempenho", &Rust::config.performance_mode);
    CyberWidgets::ToggleSwitch("Tecla de emergência", &Rust::config.panic_key_enabled);
    CyberWidgets::InputInt("VK de emergência", &Rust::config.panic_key);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    CyberWidgets::BeginCard("Exportar Fortify");
    ImGui::TextWrapped(
        "Exporta bases proximas para o Fortify (Steam). "
        "Requer entity list legivel (TC / sleeping bags).");
    CyberWidgets::SliderFloat("Distancia minima", &Rust::config.fortify_min_distance, 0.f, 500.f, "%.0f m");
    CyberWidgets::ToggleSwitch("Incluir sacos de dormir", &Rust::config.fortify_export_sleeping_bags);
    if (CyberWidgets::GoldButton("Exportar para Fortify", ImVec2(220, 32))) {
        CyberWidgets::Notify("Export Fortify: precisa lista de entidades", CyberWidgets::ToastType::Warning);
    }
    CyberWidgets::EndCard();

}
