#pragma once

#include "input_device.h"
#include "../kmbox/kmbox_net.h"
#include <chrono>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {

class KMBoxDevice final : public IInputDevice {
public:
    KMBoxDevice() = default;
    ~KMBoxDevice() override { Disconnect(); }

    [[nodiscard]] InputDeviceDescriptor GetDescriptor() const noexcept override {
        InputDeviceDescriptor desc;
        desc.type = InputDeviceType::KMBoxNet;
        desc.name = "KMBox Net";
        desc.identifier = ip_ + ":" + std::to_string(port_) + " (" + uuid_ + ")";
        desc.capabilities = InputCapability::MouseMove | InputCapability::MouseClick | InputCapability::ButtonMask | InputCapability::Diagnostics;
        desc.isConnected = kmbox_net::IsConnected();
        return desc;
    }

    [[nodiscard]] InputDeviceType GetType() const noexcept override { return InputDeviceType::KMBoxNet; }
    [[nodiscard]] std::string_view GetName() const noexcept override { return "KMBox Net"; }
    [[nodiscard]] std::string_view GetIdentifier() const noexcept override { return identifier_.c_str(); }

    Result<void> Connect(const std::string& identifier) noexcept override {
        // Parse identifier as "ip:port:uuid"
        size_t pos1 = identifier.find(':');
        size_t pos2 = identifier.find(':', pos1 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos) {
            return Err<void>("Invalid identifier format (expected ip:port:uuid)");
        }
        ip_ = identifier.substr(0, pos1);
        port_ = std::stoi(identifier.substr(pos1 + 1, pos2 - pos1 - 1));
        uuid_ = identifier.substr(pos2 + 1);
        identifier_ = identifier;
        
        bool ok = kmbox_net::Connect(ip_, port_, uuid_);
        return ok ? Ok() : Err<void>(kmbox_net::LastError());
    }

    void Disconnect() noexcept override {
        kmbox_net::Disconnect();
    }

    [[nodiscard]] bool IsConnected() const noexcept override { return kmbox_net::IsConnected(); }
    [[nodiscard]] Result<bool> Probe() const noexcept override { return Ok(kmbox_net::IsConnected()); }

    Result<void> Move(int x, int y) noexcept override {
        if (!kmbox_net::Move(x, y)) return Err<void>("Move failed");
        return Ok();
    }

    Result<void> MoveRelative(int dx, int dy) noexcept override {
        if (!kmbox_net::Move(dx, dy)) return Err<void>("Move failed");
        return Ok();
    }

    Result<void> SetLeft(bool down) noexcept override {
        if (!kmbox_net::SetLeft(down)) return Err<void>("SetLeft failed");
        return Ok();
    }

    Result<void> SetRight(bool down) noexcept override { (void)down; return Ok(); }
    Result<void> SetMiddle(bool down) noexcept override { (void)down; return Ok(); }
    Result<void> SetX1(bool down) noexcept override { (void)down; return Ok(); }
    Result<void> SetX2(bool down) noexcept override { (void)down; return Ok(); }

    [[nodiscard]] ButtonState GetButtonState(int virtualKey) const noexcept override { (void)virtualKey;
        ButtonState result;
        uint8_t mask = kmbox_net::ButtonMask();
        result.down = (mask & 0x1) != 0; // Left click
        result.source = InputDeviceType::KMBoxNet;
        result.deviceMask = mask;
        return result;
    }

    [[nodiscard]] bool IsDown(int virtualKey) const noexcept override { (void)virtualKey;
        uint8_t mask = kmbox_net::ButtonMask();
        return (mask & 0x1) != 0;
    }

    [[nodiscard]] bool IsKeyJustPressed(int virtualKey) const noexcept override { (void)virtualKey; return false; }
    [[nodiscard]] bool IsKeyJustReleased(int virtualKey) const noexcept override { (void)virtualKey; return false; }

    [[nodiscard]] uint8_t GetButtonMask() const noexcept override {
        return kmbox_net::ButtonMask();
    }

    [[nodiscard]] DevicePacket PollButtons() noexcept override {
        DevicePacket result;
        result.hasPacket = true;
        result.buttons = kmbox_net::ButtonMask();
        result.timestamp = std::chrono::steady_clock::now();
        return result;
    }

    [[nodiscard]] InputDiagnostics GetDiagnostics() const noexcept override {
        InputDiagnostics result;
        result.isConnected = kmbox_net::IsConnected();
        result.deviceType = InputDeviceType::KMBoxNet;
        result.identifier = identifier_;
        result.buttonMask = kmbox_net::ButtonMask();
        result.lastUpdate = std::chrono::steady_clock::now();
        return result;
    }

    [[nodiscard]] std::string GetConnectionStatus() const noexcept override {
        return kmbox_net::IsConnected() ? "Connected" : "Disconnected: " + kmbox_net::LastError();
    }

    [[nodiscard]] uint64_t PacketsReceived() const noexcept override { return 0; }

    void ForceClearButtons() noexcept override {}
    void EnsureMonitoring() noexcept override {}

    [[nodiscard]] InputCapability GetCapabilities() const noexcept override {
        return InputCapability::MouseMove | InputCapability::MouseClick | InputCapability::ButtonMask | InputCapability::Diagnostics;
    }

    [[nodiscard]] bool HasCapability(InputCapability cap) const noexcept override {
        return OmniGhost::Platform::HasCapability(GetCapabilities(), cap);
    }

private:
    std::string ip_;
    int port_ = 0;
    std::string uuid_;
    std::string identifier_;
};

} // namespace OmniGhost::Platform