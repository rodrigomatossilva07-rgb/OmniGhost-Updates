#include "igame_module.h"

#include "platform/expected.h"
#include "platform/logging.h"

#include <algorithm>

namespace OmniGhost::Games {





void GameModuleRegistry::RegisterFactory(std::unique_ptr<IGameModuleFactory> factory) {
    if (!factory)
        return;
    std::unique_lock lock(mutex_);

    const std::string gameId{factory->GetGameId()};
    const auto launcherGameId = factory->GetLauncherGameId();
    factories_[gameId] = std::move(factory);

    // factory was moved into factories_ above, so use the identifier captured
    // while it was still valid. Dereferencing it here caused a startup access
    // violation from the static game-module registrar.
    launcherIdMap_[launcherGameId] = factories_[gameId].get();

    // Registration is performed by a static registrar before wWinMain.  The
    // logging subsystem is not initialized at that point, so logging here can
    // throw std::system_error and abort the process during CRT startup.
}

IGameModuleFactory* GameModuleRegistry::GetFactory(std::string_view gameId) const noexcept {
    std::shared_lock lock(mutex_);

    auto it = factories_.find(std::string{gameId});
    if (it != factories_.end()) {
        return it->second.get();
    }
    return nullptr;
}

IGameModuleFactory* GameModuleRegistry::GetFactory(Launcher::GameId launcherGameId) const noexcept {
    std::shared_lock lock(mutex_);

    auto it = launcherIdMap_.find(launcherGameId);
    if (it != launcherIdMap_.end()) {
        return it->second;
    }
    return nullptr;
}

Expected<std::unique_ptr<IGameModule>> GameModuleRegistry::CreateModule(
    std::string_view gameId,
    OmniGhost::Platform::ServiceContainer& container) {

    std::shared_lock lock(mutex_);

    auto it = factories_.find(std::string{gameId});
    if (it == factories_.end()) {
        return OmniGhost::Failure<std::unique_ptr<IGameModule>>(
            OmniGhost::ErrorCode::NotFound,
            "Game module not registered: " + std::string(gameId));
    }

    try {
        auto module = it->second->Create(container);
        return OmniGhost::Success(std::move(module));
    } catch (const std::exception& ex) {
        return OmniGhost::Failure<std::unique_ptr<IGameModule>>(
            OmniGhost::ErrorCode::InternalError,
            "Failed to create game module: " + std::string(ex.what()));
    }
}

std::vector<std::string_view> GameModuleRegistry::GetRegisteredGameIds() const noexcept {
    std::shared_lock lock(mutex_);

    std::vector<std::string_view> ids;
    ids.reserve(factories_.size());
    for (const auto& [id, _] : factories_) {
        ids.push_back(id);
    }
    return ids;
}

bool GameModuleRegistry::IsRegistered(std::string_view gameId) const noexcept {
    std::shared_lock lock(mutex_);
    return factories_.contains(std::string{gameId});
}

}
