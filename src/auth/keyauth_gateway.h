#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>

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

// Boundary implemented by a future KeyAuth adapter. It deliberately accepts no
// Seller API key: administrative credentials must exist only in CI/backend code.
class IRemoteLicenseProvider {
public:
    virtual ~IRemoteLicenseProvider() = default;
    virtual LicenseResult Authenticate(std::string_view licenseKey,
                                       std::string_view hardwareId,
                                       std::stop_token stopToken) = 0;
};

class LicenseGateway final {
public:
    explicit LicenseGateway(std::shared_ptr<IRemoteLicenseProvider> provider);

    LicenseResult Authenticate(std::string_view licenseKey,
                               std::string_view hardwareId,
                               bool allowOffline,
                               std::chrono::steady_clock::time_point now,
                               std::stop_token stopToken = {});
    void Logout() noexcept;
    [[nodiscard]] bool IsAuthenticated() const noexcept;

    void SetOfflineGrantForTesting(std::string hardwareId,
                                   std::chrono::steady_clock::time_point expiresAt);

private:
    static constexpr unsigned kMaximumFailures = 5;
    static constexpr auto kLockoutDuration = std::chrono::minutes(5);

    std::shared_ptr<IRemoteLicenseProvider> provider_;
    mutable std::mutex mutex_;
    unsigned consecutiveFailures_{};
    std::chrono::steady_clock::time_point lockedUntil_{};
    std::string offlineHardwareId_;
    std::chrono::steady_clock::time_point offlineExpiresAt_{};
    bool authenticated_{};
};

} // namespace OmniGhost::Auth

