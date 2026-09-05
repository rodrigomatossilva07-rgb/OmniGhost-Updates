#include "local_auth_service.h"

#include "../platform/app_paths.h"
#include "../platform/scope_exit.h"
#include "../platform/session_log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace OmniGhost::Auth {
namespace {

constexpr std::array<std::uint8_t, 4> kFileMagic{{'O', 'G', 'A', '1'}};
constexpr std::array<std::uint8_t, 4> kPayloadMagic{{'A', 'U', 'T', 'H'}};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kPbkdf2Iterations = 120000;
constexpr std::size_t kSaltSize = 16;
constexpr std::size_t kHashSize = 32;
constexpr std::size_t kMaxEmailBytes = 320;
constexpr std::size_t kMaxPasswordBytes = 1024;

template <typename T>
void AppendPod(std::vector<std::uint8_t>& out, const T& value) {
    const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), first, first + sizeof(T));
}

template <typename T>
bool ReadPod(const std::vector<std::uint8_t>& in, std::size_t& offset, T& value) {
    if (offset > in.size() || in.size() - offset < sizeof(T))
        return false;
    std::memcpy(&value, in.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

bool Protect(const std::vector<std::uint8_t>& plaintext, std::vector<std::uint8_t>& output) {
    DATA_BLOB input{};
    input.pbData = const_cast<BYTE*>(plaintext.data());
    input.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&input, L"OmniGhost local account", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
        return false;
    auto cleanup = OmniGhost::Platform::MakeScopeExit([&] {
        if (encrypted.pbData) LocalFree(encrypted.pbData);
    });
    (void)cleanup;
    output.assign(kFileMagic.begin(), kFileMagic.end());
    output.insert(output.end(), encrypted.pbData, encrypted.pbData + encrypted.cbData);
    return true;
}

bool Unprotect(const std::vector<std::uint8_t>& input, std::vector<std::uint8_t>& output) {
    if (input.size() <= kFileMagic.size() ||
        !std::equal(kFileMagic.begin(), kFileMagic.end(), input.begin()))
        return false;

    DATA_BLOB encrypted{};
    encrypted.pbData = const_cast<BYTE*>(input.data() + kFileMagic.size());
    encrypted.cbData = static_cast<DWORD>(input.size() - kFileMagic.size());
    DATA_BLOB clear{};
    if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &clear))
        return false;
    auto cleanup = OmniGhost::Platform::MakeScopeExit([&] {
        if (clear.pbData) SecureZeroMemory(clear.pbData, clear.cbData);
        if (clear.pbData) LocalFree(clear.pbData);
    });
    (void)cleanup;
    output.assign(clear.pbData, clear.pbData + clear.cbData);
    return true;
}

bool DerivePassword(std::string_view password,
                    const std::array<std::uint8_t, kSaltSize>& salt,
                    std::uint32_t iterations,
                    std::array<std::uint8_t, kHashSize>& output) {
    if (password.empty() || password.size() > kMaxPasswordBytes)
        return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
        return false;
    auto cleanup = OmniGhost::Platform::MakeScopeExit([&] {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    });
    (void)cleanup;

    return BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()),
        static_cast<ULONG>(salt.size()),
        iterations,
        output.data(),
        static_cast<ULONG>(output.size()),
        0) >= 0;
}

