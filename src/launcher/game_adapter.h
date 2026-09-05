#pragma once

#include "launcher_data.h"
#include "../globals.h"

#include <cstdint>
#include <string_view>

namespace OmniGhost::Launcher {

enum class AdapterMaturity : std::uint8_t {
    Stable,
    Beta,
    Experimental,
    Unavailable,
};

enum class AdapterCapability : std::uint32_t {
    None = 0,
    Menu = 1u << 0,
    ReadOnlyMemory = 1u << 1,
    Overlay = 1u << 2,
    Radar = 1u << 3,
};

constexpr AdapterCapability operator|(AdapterCapability left, AdapterCapability right) noexcept {
    return static_cast<AdapterCapability>(static_cast<std::uint32_t>(left) |
                                          static_cast<std::uint32_t>(right));
}
constexpr bool HasCapability(AdapterCapability set, AdapterCapability value) noexcept {
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(value)) != 0;
}

enum class AdapterErrorCode : std::uint8_t {
    None,
    UnsupportedGame,
    AttachFailed,
    ValidationFailed,
    InitializationFailed,
    Cancelled,
};

struct AdapterError {
    AdapterErrorCode code = AdapterErrorCode::None;
    std::string_view detail{};
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return code != AdapterErrorCode::None;
    }
};

struct AdapterStartResult {
    bool succeeded = false;
    AdapterError error{};
};

struct AdapterDescriptor {
    ::Launcher::GameId id = ::Launcher::GameId::None;
    std::string_view displayName{};
    AdapterMaturity maturity = AdapterMaturity::Unavailable;
    AdapterCapability capabilities = AdapterCapability::None;
};

[[nodiscard]] std::string_view AdapterErrorMessage(AdapterErrorCode code) noexcept;

// Lifecycle contract shared by every game integration. UI code reads the
// descriptor rather than assuming every game offers the same capabilities.
class IGameAdapter {
public:
    virtual ~IGameAdapter() = default;

    // Bind to the already selected game/process. No rendering may occur here.
    virtual bool Attach() = 0;

    // Verify the adapter's currently loaded data and process binding.
    virtual bool Validate() = 0;

    // Complete game-specific startup after Attach and Validate succeeded.
    virtual bool Initialize() = 0;

    // Advance one non-blocking frame of the adapter while a session is active.
    virtual void Tick() = 0;

    // Release all resources owned by this adapter. It must be idempotent.
    virtual void Shutdown() noexcept = 0;

    // Short, user-safe status text. It must not expose offsets or credentials.
    [[nodiscard]] virtual std::string_view GetStatus() const noexcept = 0;
    [[nodiscard]] virtual AdapterDescriptor GetDescriptor() const noexcept = 0;
    [[nodiscard]] virtual AdapterError GetLastError() const noexcept = 0;

    // Check if the game process is still running. Return false when the process
    // has terminated or become unreachable. The launcher uses this to decide
    // when to end the session and return to the launcher.
    virtual bool IsGameProcessAlive() const = 0;

    // Validate live offsets against the running game process. Returns true if
    // critical offsets are still valid, false if they need refresh.
    virtual bool ValidateLiveOffsets() = 0;

    // Return a user-facing termination reason when the game process has ended.
    // Called when IsGameProcessAlive() returns false.
    [[nodiscard]] virtual std::string_view GetTerminationReason() const noexcept = 0;

    // Return the ActiveGame enum value for this adapter. Used for telemetry.
    virtual ActiveGame GetGameId() const noexcept = 0;
};

} // namespace OmniGhost::Launcher
