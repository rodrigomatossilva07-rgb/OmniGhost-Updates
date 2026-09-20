#include "epic_game_update_check.h"
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
#include <thread>
namespace Launcher::EpicGameUpdateCheck {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
struct GameManifest { GameId game; const char* appName; };
constexpr std::array<GameManifest, 2> kGames{{ {GameId::Fortnite, "Fortnite"}, {GameId::Rust, "Rust"} }};
struct SharedState { std::mutex mutex; std::array<Status, kGames.size()> status{}; std::atomic_bool checking{false}; Clock::time_point lastStart{}; };
SharedState& Shared() { static SharedState state; return state; }
bool ContainsApp(std::string_view text, std::string_view appName) {
    const std::string mainApp = "\"MainGameAppName\": \"" + std::string(appName) + "\"";
    const std::string app = "\"AppName\": \"" + std::string(appName) + "\"";
    const std::string folder = "\"MandatoryAppFolderName\": \"" + std::string(appName) + "\"";
    return text.find(mainApp) != std::string_view::npos || text.find(app) != std::string_view::npos || text.find(folder) != std::string_view::npos;
}
std::string ReadSmallFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary); if (!stream) return {};
    std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return contents.size() <= 512 * 1024 ? contents : std::string{};
}
void Scan() {
    const fs::path root = fs::path(L"C:\\ProgramData") / L"Epic" / L"EpicGamesLauncher" / L"Data" / L"Manifests";
    std::array<Status, kGames.size()> results{}; std::error_code error;
    if (fs::is_directory(root, error)) for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end; it != end; it.increment(error)) {
        if (error) { error.clear(); continue; }
        if (!it->is_regular_file(error) || it->path().extension() != L".item") continue;
        const std::string content = ReadSmallFile(it->path()); if (content.empty()) continue;
        const bool pending = it->path().parent_path().filename() == L"Pending";
        for (std::size_t index = 0; index < kGames.size(); ++index) if (ContainsApp(content, kGames[index].appName)) {
            results[index].installed = true; results[index].updatePending = results[index].updatePending || pending;
            results[index].detail = pending ? "Atualização detetada pelo Epic Games Launcher." : "Instalação Epic Games Launcher encontrada.";
        }
    }
    SharedState& shared = Shared(); { std::scoped_lock lock(shared.mutex); shared.status = std::move(results); }
    shared.checking.store(false, std::memory_order_release);
}
}
void StartForLibrary() {
    SharedState& shared = Shared(); if (shared.checking.load(std::memory_order_acquire)) return; const Clock::time_point now = Clock::now();
    { std::scoped_lock lock(shared.mutex); if (shared.lastStart.time_since_epoch().count() != 0 && now - shared.lastStart < std::chrono::seconds(30)) return; shared.lastStart = now; }
    bool expected = false; if (!shared.checking.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return; std::thread(Scan).detach();
}
Status Get(GameId game) {
    for (std::size_t index = 0; index < kGames.size(); ++index) if (kGames[index].game == game) { SharedState& shared = Shared(); std::scoped_lock lock(shared.mutex); Status result = shared.status[index]; result.checking = shared.checking.load(std::memory_order_acquire); return result; } return {};
}
}
