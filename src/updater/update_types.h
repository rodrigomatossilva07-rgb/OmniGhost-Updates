#pragma once
#include "installation_integrity.h"
#include "release_trust.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace OmniGhost::Update {
enum class Channel { Stable, Beta, Development };
enum class Status { Idle, Checking, UpToDate, Available, Downloading, Ready, Installing, Deferred, Completed, RolledBack, Error };

struct SemVersion {
    std::uint64_t major{}, minor{}, patch{};
    std::vector<std::string> prerelease;
    std::string original;
    static std::optional<SemVersion> Parse(std::string value);
    int Compare(const SemVersion& other) const;
    bool IsPrerelease() const { return !prerelease.empty(); }
};

struct Package {
    std::string platform;
    std::string architecture;
    std::string fileName;
    std::string url;
    std::uint64_t size{};
    std::string sha256;
};

struct Manifest {
    int schemaVersion{};
    std::string appId;
    Channel channel{Channel::Stable};
    SemVersion version;
    SemVersion minimumSupportedVersion;
    bool mandatory{};
    std::string mandatoryReason;
    std::string publishedAt;
    std::string releaseNotesUrl;
    std::vector<Package> packages;
};

struct Configuration {
    // Primary source: the public Releases API. The updater selects the highest
    // published stable SemVer release instead of trusting GitHub's mutable
    // "Latest" marker. manifestUrl remains a compatibility fallback.
    std::string releasesApiUrl = "https://api.github.com/repos/rodrigomatossilva07-rgb/OmniGhost-Updates/releases?per_page=30";
    std::string manifestUrl = "https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest/download/update.json";
    std::string expectedAppId = "com.omnighost.launcher";
    std::string allowedPackageUrlPrefix = "https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/download/";
    Channel channel = Channel::Stable;
    std::chrono::seconds networkTimeout{25};
    std::chrono::hours minimumCheckInterval{6};
    std::uint64_t maximumPackageBytes = 512ull * 1024ull * 1024ull;
    std::uint64_t maximumExtractedBytes = 1536ull * 1024ull * 1024ull;
    std::uint32_t maximumArchiveEntries = 20000;
    bool automaticDownload = false;
    bool automaticInstallOnExit = false;
    bool requireAuthenticode = ReleaseTrust::Configured;
    bool requireManifestSignature = ReleaseTrust::Configured;
    std::string expectedPublisherSubject = ReleaseTrust::PublisherSubject;
    std::string expectedCertificateThumbprint = ReleaseTrust::CertificateThumbprint;
    std::string manifestPublicCertificateDerBase64 = ReleaseTrust::PublicCertificateDerBase64;
    std::string secondaryPublisherSubject = ReleaseTrust::SecondaryPublisherSubject;
    std::string secondaryCertificateThumbprint = ReleaseTrust::SecondaryCertificateThumbprint;
    std::string secondaryManifestPublicCertificateDerBase64 = ReleaseTrust::SecondaryPublicCertificateDerBase64;
    int updaterExitTimeoutSeconds = 30;
    int startupConfirmationSeconds = 30;
    int backupsToKeep = 3;
};

struct Snapshot {
    Status status{Status::Idle};
    std::string installedVersion;
    std::string availableVersion;
    std::string releaseNotesUrl;
    std::string userMessage;
    std::string errorTitle;
    std::string errorStage;
    std::string technicalDetail;
    bool mandatory{};
    std::string mandatoryReason;
    bool installOnExit{};
    bool repairMode{};
    bool recentlyUpdated{};
    bool rollbackRestored{};
    std::string updateFromVersion;
    std::string releaseNotesForCompletedUpdate;
    IntegrityStatus integrityStatus{IntegrityStatus::Unknown};
    std::size_t integrityCheckedFiles{};
    std::size_t integrityFailedFiles{};
    std::string integrityMessage;
    std::uint64_t bytesReceived{};
    std::uint64_t bytesTotal{};
    double bytesPerSecond{};
};

struct ManifestResult {
    std::optional<Manifest> manifest;
    std::string error;
};

const char* ChannelName(Channel channel);
std::optional<Channel> ParseChannel(const std::string& value);
ManifestResult ParseAndValidateManifest(const std::string& json, const Configuration& config,
                                        const std::string& architecture = "x64");
const Package* SelectPackage(const Manifest& manifest, const std::string& architecture = "x64");
}
