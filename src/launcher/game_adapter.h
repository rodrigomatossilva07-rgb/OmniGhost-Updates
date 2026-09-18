#pragma once

#include "launcher_data.h"
#include "../globals.h"

#include <cstdint>
#include <string_view>
#include <unordered_set>

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

    // Switch DMA context to this game's process without full device reset.
    // Called when another game was already attached. Default: full reattach.
    virtual bool Rebind() { return Attach(); }
};

// Forward declaration for adapter registry
IGameAdapter* FindGameAdapter(::Launcher::GameId game) noexcept;

// Multi-game session manager - tracks multiple attached games and handles switching
class GameSessionManager {
public:
    static GameSessionManager& Instance() noexcept {
        static GameSessionManager instance;
        return instance;
    }

    // Get or create adapter for a game
    IGameAdapter* GetAdapter(::Launcher::GameId game) noexcept {
        return FindGameAdapter(game);
    }

    // Start a game session - attaches or rebinds as needed
    AdapterStartResult StartSession(::Launcher::GameId game) {
        IGameAdapter* adapter = GetAdapter(game);
        if (!adapter)
            return { false, { AdapterErrorCode::UnsupportedGame, {} } };

        // If this game is already attached and active, nothing to do
        if (attachedGames_.count(game) && activeGame_ == game) {
            return { true, {} };
        }

        // If another game is active, rebind to this game
        if (activeGame_ != ::Launcher::GameId::None && activeGame_ != game) {
            if (adapter->Rebind()) {
                attachedGames_.insert(game);
                activeGame_ = game;
                UpdateGameContext();
                return { true, {} };
            }
            // Rebind failed, fall through to full attach
        }

        // Fresh attach
        if (!adapter->Attach())
            return { false, adapter->GetLastError() };
        if (!adapter->Validate())
            return { false, adapter->GetLastError() };
        if (!adapter->Initialize())
            return { false, adapter->GetLastError() };

        attachedGames_.insert(game);
        activeGame_ = game;
        UpdateGameContext();
        return { true, {} };
    }

    // Switch to an already attached game (just changes active, no DMA ops)
    bool SwitchTo(::Launcher::GameId game) {
        if (!attachedGames_.count(game)) return false;
        activeGame_ = game;
        UpdateGameContext();
        return true;
    }

    // End session for a game
    void EndSession(::Launcher::GameId game) {
        IGameAdapter* adapter = GetAdapter(game);
        if (adapter) {
            adapter->Shutdown();
        }
        attachedGames_.erase(game);
        if (activeGame_ == game) {
            activeGame_ = ::Launcher::GameId::None;
            // Auto-switch to another attached game if available
            if (!attachedGames_.empty()) {
                activeGame_ = *attachedGames_.begin();
                UpdateGameContext();
            }
        }
    }

    // Get currently active game
    ::Launcher::GameId GetActiveGame() const noexcept { return activeGame_; }

    // Check if a game is attached
    bool IsAttached(::Launcher::GameId game) const noexcept { return attachedGames_.count(game); }

    // Get all attached games
    const std::unordered_set<::Launcher::GameId>& GetAttachedGames() const noexcept { return attachedGames_; }

    // Called every frame to tick the active adapter
    void TickActive() {
        if (activeGame_ != ::Launcher::GameId::None) {
            IGameAdapter* adapter = GetAdapter(activeGame_);
            if (adapter && adapter->IsGameProcessAlive()) {
                adapter->Tick();
            } else if (adapter) {
                // Game process died, clean up
                EndSession(activeGame_);
            }
        }
    }

private:
    GameSessionManager() = default;
    std::unordered_set<::Launcher::GameId> attachedGames_;
    ::Launcher::GameId activeGame_ = ::Launcher::GameId::None;

    void UpdateGameContext() {
        // Sync with legacy GameContext for backward compatibility
        OmniGhost::GameContext::Instance().SetActiveGame(
            static_cast<OmniGhost::ActiveGame>(activeGame_));
    }
};

} // namespace OmniGhost::Launcher