bool RandomSalt(std::array<std::uint8_t, kSaltSize>& salt) {
    return BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

bool ConstantTimeEqual(const std::array<std::uint8_t, kHashSize>& a,
                       const std::array<std::uint8_t, kHashSize>& b) {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

} // namespace

struct LocalAuthService::StoredAccount {
    std::string email;
    std::array<std::uint8_t, kSaltSize> salt{};
    std::array<std::uint8_t, kHashSize> hash{};
    std::uint32_t iterations = kPbkdf2Iterations;
    bool rememberMe = false;
};

LocalAuthService& LocalAuthService::Instance() {
    static LocalAuthService service;
    return service;
}

std::filesystem::path LocalAuthService::AccountPath() const {
#if defined(OMNIGHOST_TESTING)
    if (!storagePathForTesting_.empty())
        return storagePathForTesting_;
#endif
    OmniGhost::Paths::EnsureUserDirectories();
    return OmniGhost::Paths::LocalData() / L"auth.dat";
}

#if defined(OMNIGHOST_TESTING)
void LocalAuthService::SetStoragePathForTesting(std::filesystem::path path) {
    storagePathForTesting_ = std::move(path);
    initialized_ = false;
    authenticated_ = false;
    hasAccount_ = false;
    rememberMe_ = false;
    currentEmail_.clear();
}
#endif

std::string LocalAuthService::NormalizeEmail(std::string_view input) {
    std::size_t first = 0;
    std::size_t last = input.size();
    while (first < last && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;
    std::string value(input.substr(first, last - first));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool LocalAuthService::IsEmailValid(std::string_view input) {
    const std::string email = NormalizeEmail(input);
    if (email.size() < 5 || email.size() > kMaxEmailBytes || email.find(' ') != std::string::npos)
        return false;
    const std::size_t at = email.find('@');
    if (at == std::string::npos || at == 0 || at + 3 >= email.size() || email.find('@', at + 1) != std::string::npos)
        return false;
    const std::size_t dot = email.find('.', at + 2);
    return dot != std::string::npos && dot + 1 < email.size();
}

void LocalAuthService::Initialize() {
    StoredAccount account{};
    hasAccount_ = Load(account);
    rememberMe_ = hasAccount_ && account.rememberMe;
    authenticated_ = false;
    currentEmail_.clear();
    initialized_ = true;
}

bool LocalAuthService::HasAccount() const { return hasAccount_; }
bool LocalAuthService::IsAuthenticated() const { return authenticated_; }
bool LocalAuthService::RememberMe() const { return rememberMe_; }
const std::string& LocalAuthService::CurrentEmail() const { return currentEmail_; }

bool LocalAuthService::Load(StoredAccount& account) const {
    const auto path = AccountPath();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return false;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size <= kFileMagic.size() || size > 8192)
        return false;

    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> encrypted(static_cast<std::size_t>(size));
    if (!file || !file.read(reinterpret_cast<char*>(encrypted.data()), static_cast<std::streamsize>(encrypted.size())))
        return false;

    std::vector<std::uint8_t> plain;
    if (!Unprotect(encrypted, plain))
        return false;
    auto wipe = OmniGhost::Platform::MakeScopeExit([&] {
        if (!plain.empty()) SecureZeroMemory(plain.data(), plain.size());
    });
    (void)wipe;

    std::size_t offset = 0;
    if (plain.size() < kPayloadMagic.size() ||
        !std::equal(kPayloadMagic.begin(), kPayloadMagic.end(), plain.begin()))
        return false;
    offset += kPayloadMagic.size();

    std::uint32_t version = 0;
    std::uint32_t emailLength = 0;
    std::uint8_t remember = 0;
    if (!ReadPod(plain, offset, version) || version != kVersion ||
        !ReadPod(plain, offset, account.iterations) ||
        !ReadPod(plain, offset, emailLength) || emailLength == 0 || emailLength > kMaxEmailBytes)
        return false;
    if (offset + emailLength + account.salt.size() + account.hash.size() + sizeof(remember) > plain.size())
        return false;

    account.email.assign(reinterpret_cast<const char*>(plain.data() + offset), emailLength);
    offset += emailLength;
    std::memcpy(account.salt.data(), plain.data() + offset, account.salt.size());
    offset += account.salt.size();
    std::memcpy(account.hash.data(), plain.data() + offset, account.hash.size());
    offset += account.hash.size();
    if (!ReadPod(plain, offset, remember))
        return false;
    account.rememberMe = remember != 0;
    return IsEmailValid(account.email) && account.iterations >= 10000 && account.iterations <= 1000000;
}

bool LocalAuthService::Save(const StoredAccount& account) const {
    std::vector<std::uint8_t> plain;
    plain.reserve(128 + account.email.size());
    plain.insert(plain.end(), kPayloadMagic.begin(), kPayloadMagic.end());
    AppendPod(plain, kVersion);
    AppendPod(plain, account.iterations);
    const auto emailLength = static_cast<std::uint32_t>(account.email.size());
    AppendPod(plain, emailLength);
    plain.insert(plain.end(), account.email.begin(), account.email.end());
    plain.insert(plain.end(), account.salt.begin(), account.salt.end());
    plain.insert(plain.end(), account.hash.begin(), account.hash.end());
    const std::uint8_t remember = account.rememberMe ? 1 : 0;
    AppendPod(plain, remember);

    std::vector<std::uint8_t> encrypted;
    if (!Protect(plain, encrypted)) {
        if (!plain.empty()) SecureZeroMemory(plain.data(), plain.size());
        return false;
    }
    if (!plain.empty()) SecureZeroMemory(plain.data(), plain.size());

    const auto path = AccountPath();
    const auto temporary = path.wstring() + L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(reinterpret_cast<const char*>(encrypted.data()), static_cast<std::streamsize>(encrypted.size()));
        file.flush();
        if (!file) return false;
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    std::error_code error;
    std::filesystem::remove(temporary, error);
    return false;
}

Result LocalAuthService::Register(std::string_view emailInput, std::string_view password, bool rememberMe) {
    if (!initialized_) Initialize();
    if (hasAccount_)
        return {ResultCode::AccountAlreadyExists, "Já existe uma conta local neste dispositivo."};
    const std::string email = NormalizeEmail(emailInput);
    if (!IsEmailValid(email) || password.size() < 6 || password.size() > kMaxPasswordBytes)
        return {ResultCode::InvalidInput, "Usa um email válido e uma palavra-passe com pelo menos 6 caracteres."};

    StoredAccount account{};
    account.email = email;
    account.iterations = kPbkdf2Iterations;
    account.rememberMe = rememberMe;
    if (!RandomSalt(account.salt) || !DerivePassword(password, account.salt, account.iterations, account.hash))
        return {ResultCode::StorageError, "Não foi possível preparar a conta local."};
    if (!Save(account))
        return {ResultCode::StorageError, "Não foi possível guardar a conta local."};

    hasAccount_ = true;
    rememberMe_ = rememberMe;
    authenticated_ = true;
    currentEmail_ = email;
    OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Auth, "local account created");
    return {ResultCode::Success, "Conta criada com sucesso."};
}

Result LocalAuthService::Login(std::string_view emailInput, std::string_view password, bool rememberMe) {
    if (!initialized_) Initialize();
    StoredAccount account{};
    if (!Load(account)) {
        hasAccount_ = false;
        return {ResultCode::AccountNotFound, "Ainda não existe uma conta local."};
    }
    hasAccount_ = true;
    const std::string email = NormalizeEmail(emailInput);
    if (!IsEmailValid(email) || password.empty())
        return {ResultCode::InvalidInput, "Introduz o email e a palavra-passe."};

    std::array<std::uint8_t, kHashSize> candidate{};
    const bool derived = DerivePassword(password, account.salt, account.iterations, candidate);
    const bool matches = derived && email == account.email && ConstantTimeEqual(candidate, account.hash);
    SecureZeroMemory(candidate.data(), candidate.size());
    if (!matches) {
        OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::Auth, "local login failed");
        return {ResultCode::InvalidCredentials, "Email ou palavra-passe incorretos."};
    }

    account.rememberMe = rememberMe;
    if (!Save(account))
        return {ResultCode::StorageError, "Login válido, mas não foi possível guardar a preferência."};

    authenticated_ = true;
    rememberMe_ = rememberMe;
    currentEmail_ = account.email;
    OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Auth, "local login succeeded");
    return {ResultCode::Success, "Login efetuado."};
}

bool LocalAuthService::TryAutoLogin() {
    if (!initialized_) Initialize();
    StoredAccount account{};
    if (!Load(account) || !account.rememberMe)
        return false;
    hasAccount_ = true;
    rememberMe_ = true;
    authenticated_ = true;
    currentEmail_ = account.email;
    OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Auth, "local auto-login succeeded");
    return true;
}

void LocalAuthService::Logout(bool forgetRememberMe) {
    StoredAccount account{};
    if (forgetRememberMe && Load(account)) {
        account.rememberMe = false;
        if (!Save(account)) {
            OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Warning,
                OmniGhost::SessionLog::Subsystem::Auth,
                "failed to persist remember-me=false during logout");
        }
        rememberMe_ = false;
    }
    authenticated_ = false;
    currentEmail_.clear();
    OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Auth, "local logout");
}

} // namespace OmniGhost::Auth
