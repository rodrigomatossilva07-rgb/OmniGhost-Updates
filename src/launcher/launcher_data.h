#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Launcher {

enum class GameId : int {
    None = 0,
    FiveM = 1,
    CS2 = 2,
    Valorant = 3,
    Fortnite = 4,
    Rust = 5,
    Warzone = 6,
    Apex = 7,
    Count = 8
};

enum class ChangeType : int {
    Added = 0,
    Improved,
    Fixed,
    Performance,
    Compatibility,
    Security,
    Removed,
    Breaking,
    Maintenance
};

enum class SessionResult : int {
    None = 0,
    Completed,
    LaunchFailed,
    OffsetsFailed,
    DeviceMissing,
    ProcessEnded,
    GameNotFound
};

struct GameHistory {
    bool favorite = false;
    std::uint64_t lastUsedUnix = 0;
    SessionResult lastResult = SessionResult::None;
    std::string detail;
    std::uint64_t totalActiveSeconds = 0;
    std::uint64_t lastSessionSeconds = 0;
    std::uint32_t sessionCount = 0;
    struct Session {
        std::uint64_t endedUnix = 0;
        std::uint64_t activeSeconds = 0;
        SessionResult result = SessionResult::None;
    };
    std::vector<Session> recentSessions;
};

struct RgbColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct GameDefinition {
    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    GameId launch_id;
    const char* id;
    const char* name;
    const char* description;
    const char* logo_path;
    const char* banner_path;
    const char* version;
    bool coming_soon; // true = backend not yet integrated; install readiness is detected at runtime
    bool beta;        // visible maturity badge; does not change launch readiness
    RgbColor banner_top;
    RgbColor banner_bottom;
    RgbColor accent;
    const char* fallback_mark;
    const char* tagline;
    std::uint32_t schema_version = CurrentSchemaVersion;
};

struct UpdateChange {
    ChangeType type;
    const char* text;
};

struct UpdateDefinition {
    const char* id;
    const char* game_id;
    const char* version;
    const char* date_iso;
    const char* date_display;
    const char* title;
    const char* summary;
    const UpdateChange* changes;
    std::size_t change_count;
    bool example;
};

// Add future games only in launcher_data.cpp. The library and filters consume
// this list automatically; no layout duplication is required.
const GameDefinition* Games(std::size_t& count);
const UpdateDefinition* Updates(std::size_t& count);
const GameDefinition* FindGame(const char* id);
const GameDefinition* FindGame(GameId id);

const char* ChangeTypeLabel(ChangeType type);
const char* SessionResultLabel(SessionResult result);

void LoadLauncherState();
bool IsUpdateRead(const char* update_id);
void MarkUpdateRead(const char* update_id);
void MarkAllUpdatesRead();
int UnreadUpdateCount();

// Persistent Control Center state. This is local UX metadata only and never
// participates in licensing/authentication decisions.
bool IsFavorite(GameId id);
void SetFavorite(GameId id, bool favorite);
GameHistory GetGameHistory(GameId id);
void MarkGameUsed(GameId id);
void RecordGameSession(GameId id, SessionResult result, const std::string& detail = {});
void BeginTimedGameSession(GameId id);
void EndTimedGameSession(GameId id, SessionResult result, const std::string& detail = {});
GameId LastPlayedGame();
GameId MostRecentHistoryGame();
void ForgetLastPlayedGame();

} // namespace Launcher
