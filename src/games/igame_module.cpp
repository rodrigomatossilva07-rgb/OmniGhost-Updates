#include "igame_module.h"

#include "platform/expected.h"
#include "platform/logging.h"

#include <algorithm>

namespace OmniGhost::Games {

// =============================================================================
// GameModuleRegistry Implementation
// =============================================================================

void GameModuleRegistry::RegisterFactory(std::unique_ptr<IGameModuleFactory> factory) {
    std::unique_lock lock(mutex_);

    const std::string gameId{factory->GetGameId()};
    factories_[gameId] = std::move(factory);

    // Update launcher ID map
    launcherIdMap_[factory->GetLauncherGameId()] = factories_[gameId].get();

    OGS_LOG_INFO("[GameModuleRegistry] Registered game module: {}", gameId);
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

} // namespace OmniGhost::Games
