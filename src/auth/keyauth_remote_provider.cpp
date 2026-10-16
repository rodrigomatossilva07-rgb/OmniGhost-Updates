#include "keyauth_remote_provider.h"
#include "../platform/app_paths.h"
#include "../platform/text_encoding.h"

#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
#include "third_party/keyauth/x64/auth.hpp"
#include "third_party/keyauth/x64/skStr.h"
#endif

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <Windows.h>

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
    if (normalized.find("timeout") != std::string::npos || normalized.find("timed out") != std::string::npos)
        return LicenseStatus::Timeout;
    if (normalized.find("network") != std::string::npos || normalized.find("connection") != std::string::npos ||
        normalized.find("unavailable") != std::string::npos || normalized.find("could not connect") != std::string::npos ||
        normalized.find("failed to connect") != std::string::npos || normalized.find("unable to connect") != std::string::npos)
        return LicenseStatus::Unavailable;
    if (normalized.find("hwid") != std::string::npos) return LicenseStatus::HwidMismatch;
    if (normalized.find("banned") != std::string::npos || normalized.find("revoked") != std::string::npos)
        return LicenseStatus::Revoked;
    return LicenseStatus::Invalid;
}

std::string SafeExpiryRemaining(std::string_view expiry) noexcept {



    long long epochSeconds = 0;
    const auto parsed = std::from_chars(expiry.data(), expiry.data() + expiry.size(), epochSeconds);
    if (parsed.ec != std::errc{} || parsed.ptr != expiry.data() + expiry.size() || epochSeconds <= 0)
        return {};

    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const long long remaining = epochSeconds - nowSeconds;
    if (remaining <= 0)
        return "Expirado";

    const long long days = remaining / 86400;
    if (days > 0)
        return std::to_string(days) + (days == 1 ? " dia" : " dias");
    const long long hours = remaining / 3600;
    if (hours > 0)
        return std::to_string(hours) + (hours == 1 ? " hora" : " horas");
    const long long minutes = (std::max)(1LL, remaining / 60);
    return std::to_string(minutes) + (minutes == 1 ? " minuto" : " minutos");
}

#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
int __cdecl RunCommandWithoutWindow(const char* command) noexcept {
    (void)command;

    return -1;
}

bool InstallHiddenSystemImport() noexcept {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base)
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return false;

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->OriginalFirstThunk)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), "system") != 0)
                continue;
            DWORD oldProtection = 0;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(addresses->u1.Function),
                                PAGE_READWRITE, &oldProtection))
                return false;
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(&RunCommandWithoutWindow);
            DWORD ignored = 0;
            VirtualProtect(&addresses->u1.Function, sizeof(addresses->u1.Function),
                           oldProtection, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &addresses->u1.Function,
                                  sizeof(addresses->u1.Function));
            return true;
        }
    }
    return false;
}

void DisableKeyAuthShellCommands() noexcept {
    static std::once_flag installed;
    std::call_once(installed, [] { (void)InstallHiddenSystemImport(); });
}
#endif

}

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
    DisableKeyAuthShellCommands();
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

void KeyAuthRemoteLicenseProvider::UpdateStateMirror() {


    std::scoped_lock stateLock(stateMutex_);
    cachedUsername_ = username_;
    cachedEntitlements_.clear();
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    cachedEntitlements_.reserve(impl_->api.user_data.subscriptions.size());
    for (const auto& subscription : impl_->api.user_data.subscriptions) {
        if (!subscription.name.empty()) {
            cachedEntitlements_.push_back({subscription.name, subscription.expiry,
                                           SafeExpiryRemaining(subscription.expiry)});
        }
    }
#endif
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
    UpdateStateMirror();
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


    impl_->api.regstr(std::string(username), std::string(password), std::string(licenseKey));
    LicenseResult result = ResponseResult();
    if (result.Ok()) username_ = impl_->api.user_data.username.empty() ? std::string(username) : impl_->api.user_data.username;
    else username_.clear();
    UpdateStateMirror();
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
    UpdateStateMirror();
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
    UpdateStateMirror();
    return result;
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não está configurado nesta build.", "keyauth.not_configured");
#endif
}

void KeyAuthRemoteLicenseProvider::Logout() noexcept {
    std::scoped_lock lock(mutex_);
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    if (initialized_) {
        try { impl_->api.logout(); } catch (...) {  }
    }
#endif
    username_.clear();
    try { UpdateStateMirror(); } catch (...) {  }
}

LicenseResult KeyAuthRemoteLicenseProvider::CheckSession(std::stop_token stopToken) {
    std::scoped_lock lock(mutex_);
    if (stopToken.stop_requested())
        return MakeResult(LicenseStatus::Cancelled, "Revalidação cancelada.", "keyauth.cancelled");
    if (!initialized_)
        return MakeResult(LicenseStatus::Invalid, "Sessão KeyAuth não iniciada.", "keyauth.session_missing");
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    try {
        impl_->api.check();
        LicenseResult result = ResponseResult();
        UpdateStateMirror();
        return result;
    } catch (...) {
        return MakeResult(LicenseStatus::Unavailable, "Serviço KeyAuth temporariamente indisponível.",
                          "keyauth.recheck_unavailable");
    }
#else
    return MakeResult(LicenseStatus::NotConfigured, "KeyAuth não configurado.", "keyauth.not_configured");
#endif
}

std::string KeyAuthRemoteLicenseProvider::CurrentUsername() const {
    std::scoped_lock stateLock(stateMutex_);
    return cachedUsername_;
}

std::vector<RemoteEntitlement> KeyAuthRemoteLicenseProvider::CurrentEntitlements() const {
    std::scoped_lock stateLock(stateMutex_);
    return cachedEntitlements_;
}

std::string KeyAuthRemoteLicenseProvider::GetModulePath() const {


    return OmniGhost::Platform::WideToUtf8(OmniGhost::Paths::Executable().wstring());
}

}
