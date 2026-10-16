#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace OmniGhost::Auth {

enum class LicenseStatus {
    Valid,
    Invalid,
    Expired,
    Revoked,
    HwidMismatch,
    Timeout,
    Unavailable,
    Cancelled,
    LockedOut,
    OfflineCacheExpired,
    NotConfigured
};

struct LicenseResult {
    LicenseStatus status{LicenseStatus::NotConfigured};
    std::string userMessage;
    std::string diagnosticCode;
    std::chrono::system_clock::time_point licenseExpiresAt{};
    [[nodiscard]] bool Ok() const noexcept { return status == LicenseStatus::Valid; }
};

struct RemoteEntitlement {
    std::string name;
    std::string expiresAt;
    std::string remaining;
};



struct ProviderIdentity {

    std::array<std::byte, 32> moduleHash{};


    std::string version;

    bool operator==(const ProviderIdentity& other) const noexcept {
        return moduleHash == other.moduleHash;
    }
};



std::optional<ProviderIdentity> ComputeProviderIdentity(std::string_view modulePath);






constexpr ProviderIdentity kOfficialProviderIdentity{
    .moduleHash = {  },
    .version = "1.0.0"
};



class IRemoteLicenseProvider {
public:
    virtual ~IRemoteLicenseProvider() = default;
    virtual LicenseResult Authenticate(std::string_view licenseKey,
                                       std::string_view hardwareId,
                                       std::stop_token stopToken) = 0;



    virtual LicenseResult Login(std::string_view, std::string_view, std::stop_token) {
        return {LicenseStatus::NotConfigured, "O servico de autenticacao nao esta configurado.", "auth.not_configured", {}};
    }
    virtual LicenseResult Register(std::string_view, std::string_view, std::string_view, std::stop_token) {
        return {LicenseStatus::NotConfigured, "O servico de autenticacao nao esta configurado.", "auth.not_configured", {}};
    }
    virtual LicenseResult Activate(std::string_view, std::stop_token) {
        return {LicenseStatus::NotConfigured, "O servico de autenticacao nao esta configurado.", "auth.not_configured", {}};
    }
    virtual LicenseResult Upgrade(std::string_view, std::string_view, std::stop_token) {
        return {LicenseStatus::NotConfigured, "O servico de autenticacao nao esta configurado.", "auth.not_configured", {}};
    }
    virtual void Logout() noexcept {}
    virtual LicenseResult CheckSession(std::stop_token) {
        return {LicenseStatus::NotConfigured, "Revalidacao indisponivel.", "auth.recheck_unavailable", {}};
    }
    [[nodiscard]] virtual std::string CurrentUsername() const { return {}; }


    [[nodiscard]] virtual std::vector<RemoteEntitlement> CurrentEntitlements() const { return {}; }
    [[nodiscard]] virtual bool IsConfigured() const noexcept { return false; }



    virtual std::string GetModulePath() const { return {}; }
};

class LicenseGateway final {
public:


    explicit LicenseGateway(std::shared_ptr<IRemoteLicenseProvider> provider);

    LicenseResult Authenticate(std::string_view licenseKey,
                               std::string_view hardwareId,
                               bool allowOffline,
                               std::chrono::steady_clock::time_point now,
                               std::stop_token stopToken = {});
    LicenseResult Login(std::string_view username, std::string_view password,
                        std::stop_token stopToken = {});
    LicenseResult Register(std::string_view username, std::string_view password,
                           std::string_view licenseKey, std::stop_token stopToken = {});
    LicenseResult Activate(std::string_view licenseKey, std::stop_token stopToken = {});
    LicenseResult Upgrade(std::string_view username, std::string_view licenseKey,
                          std::stop_token stopToken = {});
    void Logout() noexcept;
    LicenseResult Revalidate(std::stop_token stopToken = {});
    [[nodiscard]] bool IsAuthenticated() const noexcept;
    [[nodiscard]] bool IsConfigured() const noexcept;
    [[nodiscard]] std::string CurrentUsername() const;
    [[nodiscard]] std::vector<RemoteEntitlement> CurrentEntitlements() const;

    void SetOfflineGrantForTesting(std::string hardwareId,
                                   std::chrono::steady_clock::time_point expiresAt);

private:
    static constexpr unsigned kMaximumFailures = 5;
    static constexpr auto kLockoutDuration = std::chrono::minutes(5);


    [[nodiscard]] LicenseResult CheckLockout() const;

    std::shared_ptr<IRemoteLicenseProvider> provider_;
    ProviderIdentity verifiedIdentity_;
    mutable std::mutex mutex_;
    unsigned consecutiveFailures_{};
    std::chrono::steady_clock::time_point lockedUntil_{};
    std::string offlineHardwareId_;
    std::chrono::steady_clock::time_point offlineExpiresAt_{};
    bool authenticated_{};

    bool offlineSessionActive_{};
    std::chrono::steady_clock::time_point lastRemoteValidation_{};
    std::string currentUsername_;
    std::vector<RemoteEntitlement> currentEntitlements_;

    LicenseResult CompleteRemoteOperation(LicenseResult result);
};

}