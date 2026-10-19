#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../auth/keyauth_gateway.h"
#include "../auth/launcher_account_service.h"

namespace OmniGhost::Licensing {

struct Snapshot {
    bool remoteServiceConfigured = false;
    bool remoteAuthenticated = false;
    bool websiteAccount = false;
    bool websiteAdmin = false;
    std::string remoteUsername;
    std::vector<OmniGhost::Auth::RemoteEntitlement> remoteEntitlements;
    std::vector<OmniGhost::Auth::LauncherLicense> websiteLicenses;
    std::string selectedLicenseId;
};



[[nodiscard]] OmniGhost::Auth::LicenseResult Login(std::string_view username, std::string_view password);
[[nodiscard]] OmniGhost::Auth::LicenseResult LoginWebsite(std::string_view email, std::string_view password);
[[nodiscard]] OmniGhost::Auth::LicenseResult RegisterWebsite(std::string_view email,
                                                              std::string_view password,
                                                              std::string_view licenseKey);
[[nodiscard]] OmniGhost::Auth::LicenseResult RestoreWebsiteSession();
[[nodiscard]] bool SelectWebsiteLicense(std::string_view licenseId);
[[nodiscard]] bool IsWebsiteAuthenticated() noexcept;
[[nodiscard]] OmniGhost::Auth::LicenseResult Register(std::string_view username, std::string_view password,
                                                       std::string_view licenseKey);
[[nodiscard]] OmniGhost::Auth::LicenseResult ActivateKey(std::string_view licenseKey);
[[nodiscard]] OmniGhost::Auth::LicenseResult Upgrade(std::string_view username, std::string_view licenseKey);
void LogoutRemote() noexcept;
void StartSessionRevalidation();
void ShutdownSessionRevalidation() noexcept;
[[nodiscard]] bool IsRemoteConfigured() noexcept;

[[nodiscard]] bool SaveRememberedRemoteCredentials(std::string_view username, std::string_view password);
[[nodiscard]] bool LoadRememberedRemoteCredentials(std::string& username, std::string& password);
void ClearRememberedRemoteCredentials() noexcept;
[[nodiscard]] bool IsRemoteAuthenticated() noexcept;
[[nodiscard]] std::string RemoteUsername();

[[nodiscard]] bool IsAdmin();

[[nodiscard]] bool HasGameAccess(std::string_view productId);


[[nodiscard]] std::string GameAccessDuration(std::string_view productId);
[[nodiscard]] bool HasAnyGameAccess();
[[nodiscard]] Snapshot GetSnapshot();
void Refresh();

}
