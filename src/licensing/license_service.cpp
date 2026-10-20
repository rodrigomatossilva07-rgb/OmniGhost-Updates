#include "license_service.h"
#include "../auth/keyauth_remote_provider.h"
#include "../auth/launcher_account_service.h"
#include "../launcher/remote_usage_service.h"
#include "../platform/app_paths.h"
#include "../platform/scope_exit.h"
#include "../security/security_manager.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#pragma comment(lib, "crypt32.lib")

namespace OmniGhost::Licensing {
namespace {




std::string CurrentExecutablePath() {
    char buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::string(buffer, length);
}

void LogLicenseFailure(const char* what) {
    const std::string message = std::string("[LICENSE][FATAL] Failed to initialize license gateway: ") + what + "\n";
    OutputDebugStringA(message.c_str());
    std::cerr << message;
}

void LogRemoteLogoutFailure(const char* detail) noexcept {
    OutputDebugStringA("[OmniGhost][License] Failed to start remote logout");
    if (detail && *detail) {
        OutputDebugStringA(": ");
        OutputDebugStringA(detail);
    }
    OutputDebugStringA("\n");
}




class FakeProviderForFallback final : public OmniGhost::Auth::IRemoteLicenseProvider {
public:
    [[nodiscard]] bool IsConfigured() const noexcept override { return false; }

