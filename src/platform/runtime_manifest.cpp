#include "runtime_manifest.h"
#include "runtime_bootstrap.h"
#include "text_encoding.h"
#include "session_log.h"
#include <fstream>
#include <iomanip>

namespace OmniGhost::Platform {

namespace {
constexpr wchar_t kManifestFileName[] = L"runtime_manifest.json";
constexpr uint32_t kManifestSchemaVersion = 1;
}

std::filesystem::path GetManifestPath() noexcept {
    const auto root = Paths::NativeRuntime();
    return root / kManifestFileName;
}

std::optional<DependencyManifest> LoadManifest(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return std::nullopt;

    std::ifstream file(path);
    if (!file) return std::nullopt;

    try {
        nlohmann::json j;
        file >> j;
        if (!j.contains("schemaVersion") || j["schemaVersion"].get<uint32_t>() != 1) return std::nullopt;

        DependencyManifest manifest;
        manifest.schemaVersion = j["schemaVersion"].get<uint32_t>();
        manifest.createdAt = std::chrono::system_clock::time_point(
            std::chrono::seconds(j["createdAt"].get<int64_t>()));
        manifest.omnitGhostVersion = j["omnitGhostVersion"].get<std::string>();

        for (const auto& entry : j["entries"]) {
            DependencyManifestEntry e;
            e.relativePath = entry["relativePath"].get<std::string>();
            e.kind = static_cast<DependencyKind>(entry["kind"].get<uint8_t>());
            e.origin = static_cast<DependencyOrigin>(entry["origin"].get<uint8_t>());
            e.sizeBytes = entry["sizeBytes"].get<uint64_t>();
            e.sha256 = entry["sha256"].get<std::string>();
            e.version = entry.value("version", "");
            e.reason = entry.value("reason", "");
            e.materializedAt = std::chrono::system_clock::time_point(
                std::chrono::seconds(entry["materializedAt"].get<int64_t>()));
            e.isOptional = entry.value("isOptional", false);
            e.isLazy = entry.value("isLazy", false);
            manifest.entries.push_back(e);
        }
        return manifest;
    } catch (...) {
        return std::nullopt;
    }
}

bool SaveManifest(const DependencyManifest& manifest, const std::filesystem::path& path) noexcept {
    nlohmann::json j;
    j["schemaVersion"] = manifest.schemaVersion;
    j["createdAt"] = std::chrono::duration_cast<std::chrono::seconds>(
        manifest.createdAt.time_since_epoch()).count();
    j["omnitGhostVersion"] = manifest.omnitGhostVersion;
    j["entries"] = nlohmann::json::array();

    for (const auto& e : manifest.entries) {
        nlohmann::json entry;
        entry["relativePath"] = e.relativePath;
        entry["kind"] = static_cast<uint8_t>(e.kind);
        entry["origin"] = static_cast<uint8_t>(e.origin);
        entry["sizeBytes"] = e.sizeBytes;
        entry["sha256"] = e.sha256;
        entry["version"] = e.version;
        entry["reason"] = e.reason;
        entry["materializedAt"] = std::chrono::duration_cast<std::chrono::seconds>(
            e.materializedAt.time_since_epoch()).count();
        entry["isOptional"] = e.isOptional;
        entry["isLazy"] = e.isLazy;
        j["entries"].push_back(entry);
    }

    const auto tempPath = path.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    try {
        std::ofstream out(tempPath);
        out << j.dump(2);
        out.flush();
        if (!out) return false;
        out.close();
    } catch (...) {
        return false;
    }

    std::error_code ec;
    if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }
    return true;
}

std::filesystem::path GetManifestPath() noexcept {
    return Paths::NativeRuntime() / L"runtime_manifest.json";
}

bool RegisterDependency(
    const std::string& relativePath,
    DependencyKind kind,
    DependencyOrigin origin,
    uint64_t sizeBytes,
    std::string_view sha256,
    std::string_view version,
    std::string_view reason,
    bool isOptional,
    bool isLazy
) noexcept {
    auto manifest = LoadManifest(GetManifestPath()).value_or(DependencyManifest{});
    if (manifest.entries.empty()) {
        manifest.createdAt = std::chrono::system_clock::now();
        manifest.omnitGhostVersion = app_settings::config.version;
    }

    DependencyManifestEntry entry;
    entry.relativePath = relativePath;
    entry.kind = kind;
    entry.origin = origin;
    entry.sizeBytes = sizeBytes;
    entry.sha256 = std::string(sha256);
    entry.version = std::string(version);
    entry.reason = std::string(reason);
    entry.materializedAt = std::chrono::system_clock::now();
    entry.isOptional = isOptional;
    entry.isLazy = isLazy;

    manifest.Upsert(entry);
    return SaveManifest(manifest, GetManifestPath());
}

bool ValidateDependency(const DependencyManifestEntry& entry) noexcept {
    if (entry.isLazy) return true;

    const auto target = Paths::NativeRuntime() / entry.relativePath;
    std::error_code ec;

    if (!std::filesystem::exists(target, ec) || ec) return false;
    if (!std::filesystem::is_regular_file(target, ec)) return false;

    DWORD attrs = GetFileAttributesW((Paths::NativeRuntime() / entry.relativePath).c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;

    if (entry.sha256.empty()) return true;

    std::string actual;
    std::string hashError;
    if (!Platform::Sha256File(Paths::NativeRuntime() / entry.relativePath, actual, ec) ||
        !Platform::ConstantTimeEquals(actual, entry.sha256)) {
        return false;
    }
    return true;
}

bool PruneStaleDependencies(const DependencyManifest& manifest) noexcept {
    const auto root = Paths::NativeRuntime();
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        const auto rel = std::filesystem::relative(entry.path(), root);
        const std::string relStr = rel.string();

        if (!manifest.Find(relStr)) {
            std::error_code ec2;
            std::filesystem::remove(entry.path(), ec2);
            LogDependencyAction(relStr, "pruned", "not in manifest");
        }
    }
    return true;
}

void LogDependencyAction(
    const std::string& relativePath,
    const std::string& action,
    const std::string& detail
) noexcept {
    OmniGhost::SessionLog::Write(
        SessionLog::Severity::Info,
        SessionLog::Subsystem::Core,
        "dependency " + action,
        {
            {"path", relativePath, false},
            {"action", action, false},
            {"detail", detail, false}
        });
}

} // namespace OmniGhost::Platform