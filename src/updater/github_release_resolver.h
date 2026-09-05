#pragma once

#include "update_types.h"

#include <optional>
#include <string>

namespace OmniGhost::Update {

struct GitHubReleaseSelection {
    SemVersion version;
    std::string tag;
    std::string updateManifestUrl;
    std::string changelogUrl;
    std::string releaseUrl;
};

struct GitHubReleaseResult {
    std::optional<GitHubReleaseSelection> selection;
    std::string error;
};

// Parses the public GitHub Releases API response and selects the highest
// published, non-prerelease semantic version that contains update.json.
GitHubReleaseResult ResolveHighestStableGitHubRelease(const std::string& json);

} // namespace OmniGhost::Update
