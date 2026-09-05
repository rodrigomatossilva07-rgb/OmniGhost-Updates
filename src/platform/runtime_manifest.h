#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace OmniGhost::Platform {

// Item 43: Manifest único e versionado para dependências materializadas
// Item 44: Guardar versão, tamanho, hash, origem, motivo

enum class DependencyOrigin : uint8_t {
    Embedded = 0,      // From PE resource
    ExternalJson = 1,  // From data/offsets.json
    Fallback = 2,      // Built-in defaults
    UserProvided = 3   // User placed file
};

enum class DependencyKind : uint8_t {
    DmaDriver = 0,      // vmm.dll, leechcore.dll, FTD3XXWU.dll
    DmaPlugin = 1,      // VMM plugins
    Symbols = 2,        // pdbcrust.dll
    OffsetData = 3,     // JSON offset files
    Cloudflared = 4,    // cloudflared.exe
    ConfigFile = 5,     // settings, license, auth
    LogFile = 6,        // logs
    Cache = 7           // Cached data
};

struct DependencyManifestEntry {
    std::string relativePath;           // e.g., "libs/vmm.dll"
    DependencyKind kind;
    DependencyOrigin origin;
    uint64_t sizeBytes = 0;
    std::string sha256;                 // Hex SHA-256
    std::string version;                // Version string if available
    std::string reason;                 // Why this dependency exists
    std::chrono::system_clock::time_point materializedAt;
    bool isOptional = false;
    bool isLazy = false;                // Materialized on-demand
};

struct DependencyManifest {
    uint32_t schemaVersion = 1;
    std::chrono::system_clock::time_point createdAt;
    std::string omnitGhostVersion;
    std::vector<DependencyManifestEntry> entries;
    
    // Find entry by path
    [[nodiscard]] const DependencyManifestEntry* Find(const std::string& relativePath) const noexcept {
        for (const auto& e : entries) {
            if (e.relativePath == relativePath) return &e;
        }
        return nullptr;
    }
    
    // Add or update entry
    void Upsert(const DependencyManifestEntry& entry) {
        auto it = std::find_if(entries.begin(), entries.end(),
            [&](const auto& e) { return e.relativePath == entry.relativePath; });
        if (it != entries.end()) *it = entry;
        else entries.push_back(entry);
    }
    
    // Remove entry
    void Remove(const std::string& relativePath) {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [&](const auto& e) { return e.relativePath == relativePath; }), entries.end());
    }
};

// Load manifest from file
[[nodiscard]] std::optional<DependencyManifest> LoadManifest(const std::filesystem::path& path) noexcept;

// Save manifest to file (atomic write)
[[nodiscard]] bool SaveManifest(const DependencyManifest& manifest, const std::filesystem::path& path) noexcept;

// Get manifest path
[[nodiscard]] std::filesystem::path GetManifestPath() noexcept;

// Register a materialized dependency
[[nodiscard]] bool RegisterDependency(
    const std::string& relativePath,
    DependencyKind kind,
    DependencyOrigin origin,
    uint64_t sizeBytes,
    std::string_view sha256,
    std::string_view version,
    std::string_view reason,
    bool isOptional = false,
    bool isLazy = false
) noexcept;

// Check if dependency is still valid (exists, correct hash, not a reparse point)
[[nodiscard]] bool ValidateDependency(const DependencyManifestEntry& entry) noexcept;

// Remove stale dependencies (not in current manifest)
[[nodiscard]] bool PruneStaleDependencies(const DependencyManifest& manifest) noexcept;

// Log dependency action (found, validated, materialized, rejected)
void LogDependencyAction(
    const std::string& relativePath,
    const std::string& action,  // "found", "validated", "materialized", "rejected", "pruned"
    const std::string& detail = ""
) noexcept;

} // namespace OmniGhost::Platform