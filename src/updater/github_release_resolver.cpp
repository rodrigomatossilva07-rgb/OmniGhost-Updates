#include "github_release_resolver.h"
#include "json.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

namespace OmniGhost::Update {
namespace {

constexpr std::size_t kMaximumResponseBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaximumReleaseCount = 100;
constexpr std::size_t kMaximumAssetCount = 128;
constexpr std::size_t kMaximumStringLength = 4096;
constexpr std::string_view kReleaseDownloadPrefix =
    "https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/download/";
constexpr std::string_view kReleasePagePrefix =
    "https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/tag/";

const Json::Value* Field(const Json::Object& object, const char* name) {
    return Json::Field(object, name);
}

bool Text(const Json::Object& object, const char* name, std::string& output) {
    const Json::Value* field = Field(object, name);
    const std::string* value = field ? Json::AsString(*field) : nullptr;
    if (!value || value->empty() || value->size() > kMaximumStringLength) return false;
    output = *value;
    return true;
}

bool Boolean(const Json::Object& object, const char* name, bool& output) {
    const Json::Value* field = Field(object, name);
    const bool* value = field ? Json::AsBool(*field) : nullptr;
    if (!value) return false;
    output = *value;
    return true;
}

bool StartsWith(const std::string& value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0;
}

bool EndsWith(const std::string& value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix.data(), suffix.size()) == 0;
}

bool SafeAssetUrl(const std::string& url, const std::string& tag,
                  std::string_view fileName) {
    if (!StartsWith(url, kReleaseDownloadPrefix) ||
        !EndsWith(url, std::string("/") + std::string(fileName)) ||
        url.find('\r') != std::string::npos ||
        url.find('\n') != std::string::npos ||
        url.find('\\') != std::string::npos) return false;
    return StartsWith(url, std::string(kReleaseDownloadPrefix) + tag + "/");
}

bool SafeReleaseUrl(const std::string& url, const std::string& tag) {
    return StartsWith(url, kReleasePagePrefix) && EndsWith(url, "/" + tag) &&
           url.find('\r') == std::string::npos && url.find('\n') == std::string::npos &&
           url.find('\\') == std::string::npos;
}

} // namespace

GitHubReleaseResult ResolveHighestStableGitHubRelease(const std::string& json) {
    auto fail = [](std::string message) {
        return GitHubReleaseResult{std::nullopt, std::move(message)};
    };

    if (json.empty() || json.size() > kMaximumResponseBytes) {
        return fail("A resposta da API de versões está vazia ou excede o limite permitido.");
    }

    Json::Value root;
    Json::Limits limits;
    limits.maximumDepth = 32;
    limits.maximumStringBytes = kMaximumStringLength;
    limits.maximumNodes = 50000;
    const Json::ParseResult parsed = Json::Parse(json, root, limits);
    if (!parsed.ok) {
        return fail("A resposta da API de versões não contém JSON válido.");
    }

    const Json::Array* releases = Json::AsArray(root);
    if (!releases || releases->size() > kMaximumReleaseCount) {
        return fail("A API de versões não devolveu uma lista válida.");
    }

    std::optional<GitHubReleaseSelection> best;
    std::optional<SemVersion> highestPublishedVersion;
    std::string highestPublishedTag;

    for (const Json::Value& releaseValue : *releases) {
        const Json::Object* release = Json::AsObject(releaseValue);
        if (!release) continue;

        bool draft = false;
        bool prerelease = false;
        std::string tag;
        std::string releaseUrl;

        if (!Boolean(*release, "draft", draft) ||
            !Boolean(*release, "prerelease", prerelease) ||
            !Text(*release, "tag_name", tag) ||
            draft || prerelease) {
            continue;
        }

        const auto parsedVersion = SemVersion::Parse(tag);
        if (!parsedVersion || parsedVersion->IsPrerelease()) {
            continue;
        }

        if (!Text(*release, "html_url", releaseUrl) ||
            !SafeReleaseUrl(releaseUrl, tag)) {
            continue;
        }

        if (!highestPublishedVersion ||
            parsedVersion->Compare(*highestPublishedVersion) > 0) {
            highestPublishedVersion = *parsedVersion;
            highestPublishedTag = tag;
        }

        const Json::Value* assetsField = Field(*release, "assets");
        const Json::Array* assets = assetsField ? Json::AsArray(*assetsField) : nullptr;
        if (!assets || assets->size() > kMaximumAssetCount) {
            continue;
        }

        std::string manifestUrl;
        std::string changelogUrl;
        for (const Json::Value& assetValue : *assets) {
            const Json::Object* asset = Json::AsObject(assetValue);
            if (!asset) continue;

            std::string name;
            std::string downloadUrl;
            if (!Text(*asset, "name", name) ||
                !Text(*asset, "browser_download_url", downloadUrl)) {
                continue;
            }

            if (name == "update.json" &&
                SafeAssetUrl(downloadUrl, tag, "update.json")) {
                manifestUrl = downloadUrl;
            } else if (name == "changelog.json" &&
                       SafeAssetUrl(downloadUrl, tag, "changelog.json")) {
                changelogUrl = downloadUrl;
            }
        }

        if (manifestUrl.empty()) {
            continue;
        }

        GitHubReleaseSelection candidate;
        candidate.version = *parsedVersion;
        candidate.tag = tag;
        candidate.updateManifestUrl = std::move(manifestUrl);
        candidate.changelogUrl = std::move(changelogUrl);
        candidate.releaseUrl = std::move(releaseUrl);

        if (!best || candidate.version.Compare(best->version) > 0) {
            best = std::move(candidate);
        }
    }

    if (!highestPublishedVersion) {
        return fail("Não foi encontrada nenhuma versão estável publicada.");
    }

    if (!best || best->version.Compare(*highestPublishedVersion) != 0) {
        return fail(
            "A versão estável mais recente publicada (" + highestPublishedTag +
            ") não contém um asset update.json válido.");
    }

    return GitHubReleaseResult{std::move(best), {}};
}

} // namespace OmniGhost::Update
