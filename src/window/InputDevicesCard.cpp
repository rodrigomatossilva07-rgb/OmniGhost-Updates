#pragma warning(disable: 4189 4100)
#include "InputDevicesCard.h"
#include "hardware_monitor.h"

#include "widgets.h"
#include "localization.h"
#include "aimbot/aim_type.h"
#include "ferrum/ferrum_device.h"
#include "kmbox/kmbox_net.h"
#include "makcu/makcu_wrapper.h"
#include "Memory/Memory.h"

#include <cstdio>
#include <string>
#include <atomic>
#include <thread>

namespace InputDevicesCard {
namespace {

std::jthread g_connect_worker;
std::atomic<int> g_connect_state{0};
aim_type::DeviceType g_connect_device = aim_type::DeviceType::None;
bool g_connect_auto_makcu = false;

void NotifyResult(bool ok, aim_type::DeviceType type);

void PollConnectWorker() {
    const int state = g_connect_state.load(std::memory_order_acquire);
    if (state == 0 || state == 1)
        return;
    const aim_type::DeviceType device = g_connect_device;
    const bool autoMakcu = g_connect_auto_makcu;
    g_connect_state.store(0, std::memory_order_release);
    const bool ok = state == 2;
    if (ok && autoMakcu) {
        aim_type::config.active = aim_type::DeviceType::Makcu;
        aim_type::config.makcu_enabled = true;
    }
    NotifyResult(ok, device);
}

void StartConnect(aim_type::DeviceType device, bool autoMakcu) {
    int expected = 0;
    if (!g_connect_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
        return;
    if (g_connect_worker.joinable())
        g_connect_worker.join();
    g_connect_device = device;
    g_connect_auto_makcu = autoMakcu;
    g_connect_worker = std::jthread([device, autoMakcu](std::stop_token) {
        bool ok = false;
        try {
            if (autoMakcu) {
                makcu_wrapper::MakcuInitialize("");
                aim_type::Initialize();
                ok = makcu_wrapper::IsConnected();
            } else {
                ok = aim_type::Connect(device);
            }
        } catch (...) {
            ok = false;
        }
        g_connect_state.store(ok ? 2 : 3, std::memory_order_release);
    });
}

void NotifyResult(bool ok, aim_type::DeviceType type) {
    std::string message = ok ? Loc::Tr("status.device_ok") : Loc::Tr("status.offline");
    if (!ok) {
        const std::string detail = aim_type::LastError(type);
        if (!detail.empty()) {
            message += " | ";
            message += detail;
        }
    }
    CyberWidgets::Notify(message.c_str(),
        ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
}

HardwareMonitor::DeviceType AimTypeToHW(aim_type::DeviceType type) {
    switch (type) {
        case aim_type::DeviceType::Makcu: return HardwareMonitor::DeviceType::Makcu;
        case aim_type::DeviceType::KmboxNet: return HardwareMonitor::DeviceType::KMBoxNet;
        case aim_type::DeviceType::Ferrum: return HardwareMonitor::DeviceType::Ferrum;
        default: return HardwareMonitor::DeviceType::DMA;
    }
}

void UpdateHardwareMonitor() {

    const auto dmaSnapshot = mem.GetDiagnosticsSnapshot();
    if (dmaSnapshot.deviceOpen || dmaSnapshot.deviceDetected) {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::DMA, true,
            dmaSnapshot.deviceOpen ? "Session active" : "PnP detected",
            dmaSnapshot.deviceOpen ? "Attached" : "", "");
    } else {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::DMA, false, "", "", "");
    }


    if (makcu_wrapper::IsConnectedSnapshot()) {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Makcu, true,
            "COM", makcu_wrapper::DiagnosticsLine(), "");
    } else {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Makcu, false, "", "", "");
    }
    HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::Makcu, aim_type::config.makcu_enabled);


    if (kmbox_net::IsConnected()) {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::KMBoxNet, true,
            std::string(aim_type::config.kmbox_ip) + ":" + aim_type::config.kmbox_port, "", "");
    } else {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::KMBoxNet, false, "", "", "");
    }
    HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::KMBoxNet, aim_type::config.kmbox_net_enabled);


    if (ferrum_device::IsConnected()) {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Ferrum, true,
            ferrum_device::ConnectedPort(), ferrum_device::DeviceVersion(), "");
    } else {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Ferrum, false, "", "", "");
    }
    HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::Ferrum, aim_type::config.ferrum_enabled);
}

void DrawDeviceStatus(HardwareMonitor::DeviceType type, const HardwareMonitor::DeviceStatus& status) {

    CyberWidgets::StatusBadge(status.name.c_str(), status.connected);


    std::string health = HardwareMonitor::GetDeviceHealthString(type);
    CyberWidgets::TextTone tone = CyberWidgets::TextTone::Success;
    if (health == "Disconnected" || health == "Stale") tone = CyberWidgets::TextTone::Error;
    else if (health == "High Latency" || health == "Elevated Latency") tone = CyberWidgets::TextTone::Warning;
    else if (health == "Disabled") tone = CyberWidgets::TextTone::Secondary;

    CyberWidgets::Badge(health.c_str(), tone);


    if (status.connected) {
        char lat[64];
        std::snprintf(lat, sizeof(lat), "Avg: %.1fms Max: %.1fms",
            HardwareMonitor::GetAverageLatency(type, 5),
            HardwareMonitor::GetMaxLatency(type, 5));
        CyberWidgets::KeyValueRow("Latency", lat);
    }

    if (!status.port.empty()) {
        CyberWidgets::KeyValueRow("Port", status.port.c_str());
    }
    if (!status.version.empty()) {
        CyberWidgets::KeyValueRow("Version", status.version.c_str());
    }
    if (!status.last_error.empty()) {
        CyberWidgets::KeyValueRow("Last Error", status.last_error.c_str());
    }
}

