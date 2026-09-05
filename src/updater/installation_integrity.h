#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace OmniGhost::Update {
enum class IntegrityStatus { Unknown, Checking, Healthy, Damaged, ManifestMissing, Error };

struct IntegrityIssue {
    std::string relativePath;
    std::string reason;
};

struct IntegrityReport {
    IntegrityStatus status{IntegrityStatus::Unknown};
    std::size_t checkedFiles{};
    std::size_t failedFiles{};
    std::string message;
    std::vector<IntegrityIssue> issues;
};

IntegrityReport VerifyInstallationFiles(const std::filesystem::path& installDirectory,
                                        std::size_t maximumIssues = 32);
const char* IntegrityStatusName(IntegrityStatus status);
}
