#pragma once

#include "keyauth_gateway.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace OmniGhost::Auth {

struct LauncherLicense {
    std::string id;
    std::string product;
    std::string plan;
    std::string keyauthState;
    std::string expiresAt;
    std::string realExpiresAt;

    [[nodiscard]] bool IsUsable() const noexcept;
};

struct LauncherSessionState {
    bool authenticated{};
    bool isAdmin{};
    std::string email;
    std::vector<LauncherLicense> licenses;
    std::string selectedLicenseId;
    std::chrono::system_clock::time_point expiresAt{};
};

struct LauncherApiResponse {
    int statusCode{};
    std::string body;
    std::string error;
};

class ILauncherApiTransport {
public:
    virtual ~ILauncherApiTransport() = default;
    virtual LauncherApiResponse Request(std::string_view method,
                                        std::string_view path,
                                        std::string_view body,
                                        std::string_view bearerToken,
                                        std::string_view deviceId) = 0;
};

struct StoredLauncherSession {
    std::string token;
    std::string email;
    std::string expiresAt;
    std::string selectedLicenseId;
};

class ILauncherSessionStore {
public:
    virtual ~ILauncherSessionStore() = default;
    [[nodiscard]] virtual bool Save(const StoredLauncherSession& session) = 0;
    [[nodiscard]] virtual bool Load(StoredLauncherSession& session) = 0;
    virtual void Clear() noexcept = 0;
};

class LauncherAccountService final {
public:
    explicit LauncherAccountService(
        std::shared_ptr<ILauncherApiTransport> transport = {},
        std::shared_ptr<ILauncherSessionStore> store = {});

    [[nodiscard]] LicenseResult Login(std::string_view email, std::string_view password);
    [[nodiscard]] LicenseResult Register(std::string_view email, std::string_view password,
                                         std::string_view licenseKey);
    [[nodiscard]] LicenseResult Restore();
    [[nodiscard]] LicenseResult Renew();
    [[nodiscard]] LicenseResult RefreshLicenses();
    void Logout() noexcept;

    [[nodiscard]] bool IsAuthenticated() const noexcept;
    [[nodiscard]] bool NeedsRenewal(std::chrono::system_clock::time_point now) const noexcept;
    [[nodiscard]] LauncherSessionState Snapshot() const;
    [[nodiscard]] bool SelectLicense(std::string_view licenseId);
    [[nodiscard]] bool HasProduct(std::string_view product) const;
    [[nodiscard]] std::string ProductDuration(std::string_view product) const;
    [[nodiscard]] LauncherApiResponse AuthenticatedRequest(std::string_view method,
                                                           std::string_view path,
                                                           std::string_view body = {});
    [[nodiscard]] LauncherApiResponse BearerRequest(std::string_view method,
                                                    std::string_view path,
                                                    std::string_view body,
                                                    std::string_view bearerToken);

private:
    [[nodiscard]] LicenseResult ApplySessionResponse(LauncherApiResponse response,
                                                     bool requireToken,
                                                     bool retainLicenses);
    [[nodiscard]] LicenseResult ApplyLicenseResponse(const LauncherApiResponse& response);
    [[nodiscard]] LicenseResult RefreshLicensesUnlocked();
    [[nodiscard]] bool PersistLocked() const;
    void ClearLocked() noexcept;

    std::shared_ptr<ILauncherApiTransport> transport_;
    std::shared_ptr<ILauncherSessionStore> store_;
    mutable std::mutex mutex_;
    std::mutex operationMutex_;
    LauncherSessionState state_;
    std::string token_;
    std::string expiresAtText_;
    std::string deviceId_;
};

[[nodiscard]] std::shared_ptr<ILauncherApiTransport> CreateLauncherApiTransport();
[[nodiscard]] std::shared_ptr<ILauncherSessionStore> CreateLauncherSessionStore();

}
