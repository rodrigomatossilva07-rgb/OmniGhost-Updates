#include "launcher_data.h"
#include "../platform/app_paths.h"
#include "../config/app_settings.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Launcher {
namespace {

constexpr unsigned kLauncherStateSchema = 2;

constexpr GameDefinition kGames[] = {
    {
        GameId::FiveM,
        "fivem",
        "FiveM",
        "launcher.game.fivem.description",
        "resources/games/fivem/logo.png",
        "resources/games/fivem/banner.png",
        "b3258",
        false,
        false,
        { 15, 24, 39 }, { 6, 9, 16 }, { 80, 135, 210 },
        "V", "launcher.game.fivem.tagline"
    },
    {
        GameId::CS2,
        "cs2",
        "Counter-Strike 2",
        "launcher.game.cs2.description",
        "resources/games/cs2/logo.png",
        "resources/games/cs2/banner.png",
        "1.5.0",
        false,
        false,
        { 58, 32, 16 }, { 18, 12, 10 }, { 230, 120, 55 },
        "2", "launcher.game.cs2.tagline"
    },
    {
        GameId::Rust,
        "rust",
        "Rust",
        "launcher.game.rust.description",
        "resources/games/rust/logo.png",
        "resources/games/rust/banner.png",
        "0.1.0",
        false,
        true,
        { 40, 28, 22 }, { 18, 12, 10 }, { 212, 175, 55 },
        "R", "launcher.game.rust.tagline"
    },
    {
        GameId::Warzone,
        "warzone",
        "Call of Duty: Warzone",
        "launcher.game.warzone.description",
        "resources/games/warzone/logo.png",
        "resources/games/warzone/banner.png",
        "0.1.0",
        false,
        true,
        { 30, 34, 28 },
        { 9, 11, 9 },
        { 116, 164, 92 },
        "W",
        "launcher.game.warzone.tagline"
    },
    {
        GameId::Valorant,
        "valorant",
        "Valorant",
        "launcher.game.valorant.description",
        "resources/games/valorant/logo.png",
        "resources/games/valorant/banner.png",
        "0.1.0",
        false,
        true,
        { 28, 6, 10 }, { 8, 2, 4 }, { 255, 70, 85 },
        "V", "launcher.game.valorant.tagline"
    },
    {
        GameId::Apex,
        "apex",
        "Apex Legends",
        "launcher.game.apex.description",
        "resources/games/apex/logo.png",
        "resources/games/apex/banner.png",
        "0.1.0",
        true,
        false,
        { 40, 12, 14 },
        { 14, 4, 6 },
        { 218, 41, 42 },
        "A",
        "launcher.game.apex.tagline"
    },
    {
        GameId::Fortnite,
        "fortnite",
        "Fortnite",
        "launcher.game.fortnite.description",
        "resources/games/fortnite/logo.png",
        "resources/games/fortnite/banner.png",
        "Beta",
        false, // launchable — visual menu + DMA attach (no ESP/aim yet)
        false,
        { 14, 18, 42 }, { 6, 8, 20 }, { 120, 170, 255 },
        "F", "launcher.game.fortnite.tagline"
    }
};

static_assert(std::size(kGames) == static_cast<std::size_t>(GameId::Count) - 1,
              "GameId and launcher catalogue must be updated together");

constexpr std::array<UpdateDefinition, 0> kUpdates{};

std::unordered_set<std::string> g_read_updates;
std::unordered_map<int, GameHistory> g_game_history;
GameId g_last_played = GameId::None;
bool g_state_loaded = false;

fs::path StatePath() {
    OmniGhost::Paths::EnsureUserDirectories();
    return OmniGhost::Paths::Configs() / L"launcher_state.cfg";
}

const char* GameIdString(GameId id) {
    if (const GameDefinition* game = FindGame(id))
        return game->id;
    return "";
}

GameId ParseGameId(const std::string& id) {
    if (const GameDefinition* game = FindGame(id.c_str()))
        return game->launch_id;
    return GameId::None;
}

std::string SanitizeStateText(std::string value) {
    std::replace(value.begin(), value.end(), '\n', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '|', '/');
    if (value.size() > 180)
        value.resize(180);
    return value;
}

void SaveLauncherState() {
    const fs::path path = StatePath();
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    
    // Write to temporary file first
    const fs::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return;
        file << "# OmniGhost launcher local state\n";
        file << "schema=" << kLauncherStateSchema << '\n';
        for (const std::string& id : g_read_updates)
            file << "read=" << id << '\n';
        if (app_settings::config.remember_last_game && g_last_played != GameId::None)
            file << "last_game=" << GameIdString(g_last_played) << '\n';
        for (const auto& [key, history] : g_game_history) {
            const GameId id = static_cast<GameId>(key);
            const char* gameId = GameIdString(id);
            if (!gameId || !*gameId) continue;
            if (history.favorite)
                file << "favorite=" << gameId << '\n';
            if (history.lastUsedUnix != 0 || history.lastResult != SessionResult::None || !history.detail.empty()) {
                file << "history=" << gameId << '|'
                     << history.lastUsedUnix << '|'
                     << static_cast<int>(history.lastResult) << '|'
                     << SanitizeStateText(history.detail) << '\n';
            }
        }
        file.flush();
        if (!file) {
            std::error_code removeError;
            fs::remove(temporary, removeError);
            return;
        }
    }
    
