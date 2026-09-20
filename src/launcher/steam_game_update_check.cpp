#include "steam_game_update_check.h"

#include "../platform/app_paths.h"
#include "../updater/http_client.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <thread>

namespace Launcher::SteamGameUpdateCheck {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct GameQuery { GameId game; unsigned appId; const wchar_t* offsetsFile; };
constexpr std::array<GameQuery, 3> kGames{{
    {GameId::CS2, 730, L"cs2_offsets.json"},
    {GameId::Rust, 252490, L"rust_offsets.json"},
    {GameId::Warzone, 1938090, L"warzone_offsets.json"},
}};

struct SharedState {
    std::mutex mutex;
    std::array<Status, kGames.size()> status{};
    std::atomic_bool checking{false};
    Clock::time_point lastStart{};
};

SharedState& Shared() { static SharedState state; return state; }

const GameQuery* QueryFor(GameId game) {
    for (const GameQuery& query : kGames) if (query.game == game) return &query;
    return nullptr;
}

std::size_t IndexFor(GameId game) {
    for (std::size_t index = 0; index < kGames.size(); ++index)
        if (kGames[index].game == game) return index;
    return kGames.size();
}

std::uint64_t FileTimestamp(const GameQuery& query) {
    const fs::path path = OmniGhost::Paths::InstallDirectory() / L"data" / query.offsetsFile;
    std::error_code error;
    fs::path reference = path;
    // Release builds package offsets as resources. In that configuration there
    // is deliberately no plaintext data/<game>_offsets.json beside the EXE;
    // use the installed launcher binary as the timestamp of the embedded set
    // instead of treating the offsets as missing on every library open.
    if (!fs::is_regular_file(reference, error)) {
        error.clear();
        reference = OmniGhost::Paths::Executable();
    }
    if (!fs::is_regular_file(reference, error)) return 0;
    const auto writeTime = fs::last_write_time(reference, error);
    if (error) return 0;
    const auto systemTime = std::chrono::time_point_cast<std::chrono::seconds>(
        writeTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const auto seconds = systemTime.time_since_epoch().count();
    return seconds > 0 ? static_cast<std::uint64_t>(seconds) : 0;
}

std::uint64_t LatestNewsTimestamp(std::string_view json) {
    std::uint64_t latest{};
    std::size_t cursor{};
    while ((cursor = json.find("\"date\"", cursor)) != std::string_view::npos) {
        const std::size_t colon = json.find(':', cursor + 6);
        if (colon == std::string_view::npos) break;
        const std::size_t begin = json.find_first_of("0123456789", colon + 1);
        if (begin == std::string_view::npos) break;
        std::uint64_t value{};
        const char* first = json.data() + begin;
        const char* last = json.data() + json.size();
        const auto [end, code] = std::from_chars(first, last, value);
        if (code == std::errc{}) latest = (std::max)(latest, value);
        cursor = static_cast<std::size_t>(end - json.data());
    }
    return latest;
}

Status MakeStatus(const GameQuery& query, std::uint64_t latestNews, std::string detail) {
    Status result{};
    result.latestNewsUnix = latestNews;
    result.localOffsetsUnix = FileTimestamp(query);
    result.detail = std::move(detail);
    if (latestNews == 0) result.state = State::Unavailable;
    else if (result.localOffsetsUnix == 0 || latestNews > result.localOffsetsUnix) result.state = State::OffsetsOutdated;
    else result.state = State::Current;
    return result;
}

void QuerySteam() {
    OmniGhost::Update::WinHttpClient http;
    std::atomic_bool cancelled{false};
    std::array<Status, kGames.size()> results{};
    for (std::size_t index = 0; index < kGames.size(); ++index) {
        const GameQuery& query = kGames[index];
        const std::string url = "https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid="
            + std::to_string(query.appId) + "&count=1&maxlength=1&format=json";
        const OmniGhost::Update::HttpResult response = http.GetText(url, 8000, cancelled);
        if (response.statusCode != 200) {
            results[index] = MakeStatus(query, 0, "Não foi possível consultar a Steam agora.");
            continue;
        }
        const std::uint64_t newsDate = LatestNewsTimestamp(response.body);
        results[index] = MakeStatus(query, newsDate,
            newsDate ? "Steam consultada automaticamente." : "A Steam não devolveu notícias para este jogo.");
    }
    SharedState& shared = Shared();
    { std::scoped_lock lock(shared.mutex); shared.status = std::move(results); }
    shared.checking.store(false, std::memory_order_release);
}
} // namespace

void StartForLibrary() {
    SharedState& shared = Shared();
    if (shared.checking.load(std::memory_order_acquire)) return;
    const Clock::time_point now = Clock::now();
    {
        std::scoped_lock lock(shared.mutex);
        if (shared.lastStart.time_since_epoch().count() != 0 && now - shared.lastStart < std::chrono::minutes(5)) return;
        shared.lastStart = now;
        for (Status& status : shared.status) status.state = State::Checking;
    }
    bool expected = false;
    if (!shared.checking.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    std::thread(QuerySteam).detach();
}

Status Get(GameId game) {
    const GameQuery* query = QueryFor(game);
    const std::size_t index = IndexFor(game);
    if (!query || index == kGames.size()) return {};
    SharedState& shared = Shared();
    Status result;
    { std::scoped_lock lock(shared.mutex); result = shared.status[index]; }
    result.localOffsetsUnix = FileTimestamp(*query);
    if (result.latestNewsUnix != 0)
        result.state = result.localOffsetsUnix == 0 || result.latestNewsUnix > result.localOffsetsUnix
            ? State::OffsetsOutdated : State::Current;
    return result;
}
} // namespace Launcher::SteamGameUpdateCheck
