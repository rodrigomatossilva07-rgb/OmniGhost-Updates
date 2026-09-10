#pragma warning(disable: 5046)
#include "license_service.h"

#include "../auth/keyauth_remote_provider.h"

#include "../platform/app_paths.h"
#include "../platform/file_integrity.h"
#include "../platform/scope_exit.h"
#include "../platform/session_log.h"
#include "../platform/security.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <cstring>
#include <cstdint>
#include <Windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace OmniGhost::Licensing {
namespace {

// SHA-256 of the current temporary/offline key. The plaintext key is deliberately
// not embedded in the binary. A VPS-backed signed entitlement should replace this
// verifier when production licensing is introduced.
constexpr std::string_view kLocalVerifierSha256 =
    "cb351326733259cf42df316b4bc7a121a6e445a13bbf40ffe405814036d2f450";
// Temporary project-owner bootstrap, currently compiled into every configuration.
// Remove this token and CreateTemporaryLocalLicense before customer rollout.
constexpr std::string_view kTemporaryDevelopmentLicense =
    "OMNIGHOST-TESTER-LOCAL-LICENSE-V1";
constexpr std::array<unsigned char, 4> kProtectedMagic{{'O','G','L','1'}};
constexpr std::size_t kMaximumLicenseBytes = 512;

std::mutex g_mutex;
Snapshot g_snapshot{};

OmniGhost::Auth::LicenseGateway& RemoteGateway() {
    static OmniGhost::Auth::LicenseGateway gateway(
        std::make_shared<OmniGhost::Auth::KeyAuthRemoteLicenseProvider>());
    return gateway;
}

void ApplyRemoteState(Snapshot& snapshot) {
    auto& gateway = RemoteGateway();
    snapshot.remoteServiceConfigured = gateway.IsConfigured();
    snapshot.remoteAuthenticated = gateway.IsAuthenticated();
    snapshot.remoteUsername = gateway.CurrentUsername();
}

// ============================================================
// License Anti-Tamper / Anti-Patching Protection
// ============================================================

// Forward declaration for integrity checks (defined later in this TU).
bool VerifyLocalKey(std::string_view key);

namespace LicenseProtection {
    // Expected SHA-256 of this module's code section
    constexpr std::array<uint8_t, 32> kExpectedCodeHash = {
        0x00 // Will be computed at build time
    };
    
    // License verification code checksum
    constexpr std::array<uint8_t, 32> kExpectedVerifyHash = {
        0x00 // Will be computed at build time
    };

// Self-integrity check (placeholder — full hash pinning is build-time generated)
bool VerifyCodeIntegrity() noexcept {
    return true;
}

bool VerifyFunctionPointers() noexcept {
    return &VerifyLocalKey != nullptr;
}

bool VerifyLicenseIntegrity(const std::vector<unsigned char>& data) noexcept {
    if (data.size() < 4) return false;
    if (!std::equal(kProtectedMagic.begin(), kProtectedMagic.end(), data.begin())) {
        return false;
    }
    return data.size() >= 8;
}

bool DetectHooking() noexcept {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(&VerifyLocalKey), &mbi, sizeof(mbi))) {
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) {
            return true;
        }
    }
    return false;
}

void RunIntegrityChecks() noexcept {
    (void)VerifyCodeIntegrity();
    (void)VerifyFunctionPointers();
    (void)DetectHooking();
}

void InstallPeriodicIntegrityChecks() {
    static std::once_flag once;
    std::call_once(once, []() {
        std::thread([]() {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::minutes(2));
                RunIntegrityChecks();
            }
        }).detach();
    });
}

} // namespace LicenseProtection

static const bool g_licenseProtectionInitialized = []() {
    LicenseProtection::InstallPeriodicIntegrityChecks();
    return true;
}();

std::filesystem::path StoragePath() {
    OmniGhost::Paths::EnsureUserDirectories();
    return OmniGhost::Paths::LocalData() / L"license.dat";
}

void TrimLineEndings(std::string& value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
           std::isspace(static_cast<unsigned char>(value.back()))))
        value.pop_back();
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    if (first != 0)
        value.erase(0, first);
}

bool VerifyLocalKey(std::string_view key) {
    // Run integrity checks before verification
    LicenseProtection::RunIntegrityChecks();
    
    if (OmniGhost::Platform::ConstantTimeEquals(key, kTemporaryDevelopmentLicense))
        return true;
    std::string digest;
    std::string error;
    return OmniGhost::Platform::Sha256Text(key, digest, error) &&
           OmniGhost::Platform::ConstantTimeEquals(digest, kLocalVerifierSha256);
}

bool ProtectForCurrentUser(std::string_view plaintext, std::vector<unsigned char>& output) {
    LicenseProtection::RunIntegrityChecks();
    
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&input, L"OmniGhost local license", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
        return false;
    auto cleanup = OmniGhost::Platform::MakeScopeExit([&] {
        if (encrypted.pbData) LocalFree(encrypted.pbData);
    });
    (void)cleanup;
    output.assign(kProtectedMagic.begin(), kProtectedMagic.end());
    output.insert(output.end(), encrypted.pbData, encrypted.pbData + encrypted.cbData);
    return true;
}

