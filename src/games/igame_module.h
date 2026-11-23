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







class IGameModule {
public:
    virtual ~IGameModule() = default;


    [[nodiscard]] virtual std::string_view GetGameId() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetGameName() const noexcept = 0;
    [[nodiscard]] virtual ::Launcher::GameId GetLauncherGameId() const noexcept = 0;


    virtual Expected<void> Initialize(OmniGhost::Platform::ServiceContainer& container) = 0;
    virtual Expected<void> Shutdown() = 0;
    virtual Expected<void> Update(float deltaTime) = 0;


    [[nodiscard]] virtual bool IsGameRunning() const noexcept = 0;
    [[nodiscard]] virtual bool IsAttached() const noexcept = 0;
    [[nodiscard]] virtual bool IsInGame() const noexcept = 0;


    virtual Expected<void> LoadOffsets() = 0;
    virtual Expected<void> ReloadOffsets() = 0;
    [[nodiscard]] virtual bool HasValidOffsets() const noexcept = 0;


    virtual Expected<void> EnableFeatures() = 0;
    virtual Expected<void> DisableFeatures() = 0;


    virtual void DrawSettings() = 0;
    virtual void DrawDebugInfo() = 0;


    virtual Expected<void> SaveConfig() = 0;
    virtual Expected<void> LoadConfig() = 0;


    [[nodiscard]] virtual std::string GetDiagnosticsInfo() const = 0;
};





class IGameModuleFactory {
public:
    virtual ~IGameModuleFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<IGameModule> Create(
        OmniGhost::Platform::ServiceContainer& container) = 0;
    [[nodiscard]] virtual std::string_view GetGameId() const noexcept = 0;
    [[nodiscard]] virtual ::Launcher::GameId GetLauncherGameId() const noexcept = 0;
};





class GameModuleRegistry {
public:
    static GameModuleRegistry& Instance() noexcept {
        static GameModuleRegistry instance;
        return instance;
    }


    void RegisterFactory(std::unique_ptr<IGameModuleFactory> factory);


    [[nodiscard]] IGameModuleFactory* GetFactory(std::string_view gameId) const noexcept;
    [[nodiscard]] IGameModuleFactory* GetFactory(::Launcher::GameId launcherGameId) const noexcept;


    [[nodiscard]] Expected<std::unique_ptr<IGameModule>> CreateModule(
        std::string_view gameId,
        OmniGhost::Platform::ServiceContainer& container);


    [[nodiscard]] std::vector<std::string_view> GetRegisteredGameIds() const noexcept;


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

}
