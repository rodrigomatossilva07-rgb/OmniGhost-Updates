#include "keyauth_remote_provider.h"

#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
#include "keyauth/x64/auth.hpp"
#include "keyauth/x64/skStr.h"
#endif

#include <algorithm>
#include <cctype>
#include <utility>

namespace OmniGhost::Auth {
namespace {

LicenseResult MakeResult(LicenseStatus status, std::string message, const char* code) {
    return {status, std::move(message), code, {}};
}

LicenseStatus StatusForMessage(std::string_view message) {
    std::string normalized(message);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (normalized.find("expired") != std::string::npos) return LicenseStatus::Expired;
    if (normalized.find("hwid") != std::string::npos) return LicenseStatus::HwidMismatch;
    if (normalized.find("banned") != std::string::npos || normalized.find("revoked") != std::string::npos)
        return LicenseStatus::Revoked;
    return LicenseStatus::Invalid;
}

} // namespace

struct KeyAuthRemoteLicenseProvider::Impl {
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    Impl()
        : api(skCrypt("OmniGhost").decrypt(),
              skCrypt("TMXuFRFSR1").decrypt(),
              skCrypt("1.0").decrypt(),
              skCrypt("https://keyauth.win/api/1.3/").decrypt(),
              skCrypt("").decrypt()) {}

    KeyAuth::api api;
#endif
};

KeyAuthRemoteLicenseProvider::KeyAuthRemoteLicenseProvider()
    : impl_(std::make_unique<Impl>()) {}

KeyAuthRemoteLicenseProvider::~KeyAuthRemoteLicenseProvider() = default;

bool KeyAuthRemoteLicenseProvider::IsConfigured() const noexcept {
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    return true;
#else
    return false;
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::EnsureInitialized(std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return MakeResult(LicenseStatus::Cancelled, "Autenticação cancelada.", "keyauth.cancelled");
    if (!IsConfigured())
        return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
    if (initialized_)
        return MakeResult(LicenseStatus::Valid, "KeyAuth pronto.", "keyauth.ready");

#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    impl_->api.init();
    if (!impl_->api.response.success)
        return MakeResult(StatusForMessage(impl_->api.response.message),
            impl_->api.response.message.empty() ? "Não foi possível inicializar KeyAuth." : impl_->api.response.message,
            "keyauth.init_failed");
    initialized_ = true;
    return MakeResult(LicenseStatus::Valid, "KeyAuth pronto.", "keyauth.ready");
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::ResponseResult() const {
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    const std::string message = impl_->api.response.message.empty()
        ? (impl_->api.response.success ? "Sessão KeyAuth iniciada." : "KeyAuth recusou o pedido.")
        : impl_->api.response.message;
    return MakeResult(impl_->api.response.success ? LicenseStatus::Valid : StatusForMessage(message),
        message, impl_->api.response.success ? "keyauth.ok" : "keyauth.rejected");
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::Authenticate(std::string_view licenseKey,
                                                         std::string_view,
                                                         std::stop_token stopToken) {
    return Activate(licenseKey, stopToken);
}

LicenseResult KeyAuthRemoteLicenseProvider::Login(std::string_view username, std::string_view password,
                                                   std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    LicenseResult ready = EnsureInitialized(stopToken);
    if (!ready.Ok()) return ready;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    impl_->api.login(std::string(username), std::string(password));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = impl_->api.user_data.username.empty() ? std::string(username) : impl_->api.user_data.username;
    else username_.clear();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::Register(std::string_view username, std::string_view password,
                                                      std::string_view licenseKey, std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    LicenseResult ready = EnsureInitialized(stopToken);
    if (!ready.Ok()) return ready;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    // This is the account-creating KeyAuth call. It consumes/associates the
    // supplied license key in KeyAuth and makes the user visible in its panel.
    impl_->api.regstr(std::string(username), std::string(password), std::string(licenseKey));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = impl_->api.user_data.username.empty() ? std::string(username) : impl_->api.user_data.username;
    else username_.clear();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::Activate(std::string_view licenseKey, std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    LicenseResult ready = EnsureInitialized(stopToken);
    if (!ready.Ok()) return ready;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    impl_->api.license(std::string(licenseKey));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = impl_->api.user_data.username;
    else username_.clear();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

LicenseResult KeyAuthRemoteLicenseProvider::Upgrade(std::string_view username, std::string_view licenseKey,
                                                     std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    LicenseResult ready = EnsureInitialized(stopToken);
    if (!ready.Ok()) return ready;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    impl_->api.upgrade(std::string(username), std::string(licenseKey));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = std::string(username);
    else username_.clear();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

void KeyAuthRemoteLicenseProvider::Logout() noexcept {
    std::scoped_lock lock(mutex_);
    username_.clear();
}

std::string KeyAuthRemoteLicenseProvider::CurrentUsername() const {
    std::scoped_lock lock(mutex_);
    return username_;
}

} // namespace OmniGhost::Auth
