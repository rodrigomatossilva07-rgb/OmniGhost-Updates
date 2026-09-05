#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "aim_type.h"
#include "ferrum/ferrum_device.h"
#include "kmbox/kmbox_net.h"
#include "makcu/makcu_wrapper.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace aim_type {

Config config;

namespace {

std::uint8_t LocalButtonMask() {
    std::uint8_t mask = 0;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) mask |= 0x01;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) mask |= 0x02;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) mask |= 0x04;
    if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) mask |= 0x08;
    if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) mask |= 0x10;
    return mask;
}

bool MaskContains(std::uint8_t mask, int virtualKey) {
    switch (virtualKey) {
    case VK_LBUTTON:  return (mask & 0x01) != 0;
    case VK_RBUTTON:  return (mask & 0x02) != 0;
    case VK_MBUTTON:  return (mask & 0x04) != 0;
    case VK_XBUTTON1: return (mask & 0x08) != 0;
    case VK_XBUTTON2: return (mask & 0x10) != 0;
    default: return false;
    }
}

int ParsePort(const char* value) {
    if (!value || !*value) return 0;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || !end || *end != '\0' || parsed < 1 || parsed > 65535)
        return 0;
    return static_cast<int>(parsed);
}

void SoftwareMove(int x, int y) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = x;
    input.mi.dy = y;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(input));
}

void SoftwareLeft(bool down) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(input));
}

} // namespace

void Initialize() {
    const int active = static_cast<int>(config.active);
    if (active < static_cast<int>(DeviceType::None) ||
        active > static_cast<int>(DeviceType::Makcu))
        config.active = DeviceType::None;
    config.makcu_connected = makcu_wrapper::IsConnected();
    config.kmbox_net_connected = kmbox_net::IsConnected();
    config.ferrum_connected = ferrum_device::IsConnected();
}

void Shutdown() {
    kmbox_net::Disconnect();
    ferrum_device::Disconnect();
    makcu_wrapper::MakcuShutdown();
    config.kmbox_net_connected = false;
    config.ferrum_connected = false;
    config.makcu_connected = false;
}

bool Connect(DeviceType type) {
    switch (type) {
    case DeviceType::Makcu:
        makcu_wrapper::MakcuInitialize(std::string(config.makcu_com));
        config.makcu_connected = makcu_wrapper::IsConnected();
        if (config.makcu_connected) {
            config.active = type;
            config.makcu_enabled = true;
        }
        return config.makcu_connected;

    case DeviceType::KmboxNet: {
        const int port = ParsePort(config.kmbox_port);
        config.kmbox_net_connected = kmbox_net::Connect(
            config.kmbox_ip, port, config.kmbox_uuid);
        if (config.kmbox_net_connected) {
            config.active = type;
            config.kmbox_net_enabled = true;
        }
        return config.kmbox_net_connected;
    }

    case DeviceType::Ferrum:
        config.ferrum_connected = ferrum_device::Connect(
            config.ferrum_com,
            config.ferrum_baud > 0 ? config.ferrum_baud : 115200);
        if (config.ferrum_connected) {
            config.active = type;
            config.ferrum_enabled = true;
        }
        return config.ferrum_connected;

    default:
        return false;
    }
}

void Disconnect(DeviceType type) {
    switch (type) {
    case DeviceType::Makcu:
        makcu_wrapper::MakcuShutdown();
        config.makcu_connected = false;
        config.makcu_enabled = false;
        break;
    case DeviceType::KmboxNet:
        kmbox_net::Disconnect();
        config.kmbox_net_connected = false;
        config.kmbox_net_enabled = false;
        break;
    case DeviceType::Ferrum:
        ferrum_device::Disconnect();
        config.ferrum_connected = false;
        config.ferrum_enabled = false;
        break;
    default:
        break;
    }
    if (config.active == type)
        config.active = DeviceType::None;
}

