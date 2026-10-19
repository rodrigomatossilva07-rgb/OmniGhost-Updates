#include "offset_source.h"

#include "app_paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace OmniGhost::OffsetSource {
namespace {



bool ReadFileUtf8(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty();
}

void SkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
}

bool Match(const std::string& s, size_t& i, char c) {
    SkipWs(s, i);
    if (i < s.size() && s[i] == c) { ++i; return true; }
    return false;
}

bool ParseString(const std::string& s, size_t& i, std::string& out) {
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out.push_back(s[i + 1]);
            i += 2;
            continue;
        }
        out.push_back(s[i++]);
    }
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    return true;
}

bool ParseHexU64(const std::string& text, std::uint64_t& value) {
    if (text.size() < 3 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) return false;
    try {
        value = std::stoull(text, nullptr, 16);
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[Offsets] ParseHexU64 failed for '" << text << "': " << ex.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[Offsets] ParseHexU64 unknown exception for '" << text << "'\n";
        return false;
    }
}

bool ParseValue(const std::string& s, size_t& i, const std::string& path,
                EmbeddedOffsets::Snapshot& snap);

bool ParseObject(const std::string& s, size_t& i, const std::string& path,
                 EmbeddedOffsets::Snapshot& snap) {
    if (!Match(s, i, '{')) return false;
    SkipWs(s, i);
    if (Match(s, i, '}')) return true;
    for (;;) {
        std::string key;
        if (!ParseString(s, i, key)) return false;
        if (!Match(s, i, ':')) return false;
        const std::string child = path.empty() ? key : (path + "." + key);

        if (key == "schema_version" || key == "game" || key == "build" || key == "cl" ||
            key == "module" || key == "source" || key == "generated_at" || key == "note" ||
            key == "version" || key == "build_id" || key == "label" || key == "updated_at" ||
            key == "verified_utc" || key == "build_note" || key == "id" || key == "game_slug") {
            SkipWs(s, i);
            if (i < s.size() && s[i] == '"') {
                std::string str;
                if (!ParseString(s, i, str)) return false;
                if (key == "game") snap.metadata.game = str;
                else if (key == "build" || key == "version" || key == "build_id") {
                    if (snap.metadata.build.empty() || key == "build") snap.metadata.build = str;
                } else if (key == "cl") snap.metadata.cl = str;
                else if (key == "module") snap.metadata.module = str;
                else if (key == "source") snap.metadata.source = str;
                else if (key == "generated_at" || key == "updated_at" || key == "verified_utc") {
                    if (snap.metadata.generatedAt.empty()) snap.metadata.generatedAt = str;
                } else if (key == "note" || key == "build_note" || key == "label") {
                    if (snap.metadata.note.empty()) snap.metadata.note = str;
                }
            } else if (key == "schema_version") {






                SkipWs(s, i);
                size_t j = i;
                while (j < s.size() && (isdigit((unsigned char)s[j]))) ++j;
                const size_t digitCount = j - i;
                if (digitCount > 0 && digitCount <= 10) {
                    std::uint32_t value = 0;
                    bool overflow = false;
                    for (size_t k = i; k < j; ++k) {
                        const std::uint32_t digit =
                            static_cast<std::uint32_t>(s[k] - '0');
                        if (value > (0xFFFFFFFFull - digit) / 10ull) {
                            overflow = true;
                            break;
                        }
                        value = value * 10ull + digit;
                    }
                    if (!overflow) {
                        snap.metadata.schemaVersion = value;
                        i = j;
                    }
                }
            } else {

                if (!ParseValue(s, i, child, snap)) {

                    int depth = 0;
                    while (i < s.size()) {
                        if (s[i] == '{' || s[i] == '[') ++depth;
                        else if (s[i] == '}' || s[i] == ']') {
                            if (depth == 0) break;
                            --depth;
                        } else if (s[i] == ',' && depth == 0) break;
                        ++i;
                    }
                }
            }
        } else {
            if (!ParseValue(s, i, child, snap)) return false;
        }
        SkipWs(s, i);
        if (Match(s, i, ',')) continue;
        if (Match(s, i, '}')) return true;
        return false;
    }
}

bool ParseValue(const std::string& s, size_t& i, const std::string& path,
                EmbeddedOffsets::Snapshot& snap) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    if (s[i] == '{') return ParseObject(s, i, path, snap);
    if (s[i] == '[') {

        int depth = 0;
        do {
            if (s[i] == '[') ++depth;
            else if (s[i] == ']') --depth;
            ++i;
        } while (i < s.size() && depth > 0);
        return depth == 0;
    }
    if (s[i] == '"') {
        std::string str;
        if (!ParseString(s, i, str)) return false;
        std::uint64_t value = 0;
        if (ParseHexU64(str, value)) {
            EmbeddedOffsets::Entry e{};
            e.keyHash = EmbeddedOffsets::HashOffsetPath(path);
            e.value = value;
            snap.entries.push_back(e);
        }
        return true;
    }

    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
    return true;
}

