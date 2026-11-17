#include "epic_game_update_check.h"
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <thread>
#include "platform/strprotect/strprotect.h"
namespace Launcher::EpicGameUpdateCheck {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
struct GameManifest { GameId game; const char* appName; };
constexpr std::array<GameManifest, 1> kGames{{ {GameId::Fortnite, "Fortnite"} }};
struct SharedState { std::mutex mutex; std::array<Status, kGames.size()> status{}; std::atomic_bool checking{false}; std::jthread worker{}; Clock::time_point lastStart{}; };
SharedState& Shared() { static SharedState state; return state; }
bool ContainsApp(std::string_view text, std::string_view appName) {
    const std::string mainApp = OGS_LIT("\"MainGameAppName\": \"") + std::string(appName) + "\"";
    const std::string app = OGS_LIT("\"AppName\": \"") + std::string(appName) + "\"";
    const std::string folder = OGS_LIT("\"MandatoryAppFolderName\": \"") + std::string(appName) + "\"";
    return text.find(mainApp) != std::string_view::npos || text.find(app) != std::string_view::npos || text.find(folder) != std::string_view::npos;
}
std::string ReadSmallFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary); if (!stream) return {};
    std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return contents.size() <= 512 * 1024 ? contents : std::string{};
}
void Scan(const std::stop_token& stop) {
    try {
        const fs::path root = fs::path(std::wstring(OGS_WIDE("C:\\ProgramData"))) / std::wstring(OGS_WIDE("Epic")) / std::wstring(OGS_WIDE("EpicGamesLauncher")) / std::wstring(OGS_WIDE("Data")) / std::wstring(OGS_WIDE("Manifests"));
        std::array<Status, kGames.size()> results{}; std::error_code error;
        if (fs::is_directory(root, error)) for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end; it != end; it.increment(error)) {
            if (stop.stop_requested()) break;
            if (error) { error.clear(); continue; }
            if (!it->is_regular_file(error) || it->path().extension().wstring() != std::wstring(OGS_WIDE(".item"))) continue;
            const std::string content = ReadSmallFile(it->path()); if (content.empty()) continue;
            const bool pending = it->path().parent_path().filename().wstring() == std::wstring(OGS_WIDE("Pending"));
            for (std::size_t index = 0; index < kGames.size(); ++index) if (ContainsApp(content, kGames[index].appName)) {
                results[index].installed = true; results[index].updatePending = results[index].updatePending || pending;
                results[index].detail = pending ? OGS_LIT("Atualização detetada pelo Epic Games Launcher.") : OGS_LIT("Instalação Epic Games Launcher encontrada.");
            }
        }
        SharedState& shared = Shared(); { std::scoped_lock lock(shared.mutex); shared.status = std::move(results); }
    } catch (...) {
        std::cerr << OGS_LIT("[OmniGhost][EpicUpdate] Falha ao verificar atualizações.") << std::endl;
    }
    SharedState& shared = Shared(); shared.checking.store(false, std::memory_order_release);
}
}
void StartForLibrary() {
    SharedState& shared = Shared(); if (shared.checking.load(std::memory_order_acquire)) return; const Clock::time_point now = Clock::now();
    { std::scoped_lock lock(shared.mutex); if (shared.lastStart.time_since_epoch().count() != 0 && now - shared.lastStart < std::chrono::seconds(30)) return; shared.lastStart = now; }
    bool expected = false; if (!shared.checking.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    if (shared.worker.joinable()) shared.worker.join();
    shared.worker = std::jthread([](std::stop_token stop) { Scan(stop); });
}
void Shutdown() {
    SharedState& shared = Shared();
    shared.worker.request_stop();
    if (shared.worker.joinable()) shared.worker.join();
}
Status Get(GameId game) {
    for (std::size_t index = 0; index < kGames.size(); ++index) if (kGames[index].game == game) { SharedState& shared = Shared(); std::scoped_lock lock(shared.mutex); Status result = shared.status[index]; result.checking = shared.checking.load(std::memory_order_acquire); return result; } return {};
}
}