bool Test(DeviceType type) {
    const DeviceType previous = config.active;
    config.active = type;
    const bool connected =
        (type == DeviceType::Makcu && makcu_wrapper::IsConnected()) ||
        (type == DeviceType::KmboxNet && kmbox_net::IsConnected()) ||
        (type == DeviceType::Ferrum && ferrum_device::IsConnected());
    if (connected) {
        Move(15, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        Move(-15, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        Move(0, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        Move(0, -10);
    }
    config.active = previous;
    Initialize();
    return connected;
}

void Move(int x, int y) {
    if (x == 0 && y == 0) return;

    switch (config.active) {
    case DeviceType::Makcu:
        if (makcu_wrapper::IsConnected()) { makcu_wrapper::move(x, y); return; }
        break;
    case DeviceType::KmboxNet:
        if (kmbox_net::IsConnected()) { kmbox_net::Move(x, y); return; }
        break;
    case DeviceType::Ferrum:
        if (ferrum_device::IsConnected()) { ferrum_device::Move(x, y); return; }
        break;
    default:
        break;
    }
    if (kmbox_net::IsConnected()) { kmbox_net::Move(x, y); return; }
    if (makcu_wrapper::IsConnected()) { makcu_wrapper::move(x, y); return; }
    if (ferrum_device::IsConnected()) { ferrum_device::Move(x, y); return; }
    SoftwareMove(x, y);
}

void LeftClick() {
    switch (config.active) {
    case DeviceType::Makcu:
        if (makcu_wrapper::IsConnected()) { makcu_wrapper::left_click(); return; }
        break;
    case DeviceType::KmboxNet:
        if (kmbox_net::IsConnected()) { kmbox_net::SetLeft(true); return; }
        break;
    case DeviceType::Ferrum:
        if (ferrum_device::IsConnected()) { ferrum_device::SetLeft(true); return; }
        break;
    default:
        break;
    }
    if (kmbox_net::IsConnected()) { kmbox_net::SetLeft(true); return; }
    if (makcu_wrapper::IsConnected()) { makcu_wrapper::left_click(); return; }
    if (ferrum_device::IsConnected()) { ferrum_device::SetLeft(true); return; }
    SoftwareLeft(true);
}

void LeftClickRelease() {
    switch (config.active) {
    case DeviceType::Makcu:
        if (makcu_wrapper::IsConnected()) { makcu_wrapper::left_click_release(); return; }
        break;
    case DeviceType::KmboxNet:
        if (kmbox_net::IsConnected()) { kmbox_net::SetLeft(false); return; }
        break;
    case DeviceType::Ferrum:
        if (ferrum_device::IsConnected()) { ferrum_device::SetLeft(false); return; }
        break;
    default:
        break;
    }
    if (kmbox_net::IsConnected()) { kmbox_net::SetLeft(false); return; }
    if (makcu_wrapper::IsConnected()) { makcu_wrapper::left_click_release(); return; }
    if (ferrum_device::IsConnected()) { ferrum_device::SetLeft(false); return; }
    SoftwareLeft(false);
}

std::uint8_t ButtonMask() {
    switch (config.active) {
    case DeviceType::Makcu:
        if (makcu_wrapper::IsConnected()) {
            makcu_wrapper::EnsureButtonMonitoring();
            return makcu_wrapper::GetButtonMask();
        }
        break;
    case DeviceType::KmboxNet:
        if (kmbox_net::IsConnected()) return kmbox_net::ButtonMask();
        break;
    case DeviceType::Ferrum:
        if (ferrum_device::IsConnected()) return ferrum_device::ButtonMask();
        break;
    default:
        break;
    }
    if (kmbox_net::IsConnected()) return kmbox_net::ButtonMask();
    if (makcu_wrapper::IsConnected()) {
        makcu_wrapper::EnsureButtonMonitoring();
        return makcu_wrapper::GetButtonMask();
    }
    if (ferrum_device::IsConnected()) return ferrum_device::ButtonMask();
    return LocalButtonMask();
}

bool IsDown(int virtualKey) {
    if (virtualKey <= 0) return false;
    const bool local = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    if (virtualKey >= VK_LBUTTON && virtualKey <= VK_XBUTTON2)
        return MaskContains(ButtonMask(), virtualKey) || local;
    return local;
}

bool IsConnected() {
    return makcu_wrapper::IsConnected() || kmbox_net::IsConnected() ||
        ferrum_device::IsConnected();
}

const char* StatusText() {
    if (config.active == DeviceType::KmboxNet && kmbox_net::IsConnected())
        return "Kmbox NET LIGADO (ativo)";
    if (config.active == DeviceType::Makcu && makcu_wrapper::IsConnected())
        return "Makcu LIGADO (ativo)";
    if (config.active == DeviceType::Ferrum && ferrum_device::IsConnected())
        return "Ferrum LIGADO (ativo)";
    if (kmbox_net::IsConnected()) return "Kmbox NET LIGADO";
    if (makcu_wrapper::IsConnected()) return "Makcu LIGADO";
    if (ferrum_device::IsConnected()) return "Ferrum LIGADO";
    return "Nenhum dispositivo";
}

std::string LastError(DeviceType type) {
    switch (type) {
    case DeviceType::KmboxNet: return kmbox_net::LastError();
    case DeviceType::Ferrum: return ferrum_device::LastError();
    case DeviceType::Makcu: return makcu_wrapper::DiagnosticsLine();
    default: return {};
    }
}

} // namespace aim_type