    [[nodiscard]] std::string GetModulePath() const override { return CurrentExecutablePath(); }
    OmniGhost::Auth::LicenseResult Authenticate(std::string_view, std::string_view, std::stop_token) override {
        return {OmniGhost::Auth::LicenseStatus::NotConfigured, "O serviço de licenças não está disponível.", "auth.unavailable", {}};
    }
    OmniGhost::Auth::LicenseResult Login(std::string_view, std::string_view, std::stop_token) override {
        return {OmniGhost::Auth::LicenseStatus::NotConfigured, "O serviço de autenticação não está disponível.", "auth.unavailable", {}};
    }
    OmniGhost::Auth::LicenseResult Register(std::string_view, std::string_view, std::string_view, std::stop_token) override {
        return {OmniGhost::Auth::LicenseStatus::NotConfigured, "O serviço de autenticação não está disponível.", "auth.unavailable", {}};
    }
    OmniGhost::Auth::LicenseResult Activate(std::string_view, std::stop_token) override {
        return {OmniGhost::Auth::LicenseStatus::NotConfigured, "O serviço de autenticação não está disponível.", "auth.unavailable", {}};
    }
    OmniGhost::Auth::LicenseResult Upgrade(std::string_view, std::string_view, std::stop_token) override {
        return {OmniGhost::Auth::LicenseStatus::NotConfigured, "O serviço de autenticação não está disponível.", "auth.unavailable", {}};
    }
    void Logout() noexcept override {}
    OmniGhost::Auth::LicenseResult CheckSession(std::stop_token) override {
        return {OmniGhost::Auth::LicenseStatus::NotConfigured, "Revalidação indisponível.", "auth.unavailable", {}};
    }
    [[nodiscard]] std::string CurrentUsername() const override { return {}; }
    [[nodiscard]] std::vector<OmniGhost::Auth::RemoteEntitlement> CurrentEntitlements() const override { return {}; }
};

std::mutex g_mutex;
Snapshot g_snapshot{};

OmniGhost::Auth::LauncherAccountService& WebsiteService() {
    static OmniGhost::Auth::LauncherAccountService service;
    return service;
}

OmniGhost::Auth::LicenseGateway& RemoteGateway() {
    static std::unique_ptr<OmniGhost::Auth::LicenseGateway> gateway;
    static std::once_flag initFlag;
    std::call_once(initFlag, [] {
        try {
            gateway = std::make_unique<OmniGhost::Auth::LicenseGateway>(
                std::make_shared<OmniGhost::Auth::KeyAuthRemoteLicenseProvider>());
        } catch (const std::exception& ex) {
            LogLicenseFailure(ex.what());
        } catch (...) {
            LogLicenseFailure("unknown exception");
        }
        if (!gateway) {





            try {
                gateway = std::make_unique<OmniGhost::Auth::LicenseGateway>(
                    std::make_shared<FakeProviderForFallback>());
            } catch (const std::exception& ex) {
                LogLicenseFailure(ex.what());
            } catch (...) {
                LogLicenseFailure("unknown exception building fallback gateway");
            }
        }
        if (!gateway) {



            LogLicenseFailure("gateway unavailable; licensing will report NotConfigured");
            throw std::runtime_error("Licensing gateway unavailable");
        }
    });
    return *gateway;
}
std::mutex& RevalidatorMutex() {
    static std::mutex mutex;
    return mutex;
}
std::jthread& RevalidatorThread() {
    static std::jthread worker;
    return worker;
}
void StopRevalidator() {
    std::scoped_lock lock(RevalidatorMutex());
    auto& worker = RevalidatorThread();
    if (worker.joinable()) {
        worker.request_stop();
        worker.join();
    }
}
void StartRevalidator() {

    auto& gateway = RemoteGateway();
    std::scoped_lock lock(RevalidatorMutex());
    auto& worker = RevalidatorThread();
    if (worker.joinable()) return;
    worker = std::jthread([&gateway](std::stop_token stop) {
        std::mutex waitMutex;
        std::condition_variable_any wake;
        std::unique_lock waitLock(waitMutex);
        bool degraded = false;
        auto nextWebsiteLicenseRefresh = std::chrono::steady_clock::now();
        auto nextLegacyValidation = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
            if (wake.wait_for(waitLock, stop, std::chrono::minutes(1), [] { return false; })) break;
            if (stop.stop_requested()) break;
            waitLock.unlock();
            OmniGhost::Auth::LicenseResult result{
                OmniGhost::Auth::LicenseStatus::Valid, {}, "auth.idle", {}};
            bool checked = false;
            if (WebsiteService().IsAuthenticated()) {
                const auto steadyNow = std::chrono::steady_clock::now();
                if (WebsiteService().NeedsRenewal(std::chrono::system_clock::now())) {
                    result = WebsiteService().Renew();
                    checked = true;
                    nextWebsiteLicenseRefresh = steadyNow + std::chrono::minutes(5);
                } else if (steadyNow >= nextWebsiteLicenseRefresh) {
                    result = WebsiteService().RefreshLicenses();
                    checked = true;
                    nextWebsiteLicenseRefresh = steadyNow + std::chrono::minutes(5);
                }
            } else if (gateway.IsAuthenticated() &&
                       std::chrono::steady_clock::now() >= nextLegacyValidation) {
                result = gateway.Revalidate(stop);
                checked = true;
                nextLegacyValidation = std::chrono::steady_clock::now() + std::chrono::minutes(15);
            }
            if (stop.stop_requested() || result.status == OmniGhost::Auth::LicenseStatus::Cancelled)
                break;
            if (checked && !result.Ok()) {
                degraded = true;
                OmniGhost::Security::SecurityManager::Instance().Record(
                    result.status == OmniGhost::Auth::LicenseStatus::Timeout ||
                    result.status == OmniGhost::Auth::LicenseStatus::Unavailable
                        ? OmniGhost::Security::EventId::SessionUnavailable
                        : OmniGhost::Security::EventId::SessionInvalid);
            } else if (checked && degraded) {
                degraded = false;
                OmniGhost::Security::SecurityManager::Instance().Record(
                    OmniGhost::Security::EventId::SessionRestored);
            }
            waitLock.lock();
        }
    });
}
void ApplyRemoteState(Snapshot& snapshot) {
    const auto website = WebsiteService().Snapshot();
    if (website.authenticated) {
        snapshot.remoteServiceConfigured = true;
        snapshot.remoteAuthenticated = true;
        snapshot.websiteAccount = true;
        snapshot.websiteAdmin = website.isAdmin;
        snapshot.remoteUsername = website.email;
        snapshot.websiteLicenses = website.licenses;
        snapshot.selectedLicenseId = website.selectedLicenseId;
        snapshot.remoteEntitlements.clear();
        snapshot.remoteEntitlements.reserve(website.licenses.size());
        for (const auto& license : website.licenses) {
            if (!license.IsUsable()) continue;
            snapshot.remoteEntitlements.push_back({license.product, license.expiresAt,
                license.plan == "lifetime" || license.expiresAt.empty()
                    ? "Vitalício" : license.expiresAt});
        }
        return;
    }
    auto& gateway = RemoteGateway();
    snapshot.remoteServiceConfigured = true;
    snapshot.remoteAuthenticated = gateway.IsAuthenticated();
    snapshot.remoteUsername = gateway.CurrentUsername();
    snapshot.remoteEntitlements = gateway.CurrentEntitlements();
}
std::string NormalizeEntitlement(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character)) result.push_back(static_cast<char>(std::tolower(character)));
        else if (character == '-' || character == '_' || character == '.' || character == ' ') result.push_back('_');
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result;
}
constexpr bool IsAdminEntitlement(std::string_view entitlement) noexcept {
    return entitlement == "admin";
}
constexpr bool IsUniversalEntitlement(std::string_view entitlement) noexcept {
    return IsAdminEntitlement(entitlement) || entitlement == "all" || entitlement == "todos" ||
           entitlement == "todos_os_jogos" || entitlement == "omnighost_all" ||
           entitlement == "omnighost_full" || entitlement == "omnighost_universal";
}
static_assert(IsUniversalEntitlement("admin"));
bool RemoteEntitlementGrants(std::string_view productId) {
    const std::string product = NormalizeEntitlement(productId);
    if (product.empty()) return false;
    const std::string canonical = "omnighost_" + product;
    for (const auto& item : RemoteGateway().CurrentEntitlements()) {
        const std::string entitlement = NormalizeEntitlement(item.name);
        if (IsUniversalEntitlement(entitlement) || entitlement == product || entitlement == canonical)
            return true;
    }
    return false;
}

