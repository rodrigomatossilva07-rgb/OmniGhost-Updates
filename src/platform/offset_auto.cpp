#include "offset_auto.h"

#include "cheatoffsets_api.h"
#include "../../Cs2/cs2_game.h"
#include "../../Warzone/warzone_game.h"
#include "../../Valorant/valorant_game.h"
#include "../../Fivem/game/offsets.h"
#include "app_paths.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace OmniGhost::OffsetAuto {

std::string g_status = "Offsets prontos";
std::atomic_bool g_busy{false};

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

bool InstallCandidate(const std::filesystem::path& candidate,
                      const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (std::filesystem::exists(target, ec)) {
        const auto backup = target.wstring() + L".lastgood";
        std::filesystem::copy_file(target, backup,
            std::filesystem::copy_options::overwrite_existing, ec);
        ec.clear();
    }
    std::filesystem::rename(candidate, target, ec);
    if (!ec) return true;
    ec.clear();
    std::filesystem::copy_file(candidate, target,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) std::filesystem::remove(candidate, ec);
    return !ec;
}
} // namespace

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
    // Soft gate: outdated offsets are a warning, not a hard block.
    // User can still open the menu; features may fail until SoftProbe/live
    // validation succeeds. Unsupported stays non-blocking here as well —
    // launch path reports the real attach/read error instead.
    (void)game;
    return false;
}
bool SupportsAutomaticRefresh(ActiveGame game) {
    switch (game) {
    case ActiveGame::CS2:
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
    case ActiveGame::Warzone:
#endif
    case ActiveGame::FiveM:
    case ActiveGame::Apex:
        return true;
    default:
        return false;
    }
}