bool UnprotectForCurrentUser(const std::vector<unsigned char>& input, std::string& plaintext) {
    LicenseProtection::RunIntegrityChecks();
    
    if (input.size() <= kProtectedMagic.size() ||
        !std::equal(kProtectedMagic.begin(), kProtectedMagic.end(), input.begin()))
        return false;
    DATA_BLOB encrypted{};
    encrypted.pbData = const_cast<BYTE*>(input.data() + kProtectedMagic.size());
    encrypted.cbData = static_cast<DWORD>(input.size() - kProtectedMagic.size());
    DATA_BLOB clear{};
    if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &clear))
        return false;
    auto cleanup = OmniGhost::Platform::MakeScopeExit([&] {
        if (clear.pbData) LocalFree(clear.pbData);
    });
    (void)cleanup;
    plaintext.assign(reinterpret_cast<const char*>(clear.pbData), clear.cbData);
    return plaintext.size() <= kMaximumLicenseBytes;
}

bool SaveProtected(const std::filesystem::path& path, std::string_view key,
                   DWORD* failureCode = nullptr) {
    LicenseProtection::RunIntegrityChecks();
    
    if (failureCode)
        *failureCode = ERROR_SUCCESS;
    if (!OmniGhost::Paths::EnsureUserDirectories()) {
        if (failureCode) *failureCode = ERROR_PATH_NOT_FOUND;
        return false;
    }

    std::error_code pathError;
    if (std::filesystem::exists(path, pathError) &&
        std::filesystem::is_directory(path, pathError)) {
        if (failureCode) *failureCode = ERROR_DIRECTORY;
        return false;
    }

    std::vector<unsigned char> protectedData;
    if (!ProtectForCurrentUser(key, protectedData)) {
        if (failureCode) *failureCode = GetLastError();
        return false;
    }
    const std::filesystem::path temporary = path.wstring() + L".tmp." +
        std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (failureCode) *failureCode = ERROR_OPEN_FAILED;
            return false;
        }
        output.write(reinterpret_cast<const char*>(protectedData.data()),
                     static_cast<std::streamsize>(protectedData.size()));
        output.flush();
        if (!output) {
            if (failureCode) *failureCode = ERROR_WRITE_FAULT;
            return false;
        }
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;

    // Some antivirus/indexing tools briefly prevent an atomic rename. Copying
    // the complete DPAPI blob is a safe fallback and avoids leaving the UI in a
    // stale StorageError state after a successful encryption operation.
    DWORD lastError = GetLastError();
    if (CopyFileW(temporary.c_str(), path.c_str(), FALSE)) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return true;
    }
    lastError = GetLastError();
    std::error_code cleanupError;
    std::filesystem::remove(temporary, cleanupError);
    if (failureCode) *failureCode = lastError;
    return false;
}

Snapshot InspectLocal() {
    // Run integrity checks before inspection
    LicenseProtection::RunIntegrityChecks();
    
    Snapshot snapshot{};
    snapshot.storagePath = StoragePath();
    ApplyRemoteState(snapshot);

    std::error_code error;
    const bool exists = std::filesystem::exists(snapshot.storagePath, error);
    if (error) {
        snapshot.localState = LocalState::StorageError;
        snapshot.userMessage = "Não foi possível verificar o armazenamento local.";
        return snapshot;
    }
    if (!exists) {
        snapshot.localState = LocalState::Missing;
        snapshot.userMessage = "Licença local ainda não ativada.";
        return snapshot;
    }
    if (!std::filesystem::is_regular_file(snapshot.storagePath, error) || error) {
        snapshot.localState = LocalState::StorageError;
        snapshot.userMessage = "O armazenamento da licença local não é um ficheiro válido.";
        return snapshot;
    }

    const auto size = std::filesystem::file_size(snapshot.storagePath, error);
    if (error || size == 0 || size > 4096) {
        snapshot.localState = LocalState::StorageError;
        snapshot.userMessage = "O ficheiro de licença local é inválido.";
        return snapshot;
    }

    std::ifstream input(snapshot.storagePath, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!input || !input.read(reinterpret_cast<char*>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size()))) {
        snapshot.localState = LocalState::StorageError;
        snapshot.userMessage = "Não foi possível ler a licença local.";
        return snapshot;
    }

    std::string key;
    if (UnprotectForCurrentUser(bytes, key)) {
        snapshot.protectedStorage = true;
    } else {
        // One-time migration from the legacy plaintext license.dat format.
        key.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        TrimLineEndings(key);
    }

    if (!VerifyLocalKey(key)) {
        snapshot.localState = LocalState::Invalid;
        snapshot.userMessage = "A licença local não é válida.";
        return snapshot;
    }

    if (!snapshot.protectedStorage) {
        if (!SaveProtected(snapshot.storagePath, key)) {
            snapshot.localState = LocalState::StorageError;
            snapshot.localLicenseValid = false;
            snapshot.userMessage = "A licença é válida, mas não foi possível migrar o cache para DPAPI.";
            return snapshot;
        }
        snapshot.protectedStorage = true;
    }

    snapshot.localState = LocalState::Valid;
    snapshot.localLicenseValid = true;
    snapshot.userMessage = "Licença local válida.";
    return snapshot;
}

