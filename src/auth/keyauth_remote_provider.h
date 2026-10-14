#pragma once

#include "keyauth_gateway.h"

#include <memory>
#include <mutex>

namespace OmniGhost::Auth {

// Concrete adapter for the official KeyAuth C++ package. It is intentionally
// kept outside ImGui and the launcher pages; callers use LicenseGateway only.
class KeyAuthRemoteLicenseProvider final : public IRemoteLicenseProvider {
public:
    KeyAuthRemoteLicenseProvider();
    ~KeyAuthRemoteLicenseProvider() override;

    LicenseResult Authenticate(std::string_view licenseKey, std::string_view hardwareId,
                               std::stop_token stopToken) override;
    LicenseResult Login(std::string_view username, std::string_view password,
                        std::stop_token stopToken) override;
    LicenseResult Register(std::string_view username, std::string_view password,
                           std::string_view licenseKey, std::stop_token stopToken) override;
    LicenseResult Activate(std::string_view licenseKey, std::stop_token stopToken) override;
    LicenseResult Upgrade(std::string_view username, std::string_view licenseKey,
                          std::stop_token stopToken) override;
    void Logout() noexcept override;
    LicenseResult CheckSession(std::stop_token stopToken) override;
    [[nodiscard]] std::string CurrentUsername() const override;
    [[nodiscard]] std::vector<RemoteEntitlement> CurrentEntitlements() const override;
    [[nodiscard]] bool IsConfigured() const noexcept override;

    // Returns the main executable path for provider identity verification.
    // The KeyAuth SDK is embedded in the main EXE, so the executable itself
    // is the module whose integrity is verified.
    [[nodiscard]] std::string GetModulePath() const override;

private:
    struct Impl;
    LicenseResult EnsureInitialized(std::stop_token stopToken);
    LicenseResult ResponseResult() const;

    // AUTH-002 Fix: UI-facing reads must never wait behind an in-flight
    // KeyAuth network call. mutating operations stay serialized on mutex_
    // (the SDK is not thread-safe), while this mirror — updated after each
    // mutation — serves CurrentUsername()/CurrentEntitlements() cheaply.
    mutable std::mutex stateMutex_;
    std::string cachedUsername_;
    std::vector<RemoteEntitlement> cachedEntitlements_;
    void UpdateStateMirror(); // call with mutex_ held

    mutable std::mutex mutex_;
    std::unique_ptr<Impl> impl_;
    bool initialized_{};
    std::string username_;
};

} // namespace OmniGhost::Auth
