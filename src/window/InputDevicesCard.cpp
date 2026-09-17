#pragma warning(disable: 4189 4100)
#include "InputDevicesCard.h"
#include "hardware_monitor.h"

#include "widgets.h"
#include "localization.h"
#include "aimbot/aim_type.h"
#include "ferrum/ferrum_device.h"
#include "kmbox/kmbox_net.h"
#include "makcu/makcu_wrapper.h"

#include <cstdio>
#include <string>

namespace InputDevicesCard {
namespace {

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
    // Update DMA status
    HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::DMA, true, "", "", "");
    
    // Update Makcu
    if (makcu_wrapper::IsConnected()) {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Makcu, true, 
            makcu_wrapper::GetPort(), makcu_wrapper::DiagnosticsLine(), "");
    } else {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Makcu, false, "", "", "");
    }
    HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::Makcu, aim_type::config.makcu_enabled);
    
    // Update KMBox-Net
    if (kmbox_net::IsConnected()) {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::KMBoxNet, true,
            std::string(aim_type::config.kmbox_ip) + ":" + aim_type::config.kmbox_port, "", "");
    } else {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::KMBoxNet, false, "", "", "");
    }
    HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::KMBoxNet, aim_type::config.kmbox_net_enabled);
    
    // Update Ferrum
    if (ferrum_device::IsConnected()) {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Ferrum, true,
            ferrum_device::ConnectedPort(), ferrum_device::DeviceVersion(), "");
    } else {
        HardwareMonitor::UpdateDeviceStatus(HardwareMonitor::DeviceType::Ferrum, false, "", "", "");
    }
    HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::Ferrum, aim_type::config.ferrum_enabled);
}

void DrawDeviceStatus(HardwareMonitor::DeviceType type, const HardwareMonitor::DeviceStatus& status) {
    // Connection status badge
    CyberWidgets::StatusBadge(status.name.c_str(), status.connected);
    
    // Health indicator
    std::string health = HardwareMonitor::GetDeviceHealthString(type);
    CyberWidgets::TextTone tone = CyberWidgets::TextTone::Success;
    if (health == "Disconnected" || health == "Stale") tone = CyberWidgets::TextTone::Error;
    else if (health == "High Latency" || health == "Elevated Latency") tone = CyberWidgets::TextTone::Warning;
    else if (health == "Disabled") tone = CyberWidgets::TextTone::Secondary;
    
    CyberWidgets::Badge(health.c_str(), tone);
    
    // Latency info - show real-time latency from system metrics for DMA
    if (status.connected) {
        char lat[64];
        if (type == HardwareMonitor::DeviceType::DMA) {
            const auto& metrics = HardwareMonitor::GetSystemMetrics();
            std::snprintf(lat, sizeof(lat), "Read: %.1fms Write: %.1fms", 
                metrics.dma_read_latency_ms, metrics.dma_write_latency_ms);
        } else {
            std::snprintf(lat, sizeof(lat), "Avg: %.1fms Max: %.1fms", 
                HardwareMonitor::GetAverageLatency(type, 5),
                HardwareMonitor::GetMaxLatency(type, 5));
        }
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
        NotifyResult(aim_type::Connect(aim_type::DeviceType::Makcu), aim_type::DeviceType::Makcu);
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("common.test"), ImVec2(105, 34)))
        NotifyResult(aim_type::Test(aim_type::DeviceType::Makcu), aim_type::DeviceType::Makcu);
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Automático###makcu", ImVec2(120, 34))) {
        makcu_wrapper::MakcuInitialize("");
        aim_type::Initialize();
        if (makcu_wrapper::IsConnected()) {
            aim_type::config.active = aim_type::DeviceType::Makcu;
            aim_type::config.makcu_enabled = true;
        }
        NotifyResult(makcu_wrapper::IsConnected(), aim_type::DeviceType::Makcu);
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
        NotifyResult(aim_type::Connect(aim_type::DeviceType::KmboxNet), aim_type::DeviceType::KmboxNet);
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
        NotifyResult(aim_type::Connect(aim_type::DeviceType::Ferrum), aim_type::DeviceType::Ferrum);
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("common.test"), ImVec2(105, 34)))
        NotifyResult(aim_type::Test(aim_type::DeviceType::Ferrum), aim_type::DeviceType::Ferrum);
    if (status.connected && CyberWidgets::CyberButton("Desligar###ferrum", ImVec2(120, 32)))
        aim_type::Disconnect(aim_type::DeviceType::Ferrum);
}

} // namespace

void Draw() {
    aim_type::Initialize();
    UpdateHardwareMonitor();
    
    CyberWidgets::BeginCard(Loc::Tr("aim.devices"));
    ImGui::Text("%s", aim_type::StatusText());
    ImGui::TextDisabled("O último dispositivo ligado fica ativo em todos os jogos.");
    CyberWidgets::CardGap(8.0f);
    
    // DMA Status
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

} // namespace InputDevicesCard
