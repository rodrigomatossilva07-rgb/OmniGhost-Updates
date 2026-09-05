#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace OmniGhost::Licensing {

enum class LocalState {
    Unknown,
    Missing,
    Valid,
    Invalid,
    StorageError
};

struct Snapshot {
    LocalState localState = LocalState::Unknown;
    bool localLicenseValid = false;
    bool protectedStorage = false;
    bool remoteServiceConfigured = false;
    std::filesystem::path storagePath;
    std::string userMessage;
};

// Keeps the current offline/local activation flow working until a VPS/API is configured.
// The accepted verifier is stored as SHA-256 only; the per-user cached key is protected
// with Windows DPAPI. This remains a local gate, not strong server-side licensing.
[[nodiscard]] bool EnsureInteractive(); // compatibility: non-interactive status check
[[nodiscard]] bool ActivateLocalKey(std::string_view key, std::string* userMessage = nullptr);
// Temporary local bootstrap used while the commercial provider is not connected.
// It is currently available in Release, Tester and Publish at the project owner's
// explicit request. Remove this API together with its UI before customer rollout.
[[nodiscard]] bool CreateTemporaryLocalLicense(std::string* userMessage = nullptr);
// Central game-access gate. The current offline compatibility license grants
// every integrated product. A future KeyAuth-backed implementation will return
// independent entitlements and expirations without changing launcher code.
[[nodiscard]] bool HasGameAccess(std::string_view productId);
[[nodiscard]] bool HasAnyGameAccess();
[[nodiscard]] Snapshot GetSnapshot();
void Refresh();
[[nodiscard]] const char* StateLabel(LocalState state) noexcept;

} // namespace OmniGhost::Licensing
