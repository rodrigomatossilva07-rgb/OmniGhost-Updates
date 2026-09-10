#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "../auth/keyauth_gateway.h"

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
    bool remoteAuthenticated = false;
    std::string remoteUsername;
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
// KeyAuth account and activation flows. UI code calls these service functions;
// the official SDK remains isolated behind LicenseGateway.
[[nodiscard]] OmniGhost::Auth::LicenseResult Login(std::string_view username, std::string_view password);
[[nodiscard]] OmniGhost::Auth::LicenseResult Register(std::string_view username, std::string_view password,
                                                       std::string_view licenseKey);
[[nodiscard]] OmniGhost::Auth::LicenseResult ActivateKey(std::string_view licenseKey);
[[nodiscard]] OmniGhost::Auth::LicenseResult Upgrade(std::string_view username, std::string_view licenseKey);
void LogoutRemote() noexcept;
[[nodiscard]] bool IsRemoteConfigured() noexcept;
[[nodiscard]] bool IsRemoteAuthenticated() noexcept;
[[nodiscard]] std::string RemoteUsername();
// Central game-access gate. A valid KeyAuth session grants the currently
// integrated products; the development fallback keeps local activation only
// when OMNIGHOST_SKIP_KEYAUTH is enabled.
[[nodiscard]] bool HasGameAccess(std::string_view productId);
[[nodiscard]] bool HasAnyGameAccess();
[[nodiscard]] Snapshot GetSnapshot();
void Refresh();
[[nodiscard]] const char* StateLabel(LocalState state) noexcept;

} // namespace OmniGhost::Licensing
