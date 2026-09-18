#include "game_adapter.h"
#include "game_adapter_registry.h"
#include "game_launch_service.h"
#include "../globals.h"
#include <unordered_set>

namespace OmniGhost::Launcher {

static std::unordered_set<::Launcher::GameId> s_attachedGames;
static ::Launcher::GameId s_activeGame = ::Launcher::GameId::None;

static void UpdateGameContext() {
    OmniGhost::GameContext::Instance().SetActiveGame(
        static_cast<OmniGhost::ActiveGame>(s_activeGame));
}

std::string_view AdapterErrorMessage(AdapterErrorCode code) noexcept {
    switch (code) {
        case AdapterErrorCode::UnsupportedGame: return "Jogo não suportado";
        case AdapterErrorCode::AttachFailed: return "Falha ao anexar ao processo";
        case AdapterErrorCode::ValidationFailed: return "Validação falhou";
        case AdapterErrorCode::InitializationFailed: return "Inicialização falhou";
        case AdapterErrorCode::Cancelled: return "Cancelado";
        default: return "Erro desconhecido";
    }
}

IGameAdapter* GetAdapter(::Launcher::GameId game) noexcept {
    return FindGameAdapter(game);
}

AdapterStartResult StartSession(::Launcher::GameId game) {
    IGameAdapter* adapter = GetAdapter(game);
    if (!adapter)
        return { false, { AdapterErrorCode::UnsupportedGame, {} } };

    if (s_attachedGames.count(game) && s_activeGame == game) {
        return { true, {} };
    }

    if (s_activeGame != ::Launcher::GameId::None && s_activeGame != game) {
        if (adapter->Rebind()) {
            s_attachedGames.insert(game);
            s_activeGame = game;
            UpdateGameContext();
            return { true, {} };
        }
    }

    if (!adapter->Attach())
        return { false, adapter->GetLastError() };
    if (!adapter->Validate())
        return { false, adapter->GetLastError() };
    if (!adapter->Initialize())
        return { false, adapter->GetLastError() };

    s_attachedGames.insert(game);
    s_activeGame = game;
    UpdateGameContext();
    return { true, {} };
}

bool SwitchTo(::Launcher::GameId game) {
    if (!s_attachedGames.count(game)) return false;
    s_activeGame = game;
    UpdateGameContext();
    return true;
}

void EndSession(::Launcher::GameId game) {
    IGameAdapter* adapter = GetAdapter(game);
    if (adapter) {
        adapter->Shutdown();
    }
    s_attachedGames.erase(game);
    if (s_activeGame == game) {
        s_activeGame = ::Launcher::GameId::None;
        if (!s_attachedGames.empty()) {
            s_activeGame = *s_attachedGames.begin();
            UpdateGameContext();
        }
    }
}

::Launcher::GameId GetActiveGame() noexcept { return s_activeGame; }

bool IsAttached(::Launcher::GameId game) noexcept { return s_attachedGames.count(game); }

const std::unordered_set<::Launcher::GameId>& GetAttachedGames() noexcept { return s_attachedGames; }

void TickActive() {
    if (s_activeGame != ::Launcher::GameId::None) {
        IGameAdapter* adapter = GetAdapter(s_activeGame);
        if (adapter && adapter->IsGameProcessAlive()) {
            adapter->Tick();
        } else if (adapter) {
            EndSession(s_activeGame);
        }
    }
}

} // namespace OmniGhost::Launcher