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
    std::wstring executableName;



    std::filesystem::path backupDirectory;

    std::string expectedExecutableSha256;
    std::function<bool(const std::string& stage)> failureInjector;
};
struct InstallResult { bool success{}; std::string error; };
InstallResult InstallSingleExecutable(const InstallOptions& options);
bool HasEnoughDiskSpace(const std::filesystem::path& path, std::uint64_t requiredBytes, std::string& error);



bool RestrictPathToCurrentUser(const std::filesystem::path& path, bool directory, std::string& error);


bool SafeRelative(const std::filesystem::path& path);
}
