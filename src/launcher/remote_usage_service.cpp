#include "remote_usage_service.h"

#include "launcher_data.h"
#include "../app_version.h"
#include "../licensing/license_service.h"
#include "../updater/json.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#pragma comment(lib, "ole32.lib")

namespace OmniGhost::RemoteUsage {
namespace {
std::mutex g_mutex;
std::jthread g_worker;
std::map<int, Summary> g_summaries;

const char* GameName(::Launcher::GameId game) noexcept {
    switch (game) {
    case ::Launcher::GameId::FiveM: return "fivem";
    case ::Launcher::GameId::CS2: return "cs2";
    case ::Launcher::GameId::Fortnite: return "fortnite";
    case ::Launcher::GameId::Rust: return "rust";
    case ::Launcher::GameId::Warzone: return "warzone";
    case ::Launcher::GameId::Apex: return "apex";
    default: return nullptr;
    }
}

std::string Uuid() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    char value[40]{};
    const int length = sprintf_s(value, "%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x",
        guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return length > 0 ? std::string(value, static_cast<std::size_t>(length)) : std::string{};
}

const Update::Json::Object* ParseObject(const Auth::LauncherApiResponse& response, Update::Json::Value& root) {
    if (response.statusCode < 200 || response.statusCode >= 300 || !response.error.empty()) return nullptr;
    Update::Json::Limits limits{24, 128u * 1024u, 6000};
    if (!Update::Json::Parse(response.body, root, limits).ok) return nullptr;
    return Update::Json::AsObject(root);
}

std::string StringField(const Update::Json::Object& object, std::string_view name) {
    const auto* value = Update::Json::Field(object, name);
    const auto* text = value ? Update::Json::AsString(*value) : nullptr;
    return text ? *text : std::string{};
}

std::uint64_t UnsignedField(const Update::Json::Object& object, std::string_view name) {
    const auto* value = Update::Json::Field(object, name);
    double number = 0.0;
    if (!value || !Update::Json::AsDouble(*value, number) || number < 0.0 ||
        number > static_cast<double>((std::numeric_limits<std::uint64_t>::max)())) return 0;
    return static_cast<std::uint64_t>(number);
}

void RefreshSummary(std::string_view accessToken) {
    if (accessToken.empty()) return;
    const auto response = Licensing::UsageRequest("GET", "/api/usage/summary", {}, accessToken);
    Update::Json::Value root;
    const auto* object = ParseObject(response, root);
    if (!object) return;
    const auto* gamesValue = Update::Json::Field(*object, "games");
    const auto* games = gamesValue ? Update::Json::AsArray(*gamesValue) : nullptr;
    if (!games) return;
    std::map<int, Summary> next;
    for (const auto& entry : *games) {
        const auto* row = Update::Json::AsObject(entry);
        if (!row) continue;
        const std::string gameId = StringField(*row, "gameId");
        ::Launcher::GameId id = ::Launcher::GameId::None;
        if (gameId == "fivem") id = ::Launcher::GameId::FiveM;
        else if (gameId == "cs2") id = ::Launcher::GameId::CS2;
        else if (gameId == "fortnite") id = ::Launcher::GameId::Fortnite;
        else if (gameId == "rust") id = ::Launcher::GameId::Rust;
        else if (gameId == "warzone") id = ::Launcher::GameId::Warzone;
        else if (gameId == "apex") id = ::Launcher::GameId::Apex;
        if (id == ::Launcher::GameId::None) continue;
        Summary summary;
        summary.totalActiveSeconds = UnsignedField(*row, "totalActiveSeconds");
        summary.lastSessionSeconds = UnsignedField(*row, "lastSessionSeconds");
        summary.sessionCount = static_cast<std::uint32_t>((std::min<std::uint64_t>)(UnsignedField(*row, "sessionCount"), UINT32_MAX));
        next[static_cast<int>(id)] = summary;
    }
    std::lock_guard lock(g_mutex);
    g_summaries = std::move(next);
}

void Run(std::stop_token stop, ::Launcher::GameId game) {
    const char* gameName = GameName(game);
    const std::string sessionId = Uuid();
    const std::string idempotency = Uuid();
    if (!gameName || sessionId.empty() || idempotency.empty() || stop.stop_requested()) return;
    auto exchange = Licensing::WebsiteRequest("POST", "/api/usage/auth/exchange", "{}");
    Update::Json::Value exchangeRoot;
    const auto* exchangeObject = ParseObject(exchange, exchangeRoot);
    if (!exchangeObject) return;
    std::string token = StringField(*exchangeObject, "accessToken");
    if (token.empty()) return;
    const std::string startBody = "{\"sessionId\":\"" + sessionId + "\",\"gameId\":\"" + gameName +
        "\",\"clientVersion\":\"" + std::string(Version) + "\",\"idempotencyKey\":\"" + idempotency + "\"}";
    const auto started = Licensing::UsageRequest("POST", "/api/usage/sessions", startBody, token);
    if (started.statusCode < 200 || started.statusCode >= 300) return;
    RefreshSummary(token);
    std::mutex waitMutex;
    std::condition_variable_any wake;
    std::unique_lock waitLock(waitMutex);
    auto nextExchange = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (!stop.stop_requested()) {
        if (wake.wait_for(waitLock, stop, std::chrono::seconds(45), [] { return false; })) break;
        if (stop.stop_requested()) break;
        if (std::chrono::steady_clock::now() >= nextExchange) {
            auto renewed = Licensing::WebsiteRequest("POST", "/api/usage/auth/exchange", "{}");
            Update::Json::Value renewedRoot;
            if (const auto* renewedObject = ParseObject(renewed, renewedRoot)) {
                const std::string nextToken = StringField(*renewedObject, "accessToken");
                if (!nextToken.empty()) token = nextToken;
            }
            nextExchange = std::chrono::steady_clock::now() + std::chrono::minutes(10);
        }
        const std::string eventId = Uuid();
        if (!eventId.empty()) (void)Licensing::UsageRequest("PUT", "/api/usage/sessions/" + sessionId +
            "/heartbeat", "{\"eventId\":\"" + eventId + "\"}", token);
    }
    const std::string finishId = Uuid();
    if (!finishId.empty()) (void)Licensing::UsageRequest("POST", "/api/usage/sessions/" + sessionId +
        "/finish", "{\"eventId\":\"" + finishId + "\"}", token);
    RefreshSummary(token);
}

void StopWorker() noexcept {
    std::jthread worker;
    {
        std::lock_guard lock(g_mutex);
        if (!g_worker.joinable()) return;
        g_worker.request_stop();
        worker = std::move(g_worker);
    }
    if (worker.joinable()) worker.join();
}
}

void Begin(::Launcher::GameId game) {
    StopWorker();
    if (!Licensing::IsWebsiteAuthenticated() || !GameName(game)) return;
    std::lock_guard lock(g_mutex);
    g_worker = std::jthread([game](std::stop_token stop) { Run(stop, game); });
}
void Finish(::Launcher::GameId) noexcept { StopWorker(); }
void Refresh() {
    if (!Licensing::IsWebsiteAuthenticated()) return;
    const auto exchange = Licensing::WebsiteRequest("POST", "/api/usage/auth/exchange", "{}");
    Update::Json::Value root;
    const auto* object = ParseObject(exchange, root);
    if (!object) return;
    const std::string token = StringField(*object, "accessToken");
    if (!token.empty()) RefreshSummary(token);
}
void Shutdown() noexcept { StopWorker(); }
bool TryGetSummary(::Launcher::GameId game, Summary& summary) noexcept {
    std::lock_guard lock(g_mutex);
    const auto found = g_summaries.find(static_cast<int>(game));
    if (found == g_summaries.end()) return false;
    summary = found->second;
    return true;
}
}
