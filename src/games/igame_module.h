#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

#include "platform/service_container.h"
#include "platform/expected.h"
#include "launcher/launcher_data.h"

namespace OmniGhost::Games {

// =============================================================================
// IGameModule - Interface for game-specific modules
// =============================================================================
// Each game (CS2, Rust, FiveM, etc.) implements this interface
// to provide its specific functionality through dependency injection.

class IGameModule {
public:
    virtual ~IGameModule() = default;

    // Game identification
    [[nodiscard]] virtual std::string_view GetGameId() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetGameName() const noexcept = 0;
    [[nodiscard]] virtual ::Launcher::GameId GetLauncherGameId() const noexcept = 0;

    // Lifecycle
    virtual Expected<void> Initialize(OmniGhost::Platform::ServiceContainer& container) = 0;
    virtual Expected<void> Shutdown() = 0;
    virtual Expected<void> Update(float deltaTime) = 0;

    // Game state
    [[nodiscard]] virtual bool IsGameRunning() const noexcept = 0;
    [[nodiscard]] virtual bool IsAttached() const noexcept = 0;
    [[nodiscard]] virtual bool IsInGame() const noexcept = 0;

    // Offsets
    virtual Expected<void> LoadOffsets() = 0;
    virtual Expected<void> ReloadOffsets() = 0;
    [[nodiscard]] virtual bool HasValidOffsets() const noexcept = 0;

    // Features
    virtual Expected<void> EnableFeatures() = 0;
    virtual Expected<void> DisableFeatures() = 0;

    // UI/Config
    virtual void DrawSettings() = 0;
    virtual void DrawDebugInfo() = 0;

    // Serialization
    virtual Expected<void> SaveConfig() = 0;
    virtual Expected<void> LoadConfig() = 0;

    // Diagnostics
    [[nodiscard]] virtual std::string GetDiagnosticsInfo() const = 0;
};

// =============================================================================
// IGameModuleFactory - Factory for creating game modules
// =============================================================================

class IGameModuleFactory {
public:
    virtual ~IGameModuleFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<IGameModule> Create(
        OmniGhost::Platform::ServiceContainer& container) = 0;
    [[nodiscard]] virtual std::string_view GetGameId() const noexcept = 0;
    [[nodiscard]] virtual ::Launcher::GameId GetLauncherGameId() const noexcept = 0;
};

// =============================================================================
// GameModuleRegistry - Registry for game modules
// =============================================================================

class GameModuleRegistry {
public:
    static GameModuleRegistry& Instance() noexcept {
        static GameModuleRegistry instance;
        return instance;
    }

    // Register a game module factory
    void RegisterFactory(std::unique_ptr<IGameModuleFactory> factory);

    // Get factory by game ID
    [[nodiscard]] IGameModuleFactory* GetFactory(std::string_view gameId) const noexcept;
    [[nodiscard]] IGameModuleFactory* GetFactory(::Launcher::GameId launcherGameId) const noexcept;

    // Create module instance
    [[nodiscard]] Expected<std::unique_ptr<IGameModule>> CreateModule(
        std::string_view gameId,
        OmniGhost::Platform::ServiceContainer& container);

    // Get all registered game IDs
    [[nodiscard]] std::vector<std::string_view> GetRegisteredGameIds() const noexcept;

    // Check if game is registered
    [[nodiscard]] bool IsRegistered(std::string_view gameId) const noexcept;

private:
    GameModuleRegistry() = default;
    ~GameModuleRegistry() = default;
    GameModuleRegistry(const GameModuleRegistry&) = delete;
    GameModuleRegistry& operator=(const GameModuleRegistry&) = delete;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<IGameModuleFactory>> factories_;
    std::unordered_map<::Launcher::GameId, IGameModuleFactory*> launcherIdMap_;
};

// =============================================================================
// Registration Macro
// =============================================================================

#define OGS_REGISTER_GAME_MODULE(GameModuleClass) \
    namespace { \
        struct GameModuleClass##_Registrar { \
            GameModuleClass##_Registrar() { \
                OmniGhost::Games::GameModuleRegistry::Instance().RegisterFactory( \
                    std::make_unique<OmniGhost::Games::GameModuleFactoryImpl<GameModuleClass>>() \
                ); \
            } \
        }; \
        static GameModuleClass##_Registrar g_##GameModuleClass##_registrar; \
    }

// Template factory implementation
template <typename T>
class GameModuleFactoryImpl : public OmniGhost::Games::IGameModuleFactory {
public:
    [[nodiscard]] std::unique_ptr<OmniGhost::Games::IGameModule> Create(
        OmniGhost::Platform::ServiceContainer& container) override {
        return std::make_unique<T>(container);
    }

    [[nodiscard]] std::string_view GetGameId() const noexcept override {
        return T::StaticGameId();
    }

    [[nodiscard]] ::Launcher::GameId GetLauncherGameId() const noexcept override {
        return T::StaticLauncherGameId();
    }
};

// Base class for game modules (optional helper)
class GameModuleBase : public IGameModule {
public:
    explicit GameModuleBase(OmniGhost::Platform::ServiceContainer& container)
        : container_(container) {}

protected:
    OmniGhost::Platform::ServiceContainer& container_;

    template <typename T>
    T& GetService() {
        return container_.Get<T>();
    }

    template <typename T>
    auto TryGetService() -> OmniGhost::Expected<T*> {
        auto ptr = container_.TryGet<T>();
        if (ptr) return ptr.get();
        return OmniGhost::Failure<T*>(OmniGhost::ErrorCode::NotFound, "Service not found");
    }
};

} // namespace OmniGhost::Games
