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



GitHubReleaseResult ResolveHighestStableGitHubRelease(const std::string& json);

}