constexpr unsigned char kProtectedMagic[]{'O','G','L','1'};
constexpr std::size_t kMaximumCredentialBytes = 4096;
bool ProtectForCurrentUser(std::string_view plaintext, std::vector<unsigned char>& output) {
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&input, L"OmniGhost remembered account", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) return false;
    auto cleanup = OmniGhost::Platform::MakeScopeExit([&] { if (encrypted.pbData) LocalFree(encrypted.pbData); });
    output.assign(std::begin(kProtectedMagic), std::end(kProtectedMagic));
    output.insert(output.end(), encrypted.pbData, encrypted.pbData + encrypted.cbData);
    return true;
}
bool UnprotectForCurrentUser(const std::vector<unsigned char>& input, std::string& plaintext) {
    if (input.size() <= sizeof(kProtectedMagic) ||
        !std::equal(std::begin(kProtectedMagic), std::end(kProtectedMagic), input.begin())) return false;
    DATA_BLOB encrypted{};
    encrypted.pbData = const_cast<BYTE*>(input.data() + sizeof(kProtectedMagic));
    encrypted.cbData = static_cast<DWORD>(input.size() - sizeof(kProtectedMagic));
    DATA_BLOB clear{};
    if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &clear)) return false;
    auto cleanup = OmniGhost::Platform::MakeScopeExit([&] { if (clear.pbData) LocalFree(clear.pbData); });
    plaintext.assign(reinterpret_cast<const char*>(clear.pbData), clear.cbData);
    return plaintext.size() <= kMaximumCredentialBytes;
}
bool SaveProtected(const std::filesystem::path& path, std::string_view payload) {
    if (!OmniGhost::Paths::EnsureUserDirectories() || payload.size() > kMaximumCredentialBytes) return false;
    std::vector<unsigned char> protectedData;
    if (!ProtectForCurrentUser(payload, protectedData)) return false;
    const auto temporary = std::filesystem::path(path.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()));
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(reinterpret_cast<const char*>(protectedData.data()),
                     static_cast<std::streamsize>(protectedData.size()));
        output.flush();
        if (!output) { std::error_code ec; std::filesystem::remove(temporary, ec); return false; }
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    const bool copied = CopyFileW(temporary.c_str(), path.c_str(), FALSE) != 0;
    std::error_code ec;
    std::filesystem::remove(temporary, ec);
    return copied;
}
void Publish(Snapshot snapshot) {
    std::lock_guard lock(g_mutex);
    g_snapshot = std::move(snapshot);
}
}