bool ParseExternalJsonToSnapshot(const std::string& json, EmbeddedOffsets::Snapshot& snap) {
    snap = {};
    size_t i = 0;
    if (!ParseObject(json, i, "", snap)) return false;
    if (snap.metadata.schemaVersion == 0) snap.metadata.schemaVersion = 1;
    if (snap.metadata.game.empty()) snap.metadata.game = "unknown";
    if (snap.metadata.build.empty()) snap.metadata.build = "unknown";
    if (snap.metadata.module.empty()) snap.metadata.module = "unknown";
    if (snap.metadata.source.empty()) snap.metadata.source = "external-json";

    std::sort(snap.entries.begin(), snap.entries.end(),
              [](const EmbeddedOffsets::Entry& a, const EmbeddedOffsets::Entry& b) {
                  return a.keyHash < b.keyHash;
              });

    std::vector<EmbeddedOffsets::Entry> unique;
    unique.reserve(snap.entries.size());
    for (const auto& e : snap.entries) {
        if (!unique.empty() && unique.back().keyHash == e.keyHash) continue;
        unique.push_back(e);
    }
    snap.entries.swap(unique);
    return !snap.entries.empty();
}

}

EmbeddedOffsets::Game ToEmbeddedGame(GameId id) noexcept {
    switch (id) {
    case GameId::Fortnite: return EmbeddedOffsets::Game::Fortnite;
    case GameId::Warzone: return EmbeddedOffsets::Game::Warzone;
    case GameId::CS2: return EmbeddedOffsets::Game::CS2;
    case GameId::FiveM: return EmbeddedOffsets::Game::FiveM;
    case GameId::Apex: return EmbeddedOffsets::Game::Apex;
    default: return EmbeddedOffsets::Game::Fortnite;
    }
}

const char* GameSlug(GameId id) noexcept {
    switch (id) {
    case GameId::Fortnite: return "fortnite";
    case GameId::Warzone: return "warzone";
    case GameId::CS2: return "cs2";
    case GameId::FiveM: return "fivem";
    case GameId::Apex: return "apex";
    default: return "unknown";
    }
}

const char* CanonicalJsonName(GameId id) noexcept {
    switch (id) {
    case GameId::Fortnite: return "fortnite_offsets.json";
    case GameId::Warzone: return "warzone_offsets.json";
    case GameId::CS2: return "cs2_offsets.json";
    case GameId::FiveM: return "fivem_offsets.json";
    case GameId::Apex: return "apex_offsets.json";
    default: return "offsets.json";
    }
}

std::vector<std::string> ExternalJsonCandidates(GameId id) {
    std::vector<std::string> out;
    namespace fs = std::filesystem;
    const char* name = CanonicalJsonName(id);
    auto add = [&](const fs::path& p) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) out.push_back(p.string());
    };

    try {
        const fs::path exeDir = Paths::Executable().parent_path();
        add(exeDir / "data" / name);
    } catch (const std::exception& ex) {
        std::cerr << "[OmniGhost Offsets] Failed to resolve candidates beside EXE: " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "[OmniGhost Offsets] Unknown exception resolving candidates beside EXE\n";
    }

    add(fs::path("data") / name);
    return out;
}

LoadResult LoadSnapshot(GameId id, const char* explicit_path) {
    LoadResult result{};
    result.storage = "FAIL";

#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
    std::vector<std::string> candidates;
    if (explicit_path && explicit_path[0]) {
        const std::filesystem::path requested(explicit_path);
        if (requested.parent_path().filename() == "data" &&
            requested.filename() == CanonicalJsonName(id))
            candidates.emplace_back(requested.string());
    } else {
        candidates = ExternalJsonCandidates(id);
    }

    for (const auto& path : candidates) {
        std::string json;
        if (!ReadFileUtf8(path, json)) continue;
        EmbeddedOffsets::Snapshot snap;
        if (!ParseExternalJsonToSnapshot(json, snap)) continue;

        if (!snap.metadata.game.empty() &&
            snap.metadata.game != GameSlug(id) &&
            snap.metadata.game != "unknown") {

            if (!(id == GameId::CS2 && (snap.metadata.game == "cs2" || snap.metadata.game.empty())))
                continue;
        }
        result.ok = true;
        result.from_external_json = true;
        result.storage = "EXTERNAL_JSON";
        result.detail = path;
        result.snapshot = std::move(snap);
        result.diagnostics.resourceFound = true;
        result.diagnostics.parsed = true;






        result.diagnostics.integrityValid = false;
        LogLoadResult(id, result);
        return result;
    }
#else
    (void)explicit_path;
#endif

    const bool resourceOk = EmbeddedOffsets::Load(ToEmbeddedGame(id), result.snapshot, result.diagnostics);
    result.ok = resourceOk;
    result.from_external_json = false;
    result.storage = resourceOk ? "EMBEDDED" : "FAIL";
    result.detail = resourceOk ? "rcdata" : result.diagnostics.error;
    LogLoadResult(id, result);
    return result;
}

bool TryGetAny(const EmbeddedOffsets::Snapshot& snapshot,
               std::initializer_list<const char*> keys,
               std::uint64_t& value) noexcept {
    for (const char* key : keys) {
        if (key && snapshot.TryGet(key, value))
            return true;
    }
    return false;
}

void LogLoadResult(GameId id, const LoadResult& result) {
    std::clog << "[OFFSETS] storage=" << result.storage << "\n"
              << "[OFFSETS] game=" << GameSlug(id) << "\n";
    if (result.ok) {
        std::clog << "[OFFSETS] build=" << result.snapshot.metadata.build << "\n";
        if (!result.snapshot.metadata.cl.empty())
            std::clog << "[OFFSETS] cl=" << result.snapshot.metadata.cl << "\n";
        std::clog << "[OFFSETS] entries=" << result.snapshot.entries.size() << "\n"
                  << "[OFFSETS] detail=" << result.detail << "\n"
                  << "[OFFSETS] load=PASS\n";
    } else {
        std::clog << "[OFFSETS] load=FAIL detail=" << result.detail << "\n";
    }
}

}
