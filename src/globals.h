#pragma once

#include <string>
#include <string_view>
#include <atomic>
#include <optional>
#include <mutex>

namespace OmniGhost {

enum class ActiveGame : int {
    FiveM = 0,
    CS2 = 1,
    Warzone = 3,
    Valorant = 4,
    Fortnite = 5,
    Apex = 6
};

class GameContext {
public:
    GameContext() = default;
    ~GameContext() = default;
    
    GameContext(const GameContext&) = delete;
    GameContext& operator=(const GameContext&) = delete;
    GameContext(GameContext&&) = default;
    GameContext& operator=(GameContext&&) = default;

    // Active game management
    [[nodiscard]] ActiveGame GetActiveGame() const noexcept {
        return activeGame_.load(std::memory_order_acquire);
    }
    
    void SetActiveGame(ActiveGame game) noexcept {
        activeGame_.store(game, std::memory_order_release);
    }
    
    // Valid executable (for FiveM)
    [[nodiscard]] std::string GetValidExecutable() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return validExecutable_;
    }
    
    void SetValidExecutable(std::string_view exe) {
        std::lock_guard<std::mutex> lock(mutex_);
        validExecutable_ = exe;
    }
    
    void ClearValidExecutable() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        validExecutable_.clear();
    }
    
    [[nodiscard]] bool HasValidExecutable() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return !validExecutable_.empty();
    }
    
    // Session state
    [[nodiscard]] bool IsInSession() const noexcept {
        return inSession_.load(std::memory_order_acquire);
    }
    
    void SetInSession(bool inSession) noexcept {
        inSession_.store(inSession, std::memory_order_release);
    }
    
    // Game-specific config access
    [[nodiscard]] std::string GetGamePrefix() const noexcept {
        return GetGamePrefix(GetActiveGame());
    }
    
    static std::string GetGamePrefix(ActiveGame game) noexcept {
        switch (game) {
            case ActiveGame::CS2: return "cs2_";
            case ActiveGame::Warzone: return "warzone_";
            case ActiveGame::Valorant: return "valorant_";
            default: return "fivem_";
        }
    }
    
    static std::string GetGameMeta(ActiveGame game) noexcept {
        switch (game) {
            case ActiveGame::CS2: return "cs2";
            case ActiveGame::Warzone: return "warzone";
            case ActiveGame::Valorant: return "valorant";
            default: return "fivem";
        }
    }
    
    static bool NameBelongsToActiveGame(const std::string& name, ActiveGame game) noexcept {
        const std::string prefix = GetGamePrefix(game);
        return name.rfind(prefix, 0) == 0;
    }
    
    // Singleton access
    static GameContext& Instance() noexcept {
        static GameContext instance;
        return instance;
    }

private:
    std::atomic<ActiveGame> activeGame_{ActiveGame::FiveM};
    std::atomic<bool> inSession_{false};
    std::string validExecutable_;
    mutable std::mutex mutex_;
};

} // namespace OmniGhost

// ---------------------------------------------------------------------------
// Legacy global shims — large parts of the codebase still use g_activeGame /
// g_validExecutable / bare ActiveGame. These proxies keep call sites compiling
// while the migration to GameContext continues.
// ---------------------------------------------------------------------------
using OmniGhost::ActiveGame;

struct LegacyActiveGameProxy {
    [[nodiscard]] operator ActiveGame() const noexcept {
        return OmniGhost::GameContext::Instance().GetActiveGame();
    }
    LegacyActiveGameProxy& operator=(ActiveGame game) noexcept {
        OmniGhost::GameContext::Instance().SetActiveGame(game);
        return *this;
    }
    [[nodiscard]] bool operator==(ActiveGame game) const noexcept {
        return OmniGhost::GameContext::Instance().GetActiveGame() == game;
    }
    [[nodiscard]] bool operator!=(ActiveGame game) const noexcept {
        return !(*this == game);
    }
};

struct LegacyValidExecutableProxy {
    [[nodiscard]] operator std::string() const {
        return OmniGhost::GameContext::Instance().GetValidExecutable();
    }
    LegacyValidExecutableProxy& operator=(std::string_view value) {
        OmniGhost::GameContext::Instance().SetValidExecutable(value);
        return *this;
    }
    LegacyValidExecutableProxy& operator=(const std::string& value) {
        OmniGhost::GameContext::Instance().SetValidExecutable(value);
        return *this;
    }
    LegacyValidExecutableProxy& operator=(const char* value) {
        OmniGhost::GameContext::Instance().SetValidExecutable(value ? value : "");
        return *this;
    }
    [[nodiscard]] bool empty() const noexcept {
        return !OmniGhost::GameContext::Instance().HasValidExecutable();
    }
    void clear() noexcept {
        OmniGhost::GameContext::Instance().ClearValidExecutable();
    }
};

inline LegacyActiveGameProxy g_activeGame{};
inline LegacyValidExecutableProxy g_validExecutable{};
