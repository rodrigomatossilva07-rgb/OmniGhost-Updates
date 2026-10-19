#include "keyauth_gateway.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "../platform/file_integrity.h"
#include "../platform/text_encoding.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace OmniGhost::Auth {
namespace {

LicenseResult Result(LicenseStatus status, std::string_view userMessage, std::string_view code) {
    return {status,
            std::string(userMessage),
            std::string(code),
            {}};
}










std::optional<std::array<std::byte, 32>> ComputeFileSHA256(std::string_view filePath) {
    std::string hexDigest;
    std::string error;
    const std::filesystem::path widePath(Platform::Utf8ToWide(std::string(filePath)));
    if (!Platform::Sha256File(widePath, hexDigest, error) ||
        hexDigest.size() != 64) {
        return std::nullopt;
    }
    std::array<std::byte, 32> hash{};
    for (std::size_t i = 0; i < 32; ++i) {
        const int high = std::stoi(hexDigest.substr(i * 2, 1), nullptr, 16);
        const int low = std::stoi(hexDigest.substr(i * 2 + 1, 1), nullptr, 16);
        hash[i] = static_cast<std::byte>(high * 16 + low);
    }
    return hash;
}

}

std::optional<ProviderIdentity> ComputeProviderIdentity(std::string_view modulePath) {
    auto hash = ComputeFileSHA256(modulePath);
    if (!hash) {
        return std::nullopt;
    }
    ProviderIdentity identity;
    identity.moduleHash = *hash;
    identity.version = "1.0.0";
    return identity;
}

LicenseGateway::LicenseGateway(std::shared_ptr<IRemoteLicenseProvider> provider)
    : provider_(std::move(provider)) {
    if (!provider_) {
        throw std::runtime_error("LicenseGateway: provider cannot be null");
    }


    const std::string modulePath = provider_->GetModulePath();
    if (modulePath.empty()) {
        throw std::runtime_error("LicenseGateway: provider must implement GetModulePath() for identity verification");
    }

    auto identity = ComputeProviderIdentity(modulePath);
    if (!identity) {
        throw std::runtime_error("LicenseGateway: failed to compute provider identity hash for " + modulePath);
    }























    verifiedIdentity_ = *identity;
}

LicenseResult LicenseGateway::Authenticate(std::string_view licenseKey,
                                           std::string_view hardwareId,
                                           bool allowOffline,
                                           std::chrono::steady_clock::time_point now,
                                           std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Autenticacao cancelada.", "auth.cancelled");
    if (licenseKey.empty() || hardwareId.empty())
        return Result(LicenseStatus::Invalid, "Licenca ou dispositivo invalido.", "auth.invalid_input");

    {
        std::scoped_lock lock(mutex_);
        if (now < lockedUntil_)
            return Result(LicenseStatus::LockedOut, "Demasiadas tentativas. Tenta novamente mais tarde.", "auth.locked_out");
    }

    LicenseResult remote = provider_
        ? provider_->Authenticate(licenseKey, hardwareId, stopToken)
        : Result(LicenseStatus::NotConfigured, "O servico de licencas ainda nao esta configurado.", "auth.not_configured");
    const std::string remoteUsername = remote.Ok() && provider_ ? provider_->CurrentUsername() : std::string{};
    const auto remoteEntitlements = remote.Ok() && provider_ ? provider_->CurrentEntitlements() : std::vector<RemoteEntitlement>{};

    std::scoped_lock lock(mutex_);
    if (remote.Ok()) {
        consecutiveFailures_ = 0;
        lockedUntil_ = {};
        authenticated_ = true;
        offlineSessionActive_ = false;
        lastRemoteValidation_ = std::chrono::steady_clock::now();
        currentUsername_ = remoteUsername;
        currentEntitlements_ = remoteEntitlements;
        offlineHardwareId_ = std::string(hardwareId);
        offlineExpiresAt_ = now + std::chrono::hours(24);
        return remote;
    }

    const bool transient = remote.status == LicenseStatus::Timeout ||
                           remote.status == LicenseStatus::Unavailable;
    if (transient && allowOffline) {
        if (offlineHardwareId_ == hardwareId && now < offlineExpiresAt_) {
            authenticated_ = true;




            offlineSessionActive_ = true;
            lastRemoteValidation_ = now;
            return Result(LicenseStatus::Valid, "Sessao offline validada.", "auth.offline_valid");
        }
        if (!offlineHardwareId_.empty())
            return Result(LicenseStatus::OfflineCacheExpired, "A sessao offline expirou. Liga-te a Internet.", "auth.offline_expired");
    }

    authenticated_ = false;
    currentUsername_.clear();
    currentEntitlements_.clear();
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
    offlineSessionActive_ = false;
    currentUsername_.clear();
    currentEntitlements_.clear();
}

bool LicenseGateway::IsAuthenticated() const noexcept {
    std::scoped_lock lock(mutex_);
    if (!authenticated_)
        return false;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastRemoteValidation_ <= std::chrono::minutes(20))
        return true;

    return offlineSessionActive_ && now < offlineExpiresAt_;
}