    // Atomic replace with backup
    const fs::path backup = path.wstring() + L".bak";
    if (fs::exists(path, error)) {
        fs::copy_file(path, backup, fs::copy_options::overwrite_existing, error);
    }
    
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // Success - remove backup
        std::error_code cleanupError;
        fs::remove(backup, cleanupError);
        return;
    }
    
    // Restore from backup on failure
    std::error_code restoreError;
    if (fs::exists(backup, restoreError)) {
        fs::copy_file(backup, path, fs::copy_options::overwrite_existing, restoreError);
        fs::remove(backup, restoreError);
    }
    std::error_code cleanupError;
    fs::remove(temporary, cleanupError);
}

std::uint64_t UnixNow() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

const GameDefinition* Games(std::size_t& count) {
    count = std::size(kGames);
    return kGames;
}

const UpdateDefinition* Updates(std::size_t& count) {
    count = kUpdates.size();
    return kUpdates.data();
}

const GameDefinition* FindGame(const char* id) {
    if (!id) return nullptr;
    const std::string_view requested(id);
    for (const GameDefinition& game : kGames) {
        if (std::string_view(game.id) == requested)
            return &game;
    }
    return nullptr;
}

const GameDefinition* FindGame(GameId id) {
    for (const GameDefinition& game : kGames) {
        if (game.launch_id == id)
            return &game;
    }
    return nullptr;
}

const char* ChangeTypeLabel(ChangeType type) {
    switch (type) {
    case ChangeType::Added:         return "ADICIONADO";
    case ChangeType::Improved:      return "MELHORADO";
    case ChangeType::Fixed:         return "CORRIGIDO";
    case ChangeType::Performance:   return "DESEMPENHO";
    case ChangeType::Compatibility: return "COMPATIBILIDADE";
    case ChangeType::Security:      return "SEGURANÇA";
    case ChangeType::Removed:       return "REMOVIDO";
    case ChangeType::Breaking:      return "ALTERAÇÃO IMPORTANTE";
    case ChangeType::Maintenance:   return "MANUTENÇÃO";
    default:                        return "ALTERAÇÃO";
    }
}

const char* SessionResultLabel(SessionResult result) {
    switch (result) {
    case SessionResult::Completed:    return "Concluída";
    case SessionResult::LaunchFailed: return "Falhou ao anexar";
    case SessionResult::OffsetsFailed:return "Offsets em falta";
    case SessionResult::DeviceMissing:return "Dispositivo em falta";
    case SessionResult::ProcessEnded: return "Jogo terminou";
    case SessionResult::GameNotFound: return "Jogo não encontrado";
    default:                          return "Sem histórico";
    }
}

