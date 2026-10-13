#include "license_service.h"
#include "../auth/keyauth_remote_provider.h"
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
#include <filesystem>
#include <fstream>
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

// Caminho do executável atual. O LicenseGateway usa GetModulePath() para verificar a
// identidade do provider (SHA-256 do ficheiro); se vier vazio, o construtor lança
// std::runtime_error.
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

// Fallback provider used when license gateway initialization fails.
// Returns NotConfigured for all operations so the app can show a proper error message
// instead of crashing.
class FakeProviderForFallback final : public OmniGhost::Auth::IRemoteLicenseProvider {
public:
    [[nodiscard]] bool IsConfigured() const noexcept override { return false; }
    // Tem de devolver um caminho valido, senao o LicenseGateway lanca a mesma excecao.
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
            // Gateway "vazio": devolve NotConfigured em todas as operacoes, para a app
            // poder mostrar um erro no ecra de login em vez de fechar.
            // SEC-001 Fix: a construcao de reserva tambem corre dentro de try —
            // uma excecao aqui escaparia do call_once para accessors noexcept
            // (IsRemoteConfigured/IsRemoteAuthenticated) e terminaria o processo.
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
            // Last resort: should be unreachable after SEC-001 (the fallback
            // provider is in-process and never fails identity verification),
            // but never dereference a null gateway.
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
    // Construct the gateway before the worker so the worker is destroyed first.
    auto& gateway = RemoteGateway();
    std::scoped_lock lock(RevalidatorMutex());
    auto& worker = RevalidatorThread();
    if (worker.joinable()) return;
    worker = std::jthread([&gateway](std::stop_token stop) {
        std::mutex waitMutex;
        std::condition_variable_any wake;
        std::unique_lock waitLock(waitMutex);
        bool degraded = false;
        while (!stop.stop_requested()) {
            if (wake.wait_for(waitLock, stop, std::chrono::minutes(15), [] { return false; })) break;
            if (stop.stop_requested()) break;
            waitLock.unlock();
            const auto result = gateway.Revalidate(stop);
            if (stop.stop_requested() || result.status == OmniGhost::Auth::LicenseStatus::Cancelled)
                break;
            if (!result.Ok()) {
                degraded = true;
                OmniGhost::Security::SecurityManager::Instance().Record(
                    result.status == OmniGhost::Auth::LicenseStatus::Timeout ||
                    result.status == OmniGhost::Auth::LicenseStatus::Unavailable
                        ? OmniGhost::Security::EventId::SessionUnavailable
                        : OmniGhost::Security::EventId::SessionInvalid);
            } else if (degraded) {
                degraded = false;
                OmniGhost::Security::SecurityManager::Instance().Record(
                    OmniGhost::Security::EventId::SessionRestored);
            }
            waitLock.lock();
        }
    });
}
void ApplyRemoteState(Snapshot& snapshot) {
    auto& gateway = RemoteGateway();
    snapshot.remoteServiceConfigured = gateway.IsConfigured();
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
// Preserve the existing DPAPI envelope so remembered KeyAuth logins survive upgrades.
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
} // namespace

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
OmniGhost::Auth::LicenseResult Login(std::string_view username, std::string_view password) {
    return RemoteGateway().Login(username, password);
}
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
    StopRevalidator();
    RemoteGateway().Logout();
    ClearRememberedRemoteCredentials();
    Refresh();
}
void StartSessionRevalidation() { StartRevalidator(); }
void ShutdownSessionRevalidation() noexcept { StopRevalidator(); }
bool IsRemoteConfigured() noexcept { return RemoteGateway().IsConfigured(); }
bool IsRemoteAuthenticated() noexcept { return RemoteGateway().IsAuthenticated(); }
std::string RemoteUsername() { return RemoteGateway().CurrentUsername(); }
bool IsAdmin() {
    if (!IsRemoteAuthenticated() ||
        !OmniGhost::Security::SecurityManager::Instance().CanUseProtectedFeatures())
        return false;
    const auto entitlements = RemoteGateway().CurrentEntitlements();
    return std::any_of(entitlements.begin(), entitlements.end(), [](const auto& item) {
        return IsAdminEntitlement(NormalizeEntitlement(item.name));
    });
}
bool HasGameAccess(std::string_view productId) {
    constexpr std::string_view kProducts[]{"fivem", "cs2", "rust", "warzone", "apex", "fortnite"};
    return std::find(std::begin(kProducts), std::end(kProducts), productId) != std::end(kProducts) &&
           OmniGhost::Security::SecurityManager::Instance().CanUseProtectedFeatures() &&
           IsRemoteAuthenticated() && RemoteEntitlementGrants(productId);
}
std::string GameAccessDuration(std::string_view productId) {
    if (!IsRemoteAuthenticated()) return {};
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
    return IsRemoteAuthenticated() &&
           std::any_of(std::begin(kProducts), std::end(kProducts), RemoteEntitlementGrants);
}
namespace {
std::filesystem::path RememberedRemotePath() {
    return OmniGhost::Paths::Configs() / L"keyauth_remember.bin";
}
} // namespace

bool SaveRememberedRemoteCredentials(std::string_view username, std::string_view password) {
    if (username.empty() || password.empty())
        return false;
    // username length + username + NUL + password
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

} // namespace OmniGhost::Licensing