void Refresh() {
    Snapshot snapshot{};
    ApplyRemoteState(snapshot);
    Publish(std::move(snapshot));
}
Snapshot GetSnapshot() {
    Snapshot snapshot;
    {
        std::lock_guard lock(g_mutex);
        snapshot = g_snapshot;
    }
    ApplyRemoteState(snapshot);
    return snapshot;
}
OmniGhost::Auth::LauncherApiResponse WebsiteRequest(
    std::string_view method, std::string_view path, std::string_view body) {
    return WebsiteService().AuthenticatedRequest(method, path, body);
}
OmniGhost::Auth::LauncherApiResponse UsageRequest(
    std::string_view method, std::string_view path, std::string_view body,
    std::string_view accessToken) {
    return WebsiteService().BearerRequest(method, path, body, accessToken);
}
OmniGhost::Auth::LicenseResult Login(std::string_view username, std::string_view password) {
    return RemoteGateway().Login(username, password);
}
OmniGhost::Auth::LicenseResult LoginWebsite(std::string_view email, std::string_view password) {
    const auto result = WebsiteService().Login(email, password);
    if (result.Ok()) OmniGhost::RemoteUsage::Refresh();
    Refresh();
    return result;
}
OmniGhost::Auth::LicenseResult RegisterWebsite(std::string_view email,
                                               std::string_view password,
                                               std::string_view licenseKey) {
    const auto result = WebsiteService().Register(email, password, licenseKey);
    if (result.Ok()) OmniGhost::RemoteUsage::Refresh();
    Refresh();
    return result;
}
OmniGhost::Auth::LicenseResult RestoreWebsiteSession() {
    const auto result = WebsiteService().Restore();
    if (result.Ok()) OmniGhost::RemoteUsage::Refresh();
    Refresh();
    return result;
}
bool SelectWebsiteLicense(std::string_view licenseId) {
    const bool selected = WebsiteService().SelectLicense(licenseId);
    if (selected) Refresh();
    return selected;
}
bool IsWebsiteAuthenticated() noexcept { return WebsiteService().IsAuthenticated(); }
OmniGhost::Auth::LicenseResult Register(std::string_view username, std::string_view password,
                                        std::string_view licenseKey) {
    return RemoteGateway().Register(username, password, licenseKey);
}
OmniGhost::Auth::LicenseResult ActivateKey(std::string_view licenseKey) {
    return RemoteGateway().Activate(licenseKey);
}
OmniGhost::Auth::LicenseResult Upgrade(std::string_view username, std::string_view licenseKey) {
    return RemoteGateway().Upgrade(username, licenseKey);
}
void LogoutRemote() noexcept {













    OmniGhost::RemoteUsage::Shutdown();
    StopRevalidator();
    WebsiteService().Logout();
    const bool hadRemoteSession = RemoteGateway().IsAuthenticated();
    if (hadRemoteSession) {
        static std::future<void> pendingRemoteLogout = std::async(std::launch::deferred, [] {});
        try {
            auto next = std::async(std::launch::async, [] {
                RemoteGateway().Logout();
            });



            pendingRemoteLogout = std::move(next);
        } catch (const std::exception& exception) {
            LogRemoteLogoutFailure(exception.what());
        } catch (...) {
            LogRemoteLogoutFailure("unknown exception");
        }
    }
    ClearRememberedRemoteCredentials();
    Refresh();
}
void StartSessionRevalidation() { StartRevalidator(); }
void ShutdownSessionRevalidation() noexcept { StopRevalidator(); }
bool IsRemoteConfigured() noexcept { return true; }
bool IsRemoteAuthenticated() noexcept {
    return WebsiteService().IsAuthenticated() || RemoteGateway().IsAuthenticated();
}
std::string RemoteUsername() {
    const auto website = WebsiteService().Snapshot();
    return website.authenticated ? website.email : RemoteGateway().CurrentUsername();
}
bool IsAdmin() {
    if (!IsRemoteAuthenticated() ||
        !OmniGhost::Security::SecurityManager::Instance().CanUseProtectedFeatures())
        return false;
    const auto website = WebsiteService().Snapshot();
    if (website.authenticated) return website.isAdmin;
    const auto entitlements = RemoteGateway().CurrentEntitlements();
    return std::any_of(entitlements.begin(), entitlements.end(), [](const auto& item) {
        return IsAdminEntitlement(NormalizeEntitlement(item.name));
    });
}
bool HasGameAccess(std::string_view productId) {
    constexpr std::string_view kProducts[]{"fivem", "cs2", "rust", "warzone", "apex", "fortnite"};
    if (std::find(std::begin(kProducts), std::end(kProducts), productId) == std::end(kProducts) ||
        !OmniGhost::Security::SecurityManager::Instance().CanUseProtectedFeatures())
        return false;
    if (WebsiteService().IsAuthenticated()) return WebsiteService().HasProduct(productId);
    return RemoteGateway().IsAuthenticated() && RemoteEntitlementGrants(productId);
}
std::string GameAccessDuration(std::string_view productId) {
    if (!IsRemoteAuthenticated()) return {};
    if (WebsiteService().IsAuthenticated()) return WebsiteService().ProductDuration(productId);
    const std::string product = NormalizeEntitlement(productId);
    const std::string canonical = "omnighost_" + product;
    for (const auto& item : RemoteGateway().CurrentEntitlements()) {
        const std::string entitlement = NormalizeEntitlement(item.name);
        if (IsUniversalEntitlement(entitlement) || entitlement == product || entitlement == canonical)
            return item.remaining.empty() ? "Ativo" : item.remaining;
    }
    return {};
}
bool HasAnyGameAccess() {
    constexpr std::string_view kProducts[]{"fivem", "cs2", "rust", "warzone", "apex", "fortnite"};
    if (WebsiteService().IsAuthenticated()) {
        if (WebsiteService().Snapshot().isAdmin) return true;
        return std::any_of(std::begin(kProducts), std::end(kProducts),
            [](std::string_view product) { return WebsiteService().HasProduct(product); });
    }
    return RemoteGateway().IsAuthenticated() &&
        std::any_of(std::begin(kProducts), std::end(kProducts), RemoteEntitlementGrants);
}
namespace {
std::filesystem::path RememberedRemotePath() {
    return OmniGhost::Paths::Configs() / L"keyauth_remember.bin";
}
}