void LoadLauncherState() {
    g_read_updates.clear();
    g_game_history.clear();
    g_last_played = GameId::None;

    std::ifstream file(StatePath());
    std::string line;
    unsigned loadedSchema = 1; // legacy files had no explicit version
    while (file && std::getline(file, line)) {
        if (line.rfind("schema=", 0) == 0) {
            try {
                loadedSchema = static_cast<unsigned>(std::stoul(line.substr(7)));
            } catch (...) {
                loadedSchema = 1;
            }
            continue;
        }
        if (line.rfind("read=", 0) == 0 && line.size() > 5) {
            g_read_updates.insert(line.substr(5));
            continue;
        }
        if (line.rfind("last_game=", 0) == 0) {
            g_last_played = ParseGameId(line.substr(10));
            continue;
        }
        if (line.rfind("favorite=", 0) == 0) {
            const GameId id = ParseGameId(line.substr(9));
            if (id != GameId::None)
                g_game_history[static_cast<int>(id)].favorite = true;
            continue;
        }
        if (line.rfind("history=", 0) == 0) {
            const std::string payload = line.substr(8);
            std::istringstream stream(payload);
            std::string gameId;
            std::string used;
            std::string result;
            std::string detail;
            if (!std::getline(stream, gameId, '|') ||
                !std::getline(stream, used, '|') ||
                !std::getline(stream, result, '|'))
                continue;
            std::getline(stream, detail);
            const GameId id = ParseGameId(gameId);
            if (id == GameId::None)
                continue;
            GameHistory& history = g_game_history[static_cast<int>(id)];
            try {
                history.lastUsedUnix = std::stoull(used);
                const int parsed = std::stoi(result);
                if (parsed >= static_cast<int>(SessionResult::None) &&
                    parsed <= static_cast<int>(SessionResult::GameNotFound))
                    history.lastResult = static_cast<SessionResult>(parsed);
            } catch (const std::exception& ex) {
                std::cerr << "[Launcher] Failed to parse game history for game " << static_cast<int>(id) << ": " << ex.what() << "\n";
                history.lastUsedUnix = 0;
                history.lastResult = SessionResult::None;
            } catch (...) {
                std::cerr << "[Launcher] Unknown exception parsing game history for game " << static_cast<int>(id) << "\n";
                history.lastUsedUnix = 0;
                history.lastResult = SessionResult::None;
            }
            history.detail = detail;
        }
    }
    // Unknown newer schemas keep recognized keys but are never rewritten just
    // by loading. Mutating an item writes the current normalized schema.
    (void)loadedSchema;
    g_state_loaded = true;
}

bool IsUpdateRead(const char* update_id) {
    if (!g_state_loaded) LoadLauncherState();
    return update_id && g_read_updates.contains(update_id);
}

void MarkUpdateRead(const char* update_id) {
    if (!update_id || !*update_id) return;
    if (!g_state_loaded) LoadLauncherState();
    if (g_read_updates.insert(update_id).second)
        SaveLauncherState();
}

void MarkAllUpdatesRead() {
    if (!g_state_loaded) LoadLauncherState();
    bool changed = false;
    for (const UpdateDefinition& update : kUpdates)
        changed = g_read_updates.insert(update.id).second || changed;
    if (changed) SaveLauncherState();
}

int UnreadUpdateCount() {
    if (!g_state_loaded) LoadLauncherState();
    int count = 0;
    for (const UpdateDefinition& update : kUpdates) {
        if (!g_read_updates.contains(update.id))
            ++count;
    }
    return count;
}

bool IsFavorite(GameId id) {
    if (!g_state_loaded) LoadLauncherState();
    const auto it = g_game_history.find(static_cast<int>(id));
    return it != g_game_history.end() && it->second.favorite;
}

void SetFavorite(GameId id, bool favorite) {
    if (id == GameId::None) return;
    if (!g_state_loaded) LoadLauncherState();
    GameHistory& history = g_game_history[static_cast<int>(id)];
    if (history.favorite == favorite) return;
    history.favorite = favorite;
    SaveLauncherState();
}

GameHistory GetGameHistory(GameId id) {
    if (!g_state_loaded) LoadLauncherState();
    const auto it = g_game_history.find(static_cast<int>(id));
    return it == g_game_history.end() ? GameHistory{} : it->second;
}

void MarkGameUsed(GameId id) {
    if (id == GameId::None) return;
    if (!g_state_loaded) LoadLauncherState();
    GameHistory& history = g_game_history[static_cast<int>(id)];
    history.lastUsedUnix = UnixNow();
    g_last_played = app_settings::config.remember_last_game ? id : GameId::None;
    SaveLauncherState();
}

void RecordGameSession(GameId id, SessionResult result, const std::string& detail) {
    if (id == GameId::None) return;
    if (!g_state_loaded) LoadLauncherState();
    GameHistory& history = g_game_history[static_cast<int>(id)];
    if (history.lastUsedUnix == 0)
        history.lastUsedUnix = UnixNow();
    history.lastResult = result;
    history.detail = SanitizeStateText(detail);
    g_last_played = app_settings::config.remember_last_game ? id : GameId::None;
    SaveLauncherState();
}

GameId LastPlayedGame() {
    if (!g_state_loaded) LoadLauncherState();
    return app_settings::config.remember_last_game ? g_last_played : GameId::None;
}

void ForgetLastPlayedGame() {
    if (!g_state_loaded) LoadLauncherState();
    g_last_played = GameId::None;
    SaveLauncherState();
}

} // namespace Launcher
