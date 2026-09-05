#include "widgets.h"
#include "theme.h"
#include "../../Rust/rust_game.h"
#include "../../Rust/rust_config.h"
#include "aimbot/aim_type.h"
#include "InputDevicesCard.h"
#include <cstdio>

void DrawRustDebug() {
    CyberWidgets::BeginCard("Testes gerais");
    if (CyberWidgets::GoldButton("Reconectar dispositivo de aim", ImVec2(280, 32))) {
        const auto t = aim_type::config.active;
        aim_type::Disconnect(t);
        aim_type::Connect(t);
        CyberWidgets::Notify("Pedido de reconexão do dispositivo enviado", CyberWidgets::ToastType::Info);
    }
    if (CyberWidgets::CyberButton("Testar movimento do dispositivo", ImVec2(280, 32))) {
        aim_type::Move(20, 0);
        CyberWidgets::Notify("Movimento +20x enviado", CyberWidgets::ToastType::Info);
    }
    if (CyberWidgets::CyberButton("Testar clique do dispositivo", ImVec2(280, 32))) {
        aim_type::LeftClick();
        CyberWidgets::Notify("Clique de teste enviado", CyberWidgets::ToastType::Info);
    }
    CyberWidgets::EndCard();

    InputDevicesCard::Draw();

    CyberWidgets::BeginCard("Reinicialização DMA");
    ImGui::TextWrapped(
        "Reconexão faz rebind suave do processo (FixCr3) sem reabrir o FPGA. "
        "Só reconstrói a sessão VMM se a leitura física falhar ou o ProcInfo estiver preso a 0%%. "
        "RuntimeUnavailable NÃO força rebuild VMM.");
    if (CyberWidgets::GoldButton("Reconectar DMA", ImVec2(200, 36))) {
        if (Rust::ReinitDmaAsync())
            CyberWidgets::Notify("Reconexão DMA iniciada", CyberWidgets::ToastType::Info);
        else
            CyberWidgets::Notify("Motor ocupado", CyberWidgets::ToastType::Warning);
    }
    if (Rust::backend_busy.load()) {
        const auto snap = Rust::GetBackendSnapshot();
        ImGui::Text("Operação em curso... %llums", (unsigned long long)snap.operation_elapsed_ms);
        if (CyberWidgets::DangerButton("Cancelar", ImVec2(120, 28)))
            Rust::CancelBackendOperation();
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Opções");
    CyberWidgets::ToggleSwitch("Modo de desempenho", &Rust::config.performance_mode);
    CyberWidgets::ToggleSwitch("Tecla de emergência", &Rust::config.panic_key_enabled);
    CyberWidgets::InputInt("VK de emergência", &Rust::config.panic_key);
    CyberWidgets::EndCard();

    const auto snap = Rust::GetBackendSnapshot();
    CyberWidgets::BeginCard("Diagnóstico (estados separados)");
    ImGui::Text("Motor: %s", Rust::BackendStateName());
    ImGui::Text("FPGA/Dispositivo: %s", Rust::DevicePhaseName());
    ImGui::Text("Processo: %s", Rust::ProcessPhaseName());
    ImGui::Text("Runtime: %s", Rust::RuntimePhaseName());
    ImGui::Text("Razão: %s", Rust::BackendReasonName(snap.reason));
    ImGui::Separator();
    ImGui::Text("session_gen=%llu  process_gen=%llu",
        (unsigned long long)snap.session_generation,
        (unsigned long long)snap.process_generation);
    ImGui::Text("ProcInfo: state=%d progress=%d%% recovery=%s",
        snap.procinfo_state, snap.procinfo_progress,
        snap.procinfo_recovery_recommended ? "SIM" : "nao");
    ImGui::TextWrapped("%s", Rust::runtime.status);
    ImGui::Separator();
    ImGui::Text("Em jogo: %s", Rust::runtime.in_game ? "SIM" : "nao");
    ImGui::Text("Matriz (live): %s", Rust::runtime.matrix_ok ? "OK" : "—");
    ImGui::Text("Lista entidades: %s", Rust::runtime.list_ok ? "OK" : "—");
    ImGui::Text("Jogadores=%d  world=%d  GA=0x%llX",
        Rust::runtime.player_count,
        Rust::runtime.world_count,
        (unsigned long long)Rust::runtime.game_assembly);
    ImGui::TextDisabled(
        "Matriz so fica OK com leitura recente. PID/sessao nova limpa matriz/lista/local.");
    CyberWidgets::EndCard();
}