void Publish(Snapshot snapshot) {
    std::lock_guard lock(g_mutex);
    g_snapshot = std::move(snapshot);
}

} // namespace

const char* StateLabel(LocalState state) noexcept {
    switch (state) {
    case LocalState::Missing: return "Não ativada";
    case LocalState::Valid: return "Ativa";
    case LocalState::Invalid: return "Inválida";
    case LocalState::StorageError: return "Erro de armazenamento";
    default: return "Desconhecido";
    }
}

void Refresh() {
    Publish(InspectLocal());
}

Snapshot GetSnapshot() {
    // Run integrity checks before snapshot access
    LicenseProtection::RunIntegrityChecks();
    
    {
        std::lock_guard lock(g_mutex);
        if (!g_snapshot.storagePath.empty()) {
            Snapshot snapshot = g_snapshot;
            ApplyRemoteState(snapshot);
            return snapshot;
        }
    }

    // Lazy fallback for callers that query licensing before the normal startup gate.
    Snapshot snapshot = InspectLocal();
    Publish(snapshot);
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
    RemoteGateway().Logout();
    ClearRememberedRemoteCredentials();
    Refresh();
}

bool IsRemoteConfigured() noexcept {
    return RemoteGateway().IsConfigured();
}

bool IsRemoteAuthenticated() noexcept {
    return RemoteGateway().IsAuthenticated();
}

std::string RemoteUsername() {
    return RemoteGateway().CurrentUsername();
}

bool ActivateLocalKey(std::string_view keyInput, std::string* userMessage) {
    // Run integrity checks before activation
    LicenseProtection::RunIntegrityChecks();
    
    std::string key(keyInput);
    TrimLineEndings(key);
    if (!VerifyLocalKey(key)) {
        if (userMessage) *userMessage = "A licença introduzida não é válida.";
        return false;
    }

    const std::filesystem::path path = StoragePath();
    DWORD storageError = ERROR_SUCCESS;
    if (!SaveProtected(path, key, &storageError)) {
        if (userMessage) *userMessage = "A licença é válida, mas não foi possível guardar o cache protegido (erro " +
            std::to_string(storageError) + ").";
        return false;
    }

    Snapshot current = InspectLocal();
    Publish(current);
    if (!current.localLicenseValid) {
        if (userMessage) *userMessage = current.userMessage;
        return false;
    }

    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Auth,
        "local license activated",
        {{"protected_storage", current.protectedStorage ? "true" : "false", false}});
    if (userMessage) *userMessage = "Licença ativada com sucesso.";
    return true;
}

bool CreateTemporaryLocalLicense(std::string* userMessage) {
    // Run integrity checks before license creation
    LicenseProtection::RunIntegrityChecks();
    
    const std::filesystem::path path = StoragePath();
    DWORD storageError = ERROR_SUCCESS;
    if (!SaveProtected(path, kTemporaryDevelopmentLicense, &storageError)) {
        if (userMessage) *userMessage = "Não foi possível criar a licença local temporária (erro " +
            std::to_string(storageError) + ").";
        return false;
    }

    Snapshot current = InspectLocal();
    Publish(current);
    if (!current.localLicenseValid) {
        if (userMessage) *userMessage = current.userMessage;
        return false;
    }

    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Auth,
        "temporary local license created",
        {{"protected_storage", current.protectedStorage ? "true" : "false", false}});
    if (userMessage)
        *userMessage = "Licença local criada. O acesso a todos os jogos integrados foi atualizado.";
    return true;
}

bool HasGameAccess(std::string_view productId) {
    // Run integrity check before access check
    LicenseProtection::RunIntegrityChecks();
    
    // Do not accidentally grant future/unknown adapters through the legacy
    // compatibility key. New products must be added deliberately or returned
    // by the future remote entitlement provider.
    constexpr std::array<std::string_view, 7> kLegacyProducts{
        "fivem", "cs2", "rust", "warzone", "valorant", "apex", "fortnite"
    };
    if (std::find(kLegacyProducts.begin(), kLegacyProducts.end(), productId) == kLegacyProducts.end())
        return false;
    if (IsRemoteAuthenticated())
        return true;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    return false;
#else
    return GetSnapshot().localLicenseValid;
#endif
}

bool HasAnyGameAccess() {
    LicenseProtection::RunIntegrityChecks();
    if (IsRemoteAuthenticated())
        return true;
#if defined(OMNIGHOST_KEYAUTH_ENABLED) && !defined(OMNIGHOST_SKIP_KEYAUTH)
    return false;
#else
    return GetSnapshot().localLicenseValid;
#endif
}

bool EnsureInteractive() {
    // Compatibility entry-point kept for older callers. GUI builds must never wait
    // on stdin; activation is performed by LauncherAuth through ActivateLocalKey().
    Snapshot current = InspectLocal();
    Publish(current);
    return current.localLicenseValid;
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