bool SaveRememberedRemoteCredentials(std::string_view username, std::string_view password) {
    if (username.empty() || password.empty())
        return false;

    std::string payload;
    payload.reserve(username.size() + password.size() + 8);
    const uint32_t userLen = static_cast<uint32_t>(username.size());
    payload.append(reinterpret_cast<const char*>(&userLen), sizeof(userLen));
    payload.append(username.data(), username.size());
    payload.append(password.data(), password.size());
    return SaveProtected(RememberedRemotePath(), payload);
}

bool LoadRememberedRemoteCredentials(std::string& username, std::string& password) {
    username.clear();
    password.clear();
    const auto path = RememberedRemotePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec))
        return false;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::vector<unsigned char> blob((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    std::string payload;
    if (!UnprotectForCurrentUser(blob, payload) || payload.size() < sizeof(uint32_t) + 2)
        return false;
    uint32_t userLen = 0;
    std::memcpy(&userLen, payload.data(), sizeof(userLen));
    if (userLen == 0 || sizeof(uint32_t) + userLen >= payload.size())
        return false;
    username.assign(payload.data() + sizeof(uint32_t), userLen);
    password.assign(payload.data() + sizeof(uint32_t) + userLen,
                    payload.size() - sizeof(uint32_t) - userLen);
    return !username.empty() && !password.empty();
}

void ClearRememberedRemoteCredentials() noexcept {
    std::error_code ec;
    std::filesystem::remove(RememberedRemotePath(), ec);
}

}
