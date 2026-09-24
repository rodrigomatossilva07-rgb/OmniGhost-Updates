#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../auth/keyauth_gateway.h"

namespace OmniGhost::Licensing {

struct Snapshot {
    bool remoteServiceConfigured = false;
    bool remoteAuthenticated = false;
    std::string remoteUsername;
    std::vector<OmniGhost::Auth::RemoteEntitlement> remoteEntitlements;
};

// KeyAuth account and activation flows. UI code calls these service functions;
// the official SDK remains isolated behind LicenseGateway.
[[nodiscard]] OmniGhost::Auth::LicenseResult Login(std::string_view username, std::string_view password);
[[nodiscard]] OmniGhost::Auth::LicenseResult Register(std::string_view username, std::string_view password,
                                                       std::string_view licenseKey);
[[nodiscard]] OmniGhost::Auth::LicenseResult ActivateKey(std::string_view licenseKey);
[[nodiscard]] OmniGhost::Auth::LicenseResult Upgrade(std::string_view username, std::string_view licenseKey);
void LogoutRemote() noexcept;
[[nodiscard]] bool IsRemoteConfigured() noexcept;
// Remember-me for KeyAuth login: credentials are DPAPI-protected under Configs/.
[[nodiscard]] bool SaveRememberedRemoteCredentials(std::string_view username, std::string_view password);
[[nodiscard]] bool LoadRememberedRemoteCredentials(std::string& username, std::string& password);
void ClearRememberedRemoteCredentials() noexcept;
[[nodiscard]] bool IsRemoteAuthenticated() noexcept;
[[nodiscard]] std::string RemoteUsername();
// Central game-access gate: only a valid KeyAuth session grants product access.
[[nodiscard]] bool HasGameAccess(std::string_view productId);
// Human-readable remaining duration supplied by KeyAuth for a product. Empty
// means that the product is not covered by the active remote account.
[[nodiscard]] std::string GameAccessDuration(std::string_view productId);
[[nodiscard]] bool HasAnyGameAccess();
[[nodiscard]] Snapshot GetSnapshot();
void Refresh();

} // namespace OmniGhost::Licensing
