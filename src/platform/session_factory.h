#pragma once

#include "../globals.h"
#include "game_session.h"
#include "cs2_session.h"
#include "fivem_session.h"
#include "valorant_session.h"
#include "fortnite_session.h"
#include "warzone_session.h"
#include "service_container.h"
#include <memory>
#include <functional>
#include <unordered_map>

namespace OmniGhost::Platform {

class GameSessionFactory final : public IGameSessionFactory {
public:
    explicit GameSessionFactory(const ServiceContainer& services) : services_(services) {}

    std::unique_ptr<IGameSession> CreateSession(ActiveGame game, const ServiceContainer& services) const override {
        switch (game) {
            case ActiveGame::CS2:
                return std::make_unique<Cs2Session>(services);
            case ActiveGame::FiveM:
                return std::make_unique<FivemSession>(services);
            case ActiveGame::Valorant:
                return std::make_unique<ValorantSession>(services);
            case ActiveGame::Fortnite:
                return std::make_unique<FortniteSession>(services);
            case ActiveGame::Warzone:
                return std::make_unique<WarzoneSession>(services);
            default:
                return nullptr;
        }
    }

    bool SupportsGame(ActiveGame game) const noexcept override {
        switch (game) {
            case ActiveGame::CS2:
            case ActiveGame::FiveM:
            case ActiveGame::Valorant:
            case ActiveGame::Fortnite:
            case ActiveGame::Warzone:
                return true;
            default:
                return false;
        }
    }

private:
    const ServiceContainer& services_;
};

// SessionManager registration lives with scoped_session once that type is finalized.

// Forward - SessionManager is defined in scoped_session.h
inline void RegisterAllGameSessions(class SessionManager& manager, const ServiceContainer& services) {
    (void)manager;
    (void)services;
}

} // namespace OmniGhost::Platform
