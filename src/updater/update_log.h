#pragma once
#include <filesystem>
#include <string>

namespace OmniGhost::Update {
enum class LogLevel { Info, Warning, Error };

struct UpdateHealthRecord {
    std::string stage;
    std::string installedVersion;
    std::string targetVersion;
    std::string packageSha256;
    std::string signerStatus;
    std::string rollbackResult;
    std::string detail;
};

void WriteLog(const std::filesystem::path& file, LogLevel level, const std::string& currentVersion,
              const std::string& targetVersion, const std::string& stage, const std::string& message);
bool WriteUpdateHealth(const std::filesystem::path& file, const UpdateHealthRecord& record);
void RotateLogs(const std::filesystem::path& directory, int maximumFiles = 10, int maximumAgeDays = 30);
std::string WindowsError(unsigned long code);
}
