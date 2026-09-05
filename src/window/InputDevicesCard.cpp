#include "InputDevicesCard.h"

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

void DrawMakcu() {
    const bool connected = makcu_wrapper::IsConnected();
    CyberWidgets::StatusBadge("Makcu", connected);
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
    if (connected && CyberWidgets::CyberButton("Desligar###makcu", ImVec2(120, 32)))
        aim_type::Disconnect(aim_type::DeviceType::Makcu);
    if (connected)
        ImGui::TextDisabled("%s", makcu_wrapper::DiagnosticsLine());
}

void DrawKmboxNet() {
    const bool connected = kmbox_net::IsConnected();
    CyberWidgets::StatusBadge("Kmbox-Net", connected);
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
    if (connected && CyberWidgets::CyberButton("Desligar###kmbox", ImVec2(120, 32)))
        aim_type::Disconnect(aim_type::DeviceType::KmboxNet);
    if (connected)
        ImGui::TextDisabled("UDP binário ativo em %s:%s", aim_type::config.kmbox_ip,
            aim_type::config.kmbox_port);
}

void DrawFerrum() {
    const bool connected = ferrum_device::IsConnected();
    CyberWidgets::StatusBadge("Ferrum", connected);
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
    if (connected && CyberWidgets::CyberButton("Desligar###ferrum", ImVec2(120, 32)))
        aim_type::Disconnect(aim_type::DeviceType::Ferrum);
    if (connected) {
        const std::string port = ferrum_device::ConnectedPort();
        const std::string version = ferrum_device::DeviceVersion();
        ImGui::TextDisabled("%s%s%s", port.c_str(), version.empty() ? "" : " | ", version.c_str());
    } else {
        ImGui::TextDisabled("Deteta automaticamente Ferrum / Silicon Labs CP210x; inicia a 115200 baud.");
    }
}

} // namespace

void Draw() {
    aim_type::Initialize();
    CyberWidgets::BeginCard(Loc::Tr("aim.devices"));
    ImGui::Text("%s", aim_type::StatusText());
    ImGui::TextDisabled("O último dispositivo ligado fica ativo em todos os jogos.");

    DrawMakcu();
    CyberWidgets::Separator();
    DrawKmboxNet();
    CyberWidgets::Separator();
    DrawFerrum();
    CyberWidgets::EndCard();
}

} // namespace InputDevicesCard
