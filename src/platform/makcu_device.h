#pragma once

#include "input_device.h"
#include "../makcu/makcu_wrapper.h"
#include <chrono>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {

class MakcuDevice final : public IInputDevice {
public:
    MakcuDevice() = default;
    ~MakcuDevice() override { Disconnect(); }

    [[nodiscard]] InputDeviceDescriptor GetDescriptor() const noexcept override {
        InputDeviceDescriptor desc;
        desc.type = InputDeviceType::Makcu;
        desc.name = "MAKCU";
        desc.identifier = connectedPort_;
        desc.capabilities = InputCapability::MouseMove | InputCapability::MouseClick | InputCapability::ButtonMask | InputCapability::Diagnostics;
        desc.isConnected = makcu_wrapper::IsConnected();
        desc.firmwareVersion = makcu_wrapper::GetDiagnostics().deviceInfo;
        return desc;
    }

    [[nodiscard]] InputDeviceType GetType() const noexcept override { return InputDeviceType::Makcu; }
    [[nodiscard]] std::string_view GetName() const noexcept override { return "MAKCU"; }
    [[nodiscard]] std::string_view GetIdentifier() const noexcept override { return connectedPort_; }

    Result<void> Connect(const std::string& identifier) noexcept override {
        connectedPort_ = identifier;
        makcu_wrapper::MakcuInitialize(identifier);
        return makcu_wrapper::IsConnected() ? Ok() : Err<void>("Failed to connect MAKCU");
    }

    void Disconnect() noexcept override {
        makcu_wrapper::MakcuShutdown();
        connectedPort_.clear();
    }

    [[nodiscard]] bool IsConnected() const noexcept override { return makcu_wrapper::IsConnected(); }
    [[nodiscard]] Result<bool> Probe() const noexcept override { return Ok(makcu_wrapper::IsConnected()); }

    Result<void> Move(int x, int y) noexcept override {
        makcu_wrapper::move(x, y);
        return Ok();
    }

    Result<void> MoveRelative(int dx, int dy) noexcept override {
        makcu_wrapper::move(dx, dy);
        return Ok();
    }

    Result<void> SetLeft(bool down) noexcept override {
        down ? makcu_wrapper::left_click() : makcu_wrapper::left_click_release();
        return Ok();
    }

    Result<void> SetRight(bool down) noexcept override { (void)down; return Ok(); } // Not directly supported
    Result<void> SetMiddle(bool down) noexcept override { (void)down; return Ok(); }
    Result<void> SetX1(bool down) noexcept override { (void)down; return Ok(); }
    Result<void> SetX2(bool down) noexcept override { (void)down; return Ok(); }

    [[nodiscard]] ButtonState GetButtonState(int virtualKey) const noexcept override { (void)virtualKey;
        auto state = makcu_wrapper::GetButtonState(virtualKey);
        ButtonState result;
        result.down = state.down;
        result.source = InputDeviceType::Makcu;
        result.deviceMask = state.makcuMask;
        return result;
    }

    [[nodiscard]] bool IsDown(int virtualKey) const noexcept override {
        (void)virtualKey; return makcu_wrapper::IsDown(virtualKey);
    }

    [[nodiscard]] bool IsKeyJustPressed(int virtualKey) const noexcept override {
        (void)virtualKey; return makcu_wrapper::IsKeyJustPressed(virtualKey);
    }

    [[nodiscard]] bool IsKeyJustReleased(int virtualKey) const noexcept override {
        (void)virtualKey; return makcu_wrapper::IsKeyJustReleased(virtualKey);
    }

    [[nodiscard]] uint8_t GetButtonMask() const noexcept override {
        return makcu_wrapper::GetButtonMask();
    }

    [[nodiscard]] DevicePacket PollButtons() noexcept override {
        auto packet = makcu_wrapper::PollButtons();
        DevicePacket result;
        result.hasPacket = packet.hasPacket;
        result.buttons = packet.buttons;
        result.timestamp = std::chrono::steady_clock::now();
        return result;
    }

    [[nodiscard]] InputDiagnostics GetDiagnostics() const noexcept override {
        auto diag = makcu_wrapper::GetDiagnostics();
        InputDiagnostics result;
        result.isConnected = diag.connected;
        result.isMonitoring = diag.monitoringConfirmed;
        result.deviceType = InputDeviceType::Makcu;
        result.identifier = connectedPort_;
        result.buttonMask = diag.buttonMask;
        result.lastPacketMs = diag.lastPacketMs;
        result.packetsReceived = diag.packetsReceived;
        result.parseErrors = diag.parseErrors;
        result.deviceInfo = diag.deviceInfo;
        result.faultInfo = diag.faultInfo;
        result.lastSource = diag.lastSource;
        result.lastUpdate = std::chrono::steady_clock::now();
        return result;
    }

    [[nodiscard]] std::string GetConnectionStatus() const noexcept override {
        return makcu_wrapper::GetConnectionStatus();
    }

    [[nodiscard]] uint64_t PacketsReceived() const noexcept override {
        return makcu_wrapper::PacketsReceived();
    }

    void ForceClearButtons() noexcept override {
        makcu_wrapper::ForceClearButtons();
    }

    void EnsureMonitoring() noexcept override {
        makcu_wrapper::EnsureButtonMonitoring();
    }

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