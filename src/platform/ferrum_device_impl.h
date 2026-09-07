#pragma once

#include "input_device.h"
#include "../ferrum/ferrum_device.h"
#include <chrono>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {

class FerrumDeviceImpl final : public IInputDevice {
public:
    FerrumDeviceImpl() = default;
    ~FerrumDeviceImpl() override { Disconnect(); }

    [[nodiscard]] InputDeviceDescriptor GetDescriptor() const noexcept override {
        InputDeviceDescriptor desc;
        desc.type = InputDeviceType::Ferrum;
        desc.name = "Ferrum";
        desc.identifier = connectedPort_;
        desc.capabilities = InputCapability::MouseMove | InputCapability::MouseClick | InputCapability::ButtonMask | InputCapability::Diagnostics;
        desc.isConnected = ferrum_device::IsConnected();
        desc.firmwareVersion = ferrum_device::DeviceVersion();
        return desc;
    }

    [[nodiscard]] InputDeviceType GetType() const noexcept override { return InputDeviceType::Ferrum; }
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Ferrum"; }
    [[nodiscard]] std::string_view GetIdentifier() const noexcept override { return connectedPort_.c_str(); }

    Result<void> Connect(const std::string& identifier) noexcept override {
        connectedPort_ = identifier;
        bool ok = ferrum_device::Connect(identifier);
        return ok ? Ok() : Err<void>(ferrum_device::LastError());
    }

    void Disconnect() noexcept override {
        ferrum_device::Disconnect();
        connectedPort_.clear();
    }

    [[nodiscard]] bool IsConnected() const noexcept override { return ferrum_device::IsConnected(); }
    [[nodiscard]] Result<bool> Probe() const noexcept override { return Ok(ferrum_device::IsConnected()); }

    Result<void> Move(int x, int y) noexcept override {
        if (!ferrum_device::Move(x, y)) return Err<void>("Move failed");
        return Ok();
    }

    Result<void> MoveRelative(int dx, int dy) noexcept override {
        if (!ferrum_device::Move(dx, dy)) return Err<void>("Move failed");
        return Ok();
    }

    Result<void> SetLeft(bool down) noexcept override {
        if (!ferrum_device::SetLeft(down)) return Err<void>("SetLeft failed");
        return Ok();
    }

    Result<void> SetRight(bool down) noexcept override { (void)down; return Ok(); }
    Result<void> SetMiddle(bool down) noexcept override { (void)down; return Ok(); }
    Result<void> SetX1(bool down) noexcept override { (void)down; return Ok(); }
    Result<void> SetX2(bool down) noexcept override { (void)down; return Ok(); }

    [[nodiscard]] ButtonState GetButtonState(int virtualKey) const noexcept override { (void)virtualKey;
        ButtonState result;
        uint8_t mask = ferrum_device::ButtonMask();
        result.down = (mask & 0x1) != 0;
        result.source = InputDeviceType::Ferrum;
        result.deviceMask = mask;
        return result;
    }

    [[nodiscard]] bool IsDown(int virtualKey) const noexcept override { (void)virtualKey;
        uint8_t mask = ferrum_device::ButtonMask();
        return (mask & 0x1) != 0;
    }

    [[nodiscard]] bool IsKeyJustPressed(int virtualKey) const noexcept override { (void)virtualKey; return false; }
    [[nodiscard]] bool IsKeyJustReleased(int virtualKey) const noexcept override { (void)virtualKey; return false; }

    [[nodiscard]] uint8_t GetButtonMask() const noexcept override {
        return ferrum_device::ButtonMask();
    }

    [[nodiscard]] DevicePacket PollButtons() noexcept override {
        DevicePacket result;
        result.hasPacket = true;
        result.buttons = ferrum_device::ButtonMask();
        result.timestamp = std::chrono::steady_clock::now();
        return result;
    }

    [[nodiscard]] InputDiagnostics GetDiagnostics() const noexcept override {
        InputDiagnostics result;
        result.isConnected = ferrum_device::IsConnected();
        result.deviceType = InputDeviceType::Ferrum;
        result.identifier = connectedPort_;
        result.buttonMask = ferrum_device::ButtonMask();
        result.deviceInfo = "Port: " + ferrum_device::ConnectedPort() + ", Ver: " + ferrum_device::DeviceVersion();
        result.lastUpdate = std::chrono::steady_clock::now();
        return result;
    }

    [[nodiscard]] std::string GetConnectionStatus() const noexcept override {
        return ferrum_device::IsConnected() ? "Connected" : "Disconnected: " + ferrum_device::LastError();
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
    std::string connectedPort_;
};

} // namespace OmniGhost::Platform