#pragma once

#include "input_device.h"
#include <chrono>
#include <unordered_map>

namespace OmniGhost::Platform {

class VirtualInputDevice final : public IInputDevice {
public:
    VirtualInputDevice() = default;
    ~VirtualInputDevice() override = default;

    [[nodiscard]] InputDeviceDescriptor GetDescriptor() const noexcept override {
        InputDeviceDescriptor desc;
        desc.type = InputDeviceType::Virtual;
        desc.name = "Virtual Input (Testing)";
        desc.identifier = "virtual";
        desc.capabilities = InputCapability::MouseMove | InputCapability::MouseClick | 
                           InputCapability::KeyPress | InputCapability::ButtonMask;
        desc.isConnected = true;
        return desc;
    }

    [[nodiscard]] InputDeviceType GetType() const noexcept override { return InputDeviceType::Virtual; }
    [[nodiscard]] std::string_view GetName() const noexcept override { return "Virtual"; }
    [[nodiscard]] std::string_view GetIdentifier() const noexcept override { return "virtual"; }

    Result<void> Connect(const std::string&) noexcept override { return Ok(); }
    void Disconnect() noexcept override {}
    [[nodiscard]] bool IsConnected() const noexcept override { return true; }
    [[nodiscard]] Result<bool> Probe() const noexcept override { return Ok(true); }

    Result<void> Move(int x, int y) noexcept override { 
        lastX_ = x; lastY_ = y; 
        return Ok(); 
    }
    Result<void> MoveRelative(int dx, int dy) noexcept override { 
        lastX_ += dx; lastY_ += dy; 
        return Ok(); 
    }
    Result<void> SetLeft(bool down) noexcept override { 
        buttons_[0] = down; 
        return Ok(); 
    }
    Result<void> SetRight(bool down) noexcept override { 
        buttons_[1] = down; 
        return Ok(); 
    }
    Result<void> SetMiddle(bool down) noexcept override { 
        buttons_[2] = down; 
        return Ok(); 
    }
    Result<void> SetX1(bool down) noexcept override { 
        buttons_[3] = down; 
        return Ok(); 
    }
    Result<void> SetX2(bool down) noexcept override { 
        buttons_[4] = down; 
        return Ok(); 
    }

    [[nodiscard]] ButtonState GetButtonState(int virtualKey) const noexcept override { (void)virtualKey;
        ButtonState result;
        result.down = IsDown(virtualKey);
        result.source = InputDeviceType::Virtual;
        return result;
    }

    [[nodiscard]] bool IsDown(int virtualKey) const noexcept override { (void)virtualKey;
        // Map virtual keys to button indices
        switch (virtualKey) {
            case VK_LBUTTON: return buttons_[0];
            case VK_RBUTTON: return buttons_[1];
            case VK_MBUTTON: return buttons_[2];
            case VK_XBUTTON1: return buttons_[3];
            case VK_XBUTTON2: return buttons_[4];
            default: return false;
        }
    }

    [[nodiscard]] bool IsKeyJustPressed(int virtualKey) const noexcept override { (void)virtualKey; return false; }
    [[nodiscard]] bool IsKeyJustReleased(int virtualKey) const noexcept override { (void)virtualKey; return false; }

    [[nodiscard]] uint8_t GetButtonMask() const noexcept override {
        uint8_t mask = 0;
        if (buttons_[0]) mask |= 0x1;
        if (buttons_[1]) mask |= 0x2;
        if (buttons_[2]) mask |= 0x4;
        if (buttons_[3]) mask |= 0x8;
        if (buttons_[4]) mask |= 0x10;
        return mask;
    }

    [[nodiscard]] DevicePacket PollButtons() noexcept override {
        DevicePacket result;
        result.hasPacket = true;
        result.buttons = GetButtonMask();
        result.timestamp = std::chrono::steady_clock::now();
        return result;
    }

    [[nodiscard]] InputDiagnostics GetDiagnostics() const noexcept override {
        InputDiagnostics result;
        result.isConnected = true;
        result.deviceType = InputDeviceType::Virtual;
        result.identifier = "virtual";
        result.buttonMask = GetButtonMask();
        result.lastUpdate = std::chrono::steady_clock::now();
        return result;
    }

    [[nodiscard]] std::string GetConnectionStatus() const noexcept override { return "Virtual (Testing)"; }
    [[nodiscard]] uint64_t PacketsReceived() const noexcept override { return 0; }
    void ForceClearButtons() noexcept override { for (auto& b : buttons_) b = false; }
    void EnsureMonitoring() noexcept override {}

    [[nodiscard]] InputCapability GetCapabilities() const noexcept override {
        return InputCapability::MouseMove | InputCapability::MouseClick | 
               InputCapability::KeyPress | InputCapability::ButtonMask;
    }
    [[nodiscard]] bool HasCapability(InputCapability cap) const noexcept override {
        return OmniGhost::Platform::HasCapability(GetCapabilities(), cap);
    }

    // Test helpers
    void SetButton(int index, bool down) { if (index >= 0 && index < 5) buttons_[index] = down; }
    void SetPosition(int x, int y) { lastX_ = x; lastY_ = y; }

private:
    bool buttons_[5] = {false};
    int lastX_ = 0, lastY_ = 0;
};

} // namespace OmniGhost::Platform