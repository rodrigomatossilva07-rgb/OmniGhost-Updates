#pragma once
#include "update_types.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace OmniGhost::Update {
struct ArchiveInfo { std::uint32_t entries{}; std::uint64_t totalUncompressed{}; std::vector<std::filesystem::path> files; };
bool ValidateZip(const std::filesystem::path& package, std::uint32_t maximumEntries,
                 std::uint64_t maximumExtractedBytes, ArchiveInfo& info, std::string& error);
bool ExtractZipSecurely(const std::filesystem::path& package, const std::filesystem::path& staging,
                        const Configuration& config, ArchiveInfo& info, std::string& error);

struct InstallOptions {
    std::filesystem::path staging;
    std::filesystem::path target;
    std::filesystem::path backup;
    std::wstring executableName;
    std::function<bool(const std::string& stage)> failureInjector;
};
struct InstallResult { bool success{}; bool rolledBack{}; std::string error; };
InstallResult InstallWithRollback(const InstallOptions& options);
bool RollbackInstallation(const InstallOptions& options, std::string& error);
bool HasEnoughDiskSpace(const std::filesystem::path& path, std::uint64_t requiredBytes, std::string& error);
// Applies a protected DACL granting full access only to SYSTEM, Administrators and
// the current user. Directories use inheritable ACEs so extracted children keep
// the same trust boundary.
bool RestrictPathToCurrentUser(const std::filesystem::path& path, bool directory, std::string& error);
void PruneBackups(const std::filesystem::path& backupRoot, int backupsToKeep);

// Validates that a path is safe for archive extraction (no traversal, reserved names, etc.)
bool SafeRelative(const std::filesystem::path& path);
}