void DrawMakcu() {
    const auto& status = HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::Makcu);
    DrawDeviceStatus(HardwareMonitor::DeviceType::Makcu, status);
    CyberWidgets::Separator();

    if (CyberWidgets::ToggleSwitch(Loc::TrID("aim.makcu"), &aim_type::config.makcu_enabled) &&
        !aim_type::config.makcu_enabled)
        aim_type::Disconnect(aim_type::DeviceType::Makcu);
    CyberWidgets::TextInput("COM###makcu", aim_type::config.makcu_com,
        sizeof(aim_type::config.makcu_com), "Automático / COM4");
    CyberWidgets::InputInt("Baud###makcu", &aim_type::config.makcu_baud);

    if (CyberWidgets::GoldButton(Loc::TrID("common.connect"), ImVec2(120, 34)))
        StartConnect(aim_type::DeviceType::Makcu, false);
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("common.test"), ImVec2(105, 34)))
        NotifyResult(aim_type::Test(aim_type::DeviceType::Makcu), aim_type::DeviceType::Makcu);
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Automático###makcu", ImVec2(120, 34))) {
        StartConnect(aim_type::DeviceType::Makcu, true);
    }
    if (status.connected && CyberWidgets::CyberButton("Desligar###makcu", ImVec2(120, 32)))
        aim_type::Disconnect(aim_type::DeviceType::Makcu);
}

void DrawKmboxNet() {
    const auto& status = HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::KMBoxNet);
    DrawDeviceStatus(HardwareMonitor::DeviceType::KMBoxNet, status);
    CyberWidgets::Separator();

    if (CyberWidgets::ToggleSwitch(Loc::TrID("aim.kmbox_net"), &aim_type::config.kmbox_net_enabled) &&
        !aim_type::config.kmbox_net_enabled)
        aim_type::Disconnect(aim_type::DeviceType::KmboxNet);
    CyberWidgets::TextInput(Loc::TrID("aim.ip"), aim_type::config.kmbox_ip,
        sizeof(aim_type::config.kmbox_ip), "192.168.2.188");
    CyberWidgets::TextInput(Loc::TrID("aim.port"), aim_type::config.kmbox_port,
        sizeof(aim_type::config.kmbox_port), "19856");
    CyberWidgets::TextInput(Loc::TrID("aim.uuid"), aim_type::config.kmbox_uuid,
        sizeof(aim_type::config.kmbox_uuid), "8 digitos hexadecimais");

    if (CyberWidgets::GoldButton(Loc::TrID("common.connect"), ImVec2(120, 34)))
        StartConnect(aim_type::DeviceType::KmboxNet, false);
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("common.test"), ImVec2(105, 34)))
        NotifyResult(aim_type::Test(aim_type::DeviceType::KmboxNet), aim_type::DeviceType::KmboxNet);
    if (status.connected && CyberWidgets::CyberButton("Desligar###kmbox", ImVec2(120, 32)))
        aim_type::Disconnect(aim_type::DeviceType::KmboxNet);
}

void DrawFerrum() {
    const auto& status = HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::Ferrum);
    DrawDeviceStatus(HardwareMonitor::DeviceType::Ferrum, status);
    CyberWidgets::Separator();

    if (CyberWidgets::ToggleSwitch("Ferrum", &aim_type::config.ferrum_enabled) &&
        !aim_type::config.ferrum_enabled)
        aim_type::Disconnect(aim_type::DeviceType::Ferrum);
    CyberWidgets::TextInput("COM###ferrum", aim_type::config.ferrum_com,
        sizeof(aim_type::config.ferrum_com), "Automático / COM3");
    CyberWidgets::InputInt("Baud###ferrum", &aim_type::config.ferrum_baud);

    if (CyberWidgets::GoldButton(Loc::TrID("common.connect"), ImVec2(120, 34)))
        StartConnect(aim_type::DeviceType::Ferrum, false);
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("common.test"), ImVec2(105, 34)))
        NotifyResult(aim_type::Test(aim_type::DeviceType::Ferrum), aim_type::DeviceType::Ferrum);
    if (status.connected && CyberWidgets::CyberButton("Desligar###ferrum", ImVec2(120, 32)))
        aim_type::Disconnect(aim_type::DeviceType::Ferrum);
}

}

void Draw() {
    aim_type::Initialize();
    PollConnectWorker();
    UpdateHardwareMonitor();

    CyberWidgets::BeginCard(Loc::Tr("aim.devices"));
    ImGui::Text("%s", aim_type::StatusText());
    ImGui::TextDisabled("O último dispositivo ligado fica ativo em todos os jogos.");
    CyberWidgets::CardGap(8.0f);


    const auto& dma_status = HardwareMonitor::GetDeviceStatus(HardwareMonitor::DeviceType::DMA);
    DrawDeviceStatus(HardwareMonitor::DeviceType::DMA, dma_status);
    CyberWidgets::Separator();

    DrawMakcu();
    CyberWidgets::Separator();
    DrawKmboxNet();
    CyberWidgets::Separator();
    DrawFerrum();
    CyberWidgets::EndCard();
}

}
