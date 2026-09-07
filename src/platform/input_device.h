#pragma once

#include "interfaces.h"
#include "result.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Input Device Types & Capabilities
// ============================================================
enum class InputDeviceType : std::uint8_t {
    None = 0,
    Makcu,
    KMBoxNet,
    Ferrum,
    Virtual,      // For testing without hardware
    LocalWindows  // Fallback for menu PC
};

enum class InputCapability : std::uint32_t {
    None = 0,
    MouseMove = 1u << 0,
    MouseClick = 1u << 1,
    MouseScroll = 1u << 2,
    KeyPress = 1u << 3,
    ButtonMask = 1u << 4,      // Raw button state from device
    Diagnostics = 1u << 5,     // Device diagnostics support
    ForceFeedback = 1u << 6,   // Haptic/rumble support
};

constexpr InputCapability operator|(InputCapability left, InputCapability right) noexcept {
    return static_cast<InputCapability>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
constexpr bool HasCapability(InputCapability set, InputCapability value) noexcept {
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(value)) != 0;
}

struct InputDeviceDescriptor {
    InputDeviceType type = InputDeviceType::None;
    std::string name;
    std::string identifier;  // Port, IP, UUID, etc.
    InputCapability capabilities = InputCapability::None;
    bool isConnected = false;
    std::string firmwareVersion;
    std::string hardwareVersion;
};

struct ButtonState {
    bool down = false;
    InputDeviceType source = InputDeviceType::None;
    uint8_t deviceMask = 0;
    std::chrono::steady_clock::time_point lastChange;
};

struct InputDiagnostics {
    bool isConnected = false;
    bool isMonitoring = false;
    InputDeviceType deviceType = InputDeviceType::None;
    std::string identifier;
    uint8_t buttonMask = 0;
    uint64_t lastPacketMs = 0;
    uint64_t packetsReceived = 0;
    uint64_t parseErrors = 0;
    uint64_t sendErrors = 0;
    std::string deviceInfo;
    std::string faultInfo;
    std::string lastSource;
    std::chrono::steady_clock::time_point lastUpdate;
};

struct DevicePacket {
    bool hasPacket = false;
    uint8_t buttons = 0;
    std::chrono::steady_clock::time_point timestamp;
};

// ============================================================
// IInputDevice - Unified abstraction for all input devices
// ============================================================
class IInputDevice {
public:
    virtual ~IInputDevice() = default;

    // Device identification
    [[nodiscard]] virtual InputDeviceDescriptor GetDescriptor() const noexcept = 0;
    [[nodiscard]] virtual InputDeviceType GetType() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetIdentifier() const noexcept = 0;

    // Connection management
    virtual Result<void> Connect(const std::string& identifier) noexcept = 0;
    virtual void Disconnect() noexcept = 0;
    [[nodiscard]] virtual bool IsConnected() const noexcept = 0;
    [[nodiscard]] virtual Result<bool> Probe() const noexcept = 0;

    // Mouse movement
    virtual Result<void> Move(int x, int y) noexcept = 0;
    virtual Result<void> MoveRelative(int dx, int dy) noexcept = 0;

    // Mouse buttons
    virtual Result<void> SetLeft(bool down) noexcept = 0;
    virtual Result<void> SetRight(bool down) noexcept = 0;
    virtual Result<void> SetMiddle(bool down) noexcept = 0;
    virtual Result<void> SetX1(bool down) noexcept = 0;
    virtual Result<void> SetX2(bool down) noexcept = 0;

    // Button state queries
    [[nodiscard]] virtual ButtonState GetButtonState(int virtualKey) const noexcept = 0;
    [[nodiscard]] virtual bool IsDown(int virtualKey) const noexcept = 0;
    [[nodiscard]] virtual bool IsKeyJustPressed(int virtualKey) const noexcept = 0;
    [[nodiscard]] virtual bool IsKeyJustReleased(int virtualKey) const noexcept = 0;

    // Raw button mask from device
    [[nodiscard]] virtual uint8_t GetButtonMask() const noexcept = 0;
    [[nodiscard]] virtual DevicePacket PollButtons() noexcept = 0;

    // Diagnostics
    [[nodiscard]] virtual InputDiagnostics GetDiagnostics() const noexcept = 0;
    [[nodiscard]] virtual std::string GetConnectionStatus() const noexcept = 0;
    [[nodiscard]] virtual uint64_t PacketsReceived() const noexcept = 0;

    // Advanced
    virtual void ForceClearButtons() noexcept = 0;
    virtual void EnsureMonitoring() noexcept = 0;

    // Capabilities
    [[nodiscard]] virtual InputCapability GetCapabilities() const noexcept = 0;
    [[nodiscard]] virtual bool HasCapability(InputCapability cap) const noexcept = 0;
};

// ============================================================
// Input Manager - Multi-device coordination
// ============================================================
class IInputManager {
public:
    virtual ~IInputManager() = default;

    // Device management
    virtual Result<void> RegisterDevice(std::unique_ptr<IInputDevice> device) noexcept = 0;
    virtual void UnregisterDevice(InputDeviceType type) noexcept = 0;
    [[nodiscard]] virtual IInputDevice* GetDevice(InputDeviceType type) noexcept = 0;
    [[nodiscard]] virtual const IInputDevice* GetDevice(InputDeviceType type) const noexcept = 0;

    // Active device (primary for aim/ESP)
    virtual Result<void> SetActiveDevice(InputDeviceType type) noexcept = 0;
    [[nodiscard]] virtual InputDeviceType GetActiveDeviceType() const noexcept = 0;
    [[nodiscard]] virtual IInputDevice* GetActiveDevice() noexcept = 0;
    [[nodiscard]] virtual const IInputDevice* GetActiveDevice() const noexcept = 0;

    // Fallback chain (tried in order when primary fails)
    virtual void SetFallbackChain(std::vector<InputDeviceType> chain) noexcept = 0;
    [[nodiscard]] virtual const std::vector<InputDeviceType>& GetFallbackChain() const noexcept = 0;

    // Unified input queries (uses active device + fallbacks)
    [[nodiscard]] virtual ButtonState GetButtonState(int virtualKey) const noexcept = 0;
    [[nodiscard]] virtual bool IsDown(int virtualKey) const noexcept = 0;
    [[nodiscard]] virtual bool IsKeyJustPressed(int virtualKey) const noexcept = 0;
    [[nodiscard]] virtual bool IsKeyJustReleased(int virtualKey) const noexcept = 0;
    [[nodiscard]] virtual uint8_t GetButtonMask() const noexcept = 0;
    [[nodiscard]] virtual DevicePacket PollButtons() noexcept = 0;

    // Mouse movement (uses active device)
    virtual Result<void> Move(int x, int y) noexcept = 0;
    virtual Result<void> MoveRelative(int dx, int dy) noexcept = 0;
    virtual Result<void> SetLeft(bool down) noexcept = 0;
    virtual Result<void> SetRight(bool down) noexcept = 0;

    // Diagnostics
    [[nodiscard]] virtual std::vector<InputDiagnostics> GetAllDiagnostics() const noexcept = 0;

    // Allow local Windows input as last resort (for menu PC)
    virtual void SetLocalFallbackAllowed(bool allowed) noexcept = 0;
    [[nodiscard]] virtual bool IsLocalFallbackAllowed() const noexcept = 0;
};

// CreateInputManager declared here; defined in input_device.cpp
[[nodiscard]] std::unique_ptr<IInputManager> CreateInputManager();


} // namespace OmniGhost::Platform
