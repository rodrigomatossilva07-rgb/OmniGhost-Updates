#include "offset_auto.h"

#include "src/games/Cs2/cs2_game.h"
#include "src/games/Fivem/game/offsets.h"
#include "src/games/Rust/rust_offsets.h"

#include <array>
#include <mutex>

namespace OmniGhost::OffsetAuto {

namespace {


std::mutex g_status_mutex;
std::string g_status_value = "Offsets prontos";
}

std::atomic_bool g_busy{false};

StatusProxy::operator std::string() const {
    std::scoped_lock lock(g_status_mutex);
    return g_status_value;
}

StatusProxy& StatusProxy::operator=(std::string_view value) {
    std::scoped_lock lock(g_status_mutex);
    g_status_value = value;
    return *this;
}

namespace {
std::mutex g_state_mutex;
std::mutex g_refresh_mutex;
constexpr std::size_t kGameCount = static_cast<std::size_t>(ActiveGame::Apex) + 1;
std::array<CompatibilitySnapshot, kGameCount> g_states{};

size_t Index(ActiveGame game) { return static_cast<size_t>(game); }

void SetState(ActiveGame game, CompatibilityState state, const std::string& detail) {
    std::scoped_lock lock(g_state_mutex);
    const std::size_t index = Index(game);
    if (index >= g_states.size()) return;
    g_states[index] = {state, detail};
}

}

CompatibilitySnapshot Compatibility(ActiveGame game) {
    std::scoped_lock lock(g_state_mutex);
    const std::size_t index = Index(game);
    if (index >= g_states.size())
        return {CompatibilityState::Unsupported, "Jogo desconhecido"};
    return g_states[index];
}

void MarkChecking(ActiveGame game) {
    const auto current = Compatibility(game);
    if (current.state != CompatibilityState::Outdated)
        SetState(game, CompatibilityState::Checking, "A verificar compatibilidade dos offsets");
}
void MarkLiveValid(ActiveGame game, const std::string& detail) {
    SetState(game, CompatibilityState::Ready,
        detail.empty() ? "Offsets validados com o jogo em execução" : detail);
}
void MarkOutdated(ActiveGame game, const std::string& detail) {
    SetState(game, CompatibilityState::Outdated,
        detail.empty() ? "Offsets incompatíveis com o build atual" : detail);
}
void MarkUnsupported(ActiveGame game, const std::string& detail) {
    SetState(game, CompatibilityState::Unsupported, detail);
}
bool BlocksLaunch(ActiveGame game) {




    (void)game;
    return false;
}
bool SupportsAutomaticRefresh(ActiveGame game) {
    (void)game;
    return false;
}

bool HasCriticalOffsets(ActiveGame game) {
    if (game == ActiveGame::CS2) {
        return CS2::offsets.loaded
            && CS2::offsets.dwEntityList != 0
            && CS2::offsets.dwViewMatrix != 0;
    }
    if (game == ActiveGame::FiveM) {

        return FiveM::HasAnyCriticalOffsets();
    }
    if (game == ActiveGame::Rust) {
        using namespace Rust::Offsets::Build_2026_10_22;
        return Rvas::TypeManagerInstance != 0 && Rvas::TypeToObjectSet != 0 &&
               BaseNetworkableStatic::clientEntities != 0 &&
               BasePlayer::_displayName != 0 && BaseCombatEntity::_health != 0;
    }
    if (game == ActiveGame::Apex) {

        return false;
    }
    return false;
}

bool ValidateLive(ActiveGame game) {
    if (game == ActiveGame::CS2) {
        if (!CS2::ready) return false;
        return CS2::ValidateLiveOffsets();
    }
    return true;
}

bool SoftProbeLive(ActiveGame game) {
    switch (game) {
    case ActiveGame::CS2:
        return CS2::ready && CS2::ValidateLiveOffsets();
    case ActiveGame::FiveM:
        return FiveM::SoftProbeLobbyOffsets();
    case ActiveGame::Rust:
        return HasCriticalOffsets(game);
    case ActiveGame::Apex:
        return true;
    default:
        return true;
    }
}

const char* SoftProbeFailReason(ActiveGame game) {
    switch (game) {
    case ActiveGame::CS2:
        return "Offsets CS2 inválidos no lobby (entity list / view matrix). Atualiza os offsets.";
    case ActiveGame::Warzone:
        return "Offsets Warzone inválidos (view_matrix / módulo). Atualiza os offsets.";
    case ActiveGame::FiveM:
        return "Offsets FiveM inválidos (world pointer / build). Verifica a build do servidor.";
    case ActiveGame::Rust:
        return "Runtime Rust removido; apenas o catálogo manual de offsets foi preservado.";
    default:
        return "Offsets inválidos — atualiza e tenta de novo.";
    }
}

Result EnsureOffsets(ActiveGame game, bool force_refresh) {
    std::unique_lock refreshLock(g_refresh_mutex, std::try_to_lock);
    if (!refreshLock.owns_lock()) return Result::Busy;
    g_busy.store(true);
    struct BusyReset {
        ~BusyReset() { g_busy.store(false); }
    } reset;

    MarkChecking(game);
    (void)force_refresh;

    if (game == ActiveGame::CS2) {
        const bool usable = CS2::LoadOffsetsFromJson(nullptr) && HasCriticalOffsets(game);
        g_status = usable ? "CS2: offsets manuais carregados"
                          : "CS2: offsets manuais inválidos ou em falta";
        if (usable) MarkLiveValid(game, g_status);
        else MarkOutdated(game, g_status);
        return usable ? Result::Current : Result::RefreshFailed;
    }
    if (game == ActiveGame::FiveM) {
        const bool usable = HasCriticalOffsets(game);
        g_status = usable ? "FiveM: tabela manual de builds carregada"
                          : "FiveM: offsets manuais inválidos ou em falta";
        if (usable) MarkLiveValid(game, g_status);
        else MarkOutdated(game, g_status);
        return usable ? Result::Current : Result::RefreshFailed;
    }
    if (game == ActiveGame::Rust) {
        const bool usable = HasCriticalOffsets(game);
        g_status = usable ? "Rust: catálogo manual 2026-10-22 carregado"
                          : "Rust: catálogo manual inválido ou em falta";
        if (usable) MarkLiveValid(game, g_status);
        else MarkOutdated(game, g_status);
        return usable ? Result::Current : Result::RefreshFailed;
    }



    g_status = "Atualização automática não suportada com segurança para este jogo";
    MarkUnsupported(game, g_status);
    return Result::NotSupported;
}

int RefreshAllSupported(bool force) {
    int ok = 0;
    const ActiveGame games[] = {
        ActiveGame::CS2,
        ActiveGame::FiveM,
        ActiveGame::Rust,
    };
    std::string all;
    for (ActiveGame g : games) {
        const Result r = EnsureOffsets(g, force);
        if (r == Result::Ok || r == Result::Current || r == Result::UsingCached)
            ++ok;
        if (!all.empty()) all += " | ";
        all += g_status;
    }
    g_status = all;
    return ok;
}

}
