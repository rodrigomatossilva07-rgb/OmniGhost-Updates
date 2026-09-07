#include "input_device.h"
#include "event_bus.h"
#include "feature_flags.h"
#include <algorithm>
#include <chrono>

namespace OmniGhost::Platform {

// ============================================================
// InputManager Implementation
// ============================================================
class InputManagerImpl final : public IInputManager {
public:
    Result<void> RegisterDevice(std::unique_ptr<IInputDevice> device) noexcept override {
        if (!device) return Err<void>("Null device");
        std::unique_lock lock(mutex_);
        auto type = device->GetType();
        if (devices_.contains(type)) {
            return Err<void>("Device type already registered");
        }
        devices_[type] = std::move(device);
        
        // Auto-set as active if none
        if (activeDevice_ == InputDeviceType::None) {
            activeDevice_ = devices_.begin()->first;
        }
        
        GetEventBus().Publish(InputDeviceChangedEvent{
            std::to_string(static_cast<int>(type)), true, std::chrono::steady_clock::now()
        });
        return Ok();
    }

    void UnregisterDevice(InputDeviceType type) noexcept override {
        std::unique_lock lock(mutex_);
        if (devices_.erase(type)) {
            if (activeDevice_ == type) {
                activeDevice_ = InputDeviceType::None;
                // Pick next available
                if (!devices_.empty()) {
                    activeDevice_ = devices_.begin()->first;
                }
            }
            if (!fallbackChain_.empty()) {
                fallbackChain_.erase(
                    std::remove(fallbackChain_.begin(), fallbackChain_.end(), type),
                    fallbackChain_.end()
                );
            }
        }
    }

    [[nodiscard]] IInputDevice* GetDevice(InputDeviceType type) noexcept override {
        std::shared_lock lock(mutex_);
        auto it = devices_.find(type);
        return it != devices_.end() ? it->second.get() : nullptr;
    }

    [[nodiscard]] const IInputDevice* GetDevice(InputDeviceType type) const noexcept override {
        std::shared_lock lock(mutex_);
        auto it = devices_.find(type);
        return it != devices_.end() ? it->second.get() : nullptr;
    }

    Result<void> SetActiveDevice(InputDeviceType type) noexcept override {
        std::unique_lock lock(mutex_);
        if (!devices_.contains(type)) {
            return Err<void>("Device not registered");
        }
        activeDevice_ = type;
        return Ok();
    }

    [[nodiscard]] InputDeviceType GetActiveDeviceType() const noexcept override {
        std::shared_lock lock(mutex_);
        return activeDevice_;
    }

    [[nodiscard]] IInputDevice* GetActiveDevice() noexcept override {
        std::shared_lock lock(mutex_);
        auto it = devices_.find(activeDevice_);
        return it != devices_.end() ? it->second.get() : nullptr;
    }

    [[nodiscard]] const IInputDevice* GetActiveDevice() const noexcept override {
        std::shared_lock lock(mutex_);
        auto it = devices_.find(activeDevice_);
        return it != devices_.end() ? it->second.get() : nullptr;
    }

    void SetFallbackChain(std::vector<InputDeviceType> chain) noexcept override {
        std::unique_lock lock(mutex_);
        fallbackChain_ = std::move(chain);
    }

    [[nodiscard]] const std::vector<InputDeviceType>& GetFallbackChain() const noexcept override {
        return fallbackChain_;
    }

    // Unified queries (try active, then fallback chain)
    [[nodiscard]] ButtonState GetButtonState(int virtualKey) const noexcept override {
        auto dev = GetActiveDevice();
        if (dev) return dev->GetButtonState(virtualKey);
        
        // Try fallback chain
        std::shared_lock lock(mutex_);
        for (auto type : fallbackChain_) {
            auto it = devices_.find(type);
            if (it != devices_.end()) {
                return it->second->GetButtonState(virtualKey);
            }
        }
        return {};
    }

    [[nodiscard]] bool IsDown(int virtualKey) const noexcept override {
        return GetButtonState(virtualKey).down;
    }

    [[nodiscard]] bool IsKeyJustPressed(int virtualKey) const noexcept override {
        auto dev = GetActiveDevice();
        if (dev) return dev->IsKeyJustPressed(virtualKey);
        return false;
    }

    [[nodiscard]] bool IsKeyJustReleased(int virtualKey) const noexcept override {
        auto dev = GetActiveDevice();
        if (dev) return dev->IsKeyJustReleased(virtualKey);
        return false;
    }

    [[nodiscard]] uint8_t GetButtonMask() const noexcept override {
        auto dev = GetActiveDevice();
        return dev ? dev->GetButtonMask() : 0;
    }

    [[nodiscard]] DevicePacket PollButtons() noexcept override {
        auto dev = GetActiveDevice();
        if (dev) return dev->PollButtons();
        return {};
    }

    Result<void> Move(int x, int y) noexcept override {
        auto dev = GetActiveDevice();
        return dev ? dev->Move(x, y) : Err<void>("No active device");
    }

    Result<void> MoveRelative(int dx, int dy) noexcept override {
        auto dev = GetActiveDevice();
        return dev ? dev->MoveRelative(dx, dy) : Err<void>("No active device");
    }

    Result<void> SetLeft(bool down) noexcept override {
        auto dev = GetActiveDevice();
        return dev ? dev->SetLeft(down) : Err<void>("No active device");
    }

    Result<void> SetRight(bool down) noexcept override {
        auto dev = GetActiveDevice();
        return dev ? dev->SetRight(down) : Err<void>("No active device");
    }

    [[nodiscard]] std::vector<InputDiagnostics> GetAllDiagnostics() const noexcept override {
        std::shared_lock lock(mutex_);
        std::vector<InputDiagnostics> result;
        result.reserve(devices_.size());
        for (const auto& [_, dev] : devices_) {
            result.push_back(dev->GetDiagnostics());
        }
        return result;
    }

    void SetLocalFallbackAllowed(bool allowed) noexcept override {
        localFallbackAllowed_ = allowed;
    }

    [[nodiscard]] bool IsLocalFallbackAllowed() const noexcept override {
        return localFallbackAllowed_;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<InputDeviceType, std::unique_ptr<IInputDevice>> devices_;
    InputDeviceType activeDevice_ = InputDeviceType::None;
    std::vector<InputDeviceType> fallbackChain_;
    bool localFallbackAllowed_ = false;
};

// Factory function
std::unique_ptr<IInputManager> CreateInputManager() {
    return std::make_unique<InputManagerImpl>();
}

} // namespace OmniGhost::Platform