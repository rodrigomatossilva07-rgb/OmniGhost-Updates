#include "keyauth_gateway.h"

#include <utility>

namespace OmniGhost::Auth {
namespace {
LicenseResult Result(LicenseStatus status, const char* userMessage, const char* code) {
    return {status, userMessage, code, {}};
}
}

LicenseGateway::LicenseGateway(std::shared_ptr<IRemoteLicenseProvider> provider)
    : provider_(std::move(provider)) {}

LicenseResult LicenseGateway::Authenticate(std::string_view licenseKey,
                                           std::string_view hardwareId,
                                           bool allowOffline,
                                           std::chrono::steady_clock::time_point now,
                                           std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Autenticação cancelada.", "auth.cancelled");
    if (licenseKey.empty() || hardwareId.empty())
        return Result(LicenseStatus::Invalid, "Licença ou dispositivo inválido.", "auth.invalid_input");

    {
        std::scoped_lock lock(mutex_);
        if (now < lockedUntil_)
            return Result(LicenseStatus::LockedOut, "Demasiadas tentativas. Tenta novamente mais tarde.", "auth.locked_out");
    }

    LicenseResult remote = provider_
        ? provider_->Authenticate(licenseKey, hardwareId, stopToken)
        : Result(LicenseStatus::NotConfigured, "O serviço de licenças ainda não está configurado.", "auth.not_configured");

    std::scoped_lock lock(mutex_);
    if (remote.Ok()) {
        consecutiveFailures_ = 0;
        lockedUntil_ = {};
        authenticated_ = true;
        offlineHardwareId_ = std::string(hardwareId);
        offlineExpiresAt_ = now + std::chrono::hours(24);
        return remote;
    }

    const bool transient = remote.status == LicenseStatus::Timeout ||
                           remote.status == LicenseStatus::Unavailable;
    if (transient && allowOffline) {
        if (offlineHardwareId_ == hardwareId && now < offlineExpiresAt_) {
            authenticated_ = true;
            return Result(LicenseStatus::Valid, "Sessão offline validada.", "auth.offline_valid");
        }
        if (!offlineHardwareId_.empty())
            return Result(LicenseStatus::OfflineCacheExpired, "A sessão offline expirou. Liga-te à Internet.", "auth.offline_expired");
    }

    authenticated_ = false;
    if (!transient && remote.status != LicenseStatus::Cancelled &&
        remote.status != LicenseStatus::NotConfigured) {
        if (++consecutiveFailures_ >= kMaximumFailures) {
            consecutiveFailures_ = 0;
            lockedUntil_ = now + kLockoutDuration;
        }
    }
    return remote;
}

void LicenseGateway::Logout() noexcept {
    if (provider_)
        provider_->Logout();
    std::scoped_lock lock(mutex_);
    authenticated_ = false;
}

bool LicenseGateway::IsAuthenticated() const noexcept {
    std::scoped_lock lock(mutex_);
    return authenticated_;
}

LicenseResult LicenseGateway::CompleteRemoteOperation(LicenseResult result) {
    std::scoped_lock lock(mutex_);
    authenticated_ = result.Ok();
    if (result.Ok()) {
        consecutiveFailures_ = 0;
        lockedUntil_ = {};
    }
    return result;
}

LicenseResult LicenseGateway::Login(std::string_view username, std::string_view password,
                                    std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Autenticação cancelada.", "auth.cancelled");
    if (username.empty() || password.empty())
        return Result(LicenseStatus::Invalid, "Introduz o utilizador e a palavra-passe.", "auth.invalid_input");
    return CompleteRemoteOperation(provider_ ? provider_->Login(username, password, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth não está configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Register(std::string_view username, std::string_view password,
                                       std::string_view licenseKey, std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Registo cancelado.", "auth.cancelled");
    if (username.empty() || password.empty() || licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz utilizador, palavra-passe e chave de licença.", "auth.invalid_input");
    return CompleteRemoteOperation(provider_ ? provider_->Register(username, password, licenseKey, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth não está configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Activate(std::string_view licenseKey, std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Ativação cancelada.", "auth.cancelled");
    if (licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz uma chave de licença.", "auth.invalid_input");
    return CompleteRemoteOperation(provider_ ? provider_->Activate(licenseKey, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth não está configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Upgrade(std::string_view username, std::string_view licenseKey,
                                      std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Upgrade cancelado.", "auth.cancelled");
    if (username.empty() || licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz o utilizador e a chave de licença.", "auth.invalid_input");
    return CompleteRemoteOperation(provider_ ? provider_->Upgrade(username, licenseKey, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth não está configurado.", "auth.not_configured"));
}

bool LicenseGateway::IsConfigured() const noexcept {
    return provider_ && provider_->IsConfigured();
}

std::string LicenseGateway::CurrentUsername() const {
    return provider_ ? provider_->CurrentUsername() : std::string{};
}

std::vector<RemoteEntitlement> LicenseGateway::CurrentEntitlements() const {
    return provider_ ? provider_->CurrentEntitlements() : std::vector<RemoteEntitlement>{};
}

void LicenseGateway::SetOfflineGrantForTesting(
    std::string hardwareId, std::chrono::steady_clock::time_point expiresAt) {
    std::scoped_lock lock(mutex_);
    offlineHardwareId_ = std::move(hardwareId);
    offlineExpiresAt_ = expiresAt;
}

} // namespace OmniGhost::Auth
