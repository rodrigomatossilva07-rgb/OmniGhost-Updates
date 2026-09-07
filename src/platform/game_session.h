#pragma once

#include "../globals.h"
#include "result.h"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace OmniGhost::Platform {

class ServiceContainer;

// ============================================================
// Session Lifecycle Events
// ============================================================
enum class SessionState : std::uint8_t {
    Detached,
    Attaching,
    Attached,
    LoadingOffsets,
    OffsetsLoaded,
    Initializing,
    Running,
    SoftProbing,
    Terminating,
    Cleanup,
    Failed
};

struct SessionEvent {
    enum class Type : std::uint8_t {
        StateChanged,
        GameProcessLost,
        OffsetsRefreshNeeded,
        OffsetsRefreshed,
        HardwareDisconnected,
        HardwareReconnected,
        Error,
        UserRequestedExit
    } type;

    SessionState oldState = SessionState::Detached;
    SessionState newState = SessionState::Detached;
    std::string message;
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
};

// ============================================================
// IGameSession - Standardized session lifecycle
// ============================================================
class IGameSession {
public:
    virtual ~IGameSession() = default;

    // Attach to game process (opens DMA, binds to PID)
    virtual Result<void> Attach() noexcept = 0;

    // Load/refresh offsets for the game
    virtual Result<void> LoadOffsets() noexcept = 0;

    // Initialize game-specific features (ESP, aim, radar)
    virtual Result<void> Initialize() noexcept = 0;

    // Run one frame tick (called repeatedly during session)
    virtual void Tick() noexcept = 0;

    // Soft probe live offsets (non-blocking)
    virtual Result<bool> SoftProbeOffsets() noexcept = 0;

    // Validate live offsets against running process
    virtual Result<bool> ValidateLiveOffsets() noexcept = 0;

    // Check if game process is still alive
    virtual Result<bool> IsGameProcessAlive() const noexcept = 0;

    // Get user-facing status text
    [[nodiscard]] virtual std::string_view GetStatus() const noexcept = 0;

    // Get current session state
    [[nodiscard]] virtual SessionState GetState() const noexcept = 0;

    // Get termination reason when process ends
    [[nodiscard]] virtual std::string_view GetTerminationReason() const noexcept = 0;

    // Clean shutdown (idempotent)
    virtual void Shutdown() noexcept = 0;

    // Set state change callback
    virtual void SetStateCallback(std::function<void(const SessionEvent&)> callback) noexcept = 0;
};

// ============================================================
// IGameAdapter - Extended with DI support
// ============================================================
class IGameAdapter {
public:
    virtual ~IGameAdapter() = default;

    // Factory method to create a new session
    virtual std::unique_ptr<IGameSession> CreateSession(const ServiceContainer& services) const = 0;

    // Game metadata
    [[nodiscard]] virtual std::string_view GetGameName() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetGameId() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t GetCapabilities() const noexcept = 0;
    [[nodiscard]] virtual bool IsExperimental() const noexcept = 0;
};

// ============================================================
// Session Factory for DI integration
// ============================================================
class IGameSessionFactory {
public:
    virtual ~IGameSessionFactory() = default;
    virtual std::unique_ptr<IGameSession> CreateSession(::ActiveGame game, const ServiceContainer& services) const = 0;
    virtual bool SupportsGame(::ActiveGame game) const noexcept = 0;
};

} // namespace OmniGhost::Platform