bool HasCriticalOffsets(ActiveGame game) {
    if (game == ActiveGame::CS2) {
        return CS2::offsets.loaded
            && CS2::offsets.dwEntityList != 0
            && CS2::offsets.dwViewMatrix != 0;
    }
    if (game == ActiveGame::Warzone) {
        return Warzone::offsets.loaded && Warzone::offsets.view_matrix != 0;
    }
    if (game == ActiveGame::Apex || game == ActiveGame::FiveM) {
        // File-backed dumps from cheatoffsets — presence is enough until dedicated loaders expand
        return true;
    }
    return true;
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
    case ActiveGame::Warzone:
        return Warzone::ready && Warzone::SoftProbeLobbyOffsets();
    case ActiveGame::Valorant:
        return Valorant::runtime.attached && Valorant::SoftProbeLobbyOffsets();
    case ActiveGame::FiveM:
        return FiveM::SoftProbeLobbyOffsets();
    case ActiveGame::Apex:
        return true; // file-backed until full runtime exists
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
    case ActiveGame::Valorant:
        return "Offsets Valorant inválidos (UWorld RVA). Atualiza os offsets.";
    case ActiveGame::FiveM:
        return "Offsets FiveM inválidos (world pointer / build). Verifica a build do servidor.";
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
#if !defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
    (void)force_refresh;
    // Customer snapshots are immutable PE resources. Updating a JSON beside the
    // executable could not replace those bytes, so Release never creates a data/
    // cache or pretends that a downloaded file changed the active snapshot.
    g_status = "Offsets integrados na versão instalada";
    MarkLiveValid(game, g_status);
    return Result::Current;
#else
    std::string networkStatus;
    const auto data = OmniGhost::Paths::InstallDirectory() / L"data";

    auto try_install = [&](const std::filesystem::path& candidate,
                           const std::filesystem::path& target,
                           auto load_fn) -> bool {
        if (!std::filesystem::exists(candidate)) return false;
        if (!load_fn(candidate.string().c_str())) return false;
        if (!HasCriticalOffsets(game)) return false;
        if (!InstallCandidate(candidate, target)) return false;
        load_fn(target.string().c_str());
        return true;
    };

    if (game == ActiveGame::CS2) {
        const auto candidate = data / L"cs2_offsets.candidate.json";
        const bool downloaded = force_refresh
            ? OmniGhost::CheatOffsets::FetchGame("cs2", candidate.string().c_str(), &networkStatus)
            : OmniGhost::CheatOffsets::FetchGameIfChanged("cs2", candidate.string().c_str(), &networkStatus);
        // LoadOffsetsFromJson now accepts API markdown + classic client.dll JSON.
        if (downloaded && try_install(candidate, data / L"cs2_offsets.json",
                [](const char* p) { return CS2::LoadOffsetsFromJson(p); })) {
            MarkLiveValid(game, networkStatus.empty()
                ? "CS2: offsets atualizados via cheatoffsets.com" : networkStatus);
            g_status = "CS2: offsets atualizados";
            return Result::Ok;
        }
        if (!force_refresh && networkStatus.find("304") != std::string::npos) {
            const bool cached = CS2::LoadOffsetsFromJson(nullptr) && HasCriticalOffsets(game);
            if (cached) {
                MarkLiveValid(game, "CS2: já atuais (API 304)");
                g_status = "CS2: offsets já atuais";
                return Result::Current;
            }
        }
        const bool cached = CS2::LoadOffsetsFromJson(nullptr) && HasCriticalOffsets(game);
        if (!downloaded) {
            g_status = cached
                ? (std::string("CS2: API indisponível (") + networkStatus + "); mantido último válido")
                : (std::string("CS2: API falhou (") + networkStatus + ") e offsets críticos em falta");
        } else {
            g_status = cached
                ? "CS2: dump descarregado mas validação parcial; mantido último válido"
                : "CS2: dump da API sem dwEntityList/dwViewMatrix reconhecíveis";
        }
        if (cached && !BlocksLaunch(game)) MarkLiveValid(game, g_status);
        else if (!cached) MarkOutdated(game, g_status);
        return cached ? Result::UsingCached : Result::RefreshFailed;
    }
    if (game == ActiveGame::Warzone) {
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
        const auto candidate = data / L"warzone_offsets.candidate.json";
        const bool downloaded = force_refresh
            ? OmniGhost::CheatOffsets::FetchGame("warzone", candidate.string().c_str(), &networkStatus)
            : OmniGhost::CheatOffsets::FetchGameIfChanged("warzone", candidate.string().c_str(), &networkStatus);
        if (downloaded && InstallCandidate(candidate, data / L"warzone_offsets.json")) {
            const std::string external = (data / L"warzone_offsets.json").string();
            Warzone::LoadOffsetsFromJson(external.c_str());
            if (HasCriticalOffsets(game)) {
                MarkLiveValid(game, "Warzone: offsets externos atualizados (Tester)");
                g_status = "Warzone: offsets externos atualizados (Tester)";
                return Result::Ok;
            }
        }
#endif
        const bool loaded = Warzone::ReloadOffsets();
        const bool usable = loaded && HasCriticalOffsets(game);
        g_status = usable ? "Warzone: offsets embedded carregados"
                          : "Warzone: recurso de offsets embedded invalido/ausente";
        if (usable) MarkLiveValid(game, g_status);
        else MarkOutdated(game, g_status);
        return usable ? Result::Current : Result::RefreshFailed;
    }
    if (game == ActiveGame::FiveM) {
        const auto candidate = data / L"fivem_offsets.candidate.json";
        const auto target = data / L"fivem_offsets.json";
        const bool downloaded = force_refresh
            ? OmniGhost::CheatOffsets::FetchGame("fivem", candidate.string().c_str(), &networkStatus)
            : OmniGhost::CheatOffsets::FetchGameIfChanged("fivem", candidate.string().c_str(), &networkStatus);
        if (downloaded && std::filesystem::exists(candidate)) {
            // Keep embedded build table as runtime authority; still refresh the data dump for
            // future loaders / diagnostics and so the launcher stops showing "outdated".
            if (InstallCandidate(candidate, target) || std::filesystem::exists(target)) {
                MarkLiveValid(game, networkStatus.empty()
                    ? "FiveM: offsets atualizados via cheatoffsets.com" : networkStatus);
                g_status = "FiveM: offsets atualizados (API + tabela de builds local)";
                return Result::Ok;
            }
        }
        if (!force_refresh && networkStatus.find("304") != std::string::npos) {
            MarkLiveValid(game, "FiveM: já atuais (API 304)");
            g_status = "FiveM: offsets já atuais";
            return Result::Current;
        }
        // Embedded table always allows launch; API dump is best-effort
        MarkLiveValid(game, "FiveM: tabela de builds local (API indisponível)");
        g_status = std::string("FiveM: API indisponível (") + networkStatus + "); tabela local OK";
        return Result::UsingCached;
    }
    if (game == ActiveGame::Apex) {
        const auto candidate = data / L"apex_offsets.candidate.json";
        const auto target = data / L"apex_offsets.json";
        const bool downloaded = force_refresh
            ? OmniGhost::CheatOffsets::FetchGame("apex", candidate.string().c_str(), &networkStatus)
            : OmniGhost::CheatOffsets::FetchGameIfChanged("apex", candidate.string().c_str(), &networkStatus);
        if (downloaded && std::filesystem::exists(candidate)) {
            std::error_code ec;
            const auto sz = std::filesystem::file_size(candidate, ec);
            if (!ec && sz > 64 && InstallCandidate(candidate, target)) {
                MarkLiveValid(game, networkStatus.empty()
                    ? "Apex: offsets atualizados via cheatoffsets.com" : networkStatus);
                g_status = "Apex: offsets atualizados";
                return Result::Ok;
            }
        }
        if (!force_refresh && networkStatus.find("304") != std::string::npos) {
            MarkLiveValid(game, "Apex: já atuais (API 304)");
            g_status = "Apex: offsets já atuais";
            return Result::Current;
        }
        std::error_code ec;
        const bool cached = std::filesystem::exists(target, ec) &&
            !ec && std::filesystem::file_size(target, ec) > 64;
        g_status = cached
            ? (std::string("Apex: API indisponível (") + networkStatus + "); mantido último válido")
            : "Apex: offsets em falta — tenta Atualizar offsets";
        if (cached) MarkLiveValid(game, g_status);
        else MarkOutdated(game, g_status);
        return cached ? Result::UsingCached : Result::RefreshFailed;
    }

    g_status = "Atualização automática não suportada com segurança para este jogo";
    MarkUnsupported(game, g_status);
    return Result::NotSupported;
#endif
}

int RefreshAllSupported(bool force) {
    int ok = 0;
    const ActiveGame games[] = {
        ActiveGame::CS2,
        ActiveGame::Warzone,
        ActiveGame::FiveM,
        ActiveGame::Apex,
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

} // namespace OmniGhost::OffsetAuto