LicenseResult LicenseGateway::Revalidate(std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Revalidacao cancelada.", "auth.cancelled");
    {
        std::scoped_lock lock(mutex_);
        if (!authenticated_)
            return Result(LicenseStatus::Invalid, "Sessao nao iniciada.", "auth.session_missing");
    }
    const auto result = provider_ ? provider_->CheckSession(stopToken)
        : Result(LicenseStatus::NotConfigured, "Revalidacao indisponivel.", "auth.not_configured");
    const std::string remoteUsername = result.Ok() && provider_ ? provider_->CurrentUsername() : std::string{};
    const auto remoteEntitlements = result.Ok() && provider_ ? provider_->CurrentEntitlements() : std::vector<RemoteEntitlement>{};
    std::scoped_lock lock(mutex_);
    if (result.Ok()) {
        lastRemoteValidation_ = std::chrono::steady_clock::now();
        consecutiveTransientFailures_ = 0;
        offlineSessionActive_ = false;
        currentUsername_ = remoteUsername;
        currentEntitlements_ = remoteEntitlements;
    } else if (result.status == LicenseStatus::Timeout ||
               result.status == LicenseStatus::Unavailable) {








        if (++consecutiveTransientFailures_ >= kMaximumTransientFailures) {
            consecutiveTransientFailures_ = 0;
            authenticated_ = false;
            currentUsername_.clear();
            currentEntitlements_.clear();
        }
    } else if (result.status != LicenseStatus::Cancelled) {
        consecutiveTransientFailures_ = 0;
        authenticated_ = false;
        currentUsername_.clear();
        currentEntitlements_.clear();
    }
    return result;
}

LicenseResult LicenseGateway::CompleteRemoteOperation(LicenseResult result) {
    const std::string username = result.Ok() && provider_ ? provider_->CurrentUsername() : std::string{};
    const auto entitlements = result.Ok() && provider_ ? provider_->CurrentEntitlements() : std::vector<RemoteEntitlement>{};
    std::scoped_lock lock(mutex_);
    authenticated_ = result.Ok();
    currentUsername_ = username;
    currentEntitlements_ = entitlements;
    if (result.Ok()) {
        consecutiveFailures_ = 0;
        lockedUntil_ = {};
        offlineSessionActive_ = false;
        lastRemoteValidation_ = std::chrono::steady_clock::now();
    } else if (result.status != LicenseStatus::Timeout &&
               result.status != LicenseStatus::Unavailable &&
               result.status != LicenseStatus::Cancelled &&
               result.status != LicenseStatus::NotConfigured) {


        if (++consecutiveFailures_ >= kMaximumFailures) {
            consecutiveFailures_ = 0;
            lockedUntil_ = std::chrono::steady_clock::now() + kLockoutDuration;
        }
    }
    return result;
}

LicenseResult LicenseGateway::CheckLockout() const {
    std::scoped_lock lock(mutex_);
    if (std::chrono::steady_clock::now() < lockedUntil_)
        return Result(LicenseStatus::LockedOut, "Demasiadas tentativas. Tenta novamente mais tarde.", "auth.locked_out");
    return Result(LicenseStatus::Valid, "", "auth.lockout_clear");
}

LicenseResult LicenseGateway::Login(std::string_view username, std::string_view password,
                                    std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Autenticacao cancelada.", "auth.cancelled");
    if (username.empty() || password.empty())
        return Result(LicenseStatus::Invalid, "Introduz o utilizador e a palavra-passe.", "auth.invalid_input");
    if (const auto locked = CheckLockout(); !locked.Ok()) return locked;
    return CompleteRemoteOperation(provider_ ? provider_->Login(username, password, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth nao esta configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Register(std::string_view username, std::string_view password,
                                       std::string_view licenseKey, std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Registo cancelado.", "auth.cancelled");
    if (username.empty() || password.empty() || licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz utilizador, palavra-passe e chave de licenca.", "auth.invalid_input");
    if (const auto locked = CheckLockout(); !locked.Ok()) return locked;
    return CompleteRemoteOperation(provider_ ? provider_->Register(username, password, licenseKey, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth nao esta configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Activate(std::string_view licenseKey, std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Ativacao cancelada.", "auth.cancelled");
    if (licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz uma chave de licenca.", "auth.invalid_input");
    if (const auto locked = CheckLockout(); !locked.Ok()) return locked;
    return CompleteRemoteOperation(provider_ ? provider_->Activate(licenseKey, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth nao esta configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Upgrade(std::string_view username, std::string_view licenseKey,
                                      std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Upgrade cancelado.", "auth.cancelled");
    if (username.empty() || licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz o utilizador e a chave de licenca.", "auth.invalid_input");
    if (const auto locked = CheckLockout(); !locked.Ok()) return locked;
    return CompleteRemoteOperation(provider_ ? provider_->Upgrade(username, licenseKey, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth nao esta configurado.", "auth.not_configured"));
}

bool LicenseGateway::IsConfigured() const noexcept {
    return provider_ && provider_->IsConfigured();
}

std::string LicenseGateway::CurrentUsername() const {
    std::scoped_lock lock(mutex_);
    return currentUsername_;
}

std::vector<RemoteEntitlement> LicenseGateway::CurrentEntitlements() const {
    std::scoped_lock lock(mutex_);
    return currentEntitlements_;
}






#if !defined(OMNIGHOST_PUBLISH_BUILD) || defined(OMNIGHOST_TESTING)
void LicenseGateway::SetOfflineGrantForTesting(
    std::string hardwareId, std::chrono::steady_clock::time_point expiresAt) {
    std::scoped_lock lock(mutex_);
    offlineHardwareId_ = std::move(hardwareId);
    offlineExpiresAt_ = expiresAt;
}
#endif

}
