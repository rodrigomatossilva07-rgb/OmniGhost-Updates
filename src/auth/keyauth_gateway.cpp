#include "keyauth_gateway.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace OmniGhost::Auth {
namespace {

LicenseResult Result(LicenseStatus status, const char* userMessage, const char* code) {
    return {status, userMessage, code, {}};
}

// SHA-256 constants (FIPS 180-4)
static constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// SHA-256 implementation
class Sha256 {
public:
    Sha256() { Reset(); }

    void Reset() {
        state_[0] = 0x6a09e667;
        state_[1] = 0xbb67ae85;
        state_[2] = 0x3c6ef372;
        state_[3] = 0xa54ff53a;
        state_[4] = 0x510e527f;
        state_[5] = 0x9b05688c;
        state_[6] = 0x1f83d9ab;
        state_[7] = 0x5be0cd19;
        bitCount_ = 0;
        bufferPos_ = 0;
    }

    void Update(const std::uint8_t* data, size_t len) {
        while (len > 0) {
            size_t space = 64 - bufferPos_;
            size_t chunk = (len < space) ? len : space;
            std::memcpy(buffer_ + bufferPos_, data, chunk);
            bufferPos_ += chunk;
            data += chunk;
            len -= chunk;
            bitCount_ += static_cast<std::uint64_t>(chunk) * 8;

            if (bufferPos_ == 64) {
                ProcessBlock();
                bufferPos_ = 0;
            }
        }
    }

    void Finalize(std::array<std::byte, 32>& outHash) {
        // Pad with 0x80 then zeros, then length in bits (big-endian)
        std::uint8_t pad[64] = {0};
        pad[0] = 0x80;
        
        size_t padLen = (bufferPos_ < 56) ? (56 - bufferPos_) : (64 + 56 - bufferPos_);
        Update(pad, padLen);

        // Append bit count as big-endian 64-bit
        std::uint8_t lengthBytes[8];
        for (int i = 0; i < 8; ++i) {
            lengthBytes[i] = static_cast<std::uint8_t>((bitCount_ >> (56 - i * 8)) & 0xFF);
        }
        Update(lengthBytes, 8);

        // Output hash
        for (int i = 0; i < 8; ++i) {
            outHash[i * 4 + 0] = static_cast<std::byte>((state_[i] >> 24) & 0xFF);
            outHash[i * 4 + 1] = static_cast<std::byte>((state_[i] >> 16) & 0xFF);
            outHash[i * 4 + 2] = static_cast<std::byte>((state_[i] >> 8) & 0xFF);
            outHash[i * 4 + 3] = static_cast<std::byte>(state_[i] & 0xFF);
        }
    }

private:
    void ProcessBlock() {
        std::uint32_t w[64];
        
        // Prepare message schedule
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(buffer_[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(buffer_[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(buffer_[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(buffer_[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
            std::uint32_t S0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    static constexpr std::uint32_t ROTR(std::uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    std::uint32_t state_[8];
    std::uint64_t bitCount_ = 0;
    std::uint8_t buffer_[64];
    size_t bufferPos_ = 0;
};

// Compute SHA-256 hash of a file
std::optional<std::array<std::byte, 32>> ComputeFileSHA256(std::string_view filePath) {
    std::ifstream file(filePath.data(), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    Sha256 sha256;
    constexpr size_t kBufferSize = 64 * 1024;
    std::vector<std::uint8_t> buffer(kBufferSize);

    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), kBufferSize);
        std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0) {
            sha256.Update(buffer.data(), static_cast<size_t>(bytesRead));
        }
    }

    if (!file && !file.eof()) {
        return std::nullopt; // Read error
    }

    std::array<std::byte, 32> hash;
    sha256.Finalize(hash);
    return hash;
}

} // namespace

std::optional<ProviderIdentity> ComputeProviderIdentity(std::string_view modulePath) {
    auto hash = ComputeFileSHA256(modulePath);
    if (!hash) {
        return std::nullopt;
    }
    ProviderIdentity identity;
    identity.moduleHash = *hash;
    identity.version = "1.0.0"; // Would be read from version resource in production
    return identity;
}

LicenseGateway::LicenseGateway(std::shared_ptr<IRemoteLicenseProvider> provider)
    : provider_(std::move(provider)) {
    if (!provider_) {
        throw std::runtime_error("LicenseGateway: provider cannot be null");
    }

    // Verify provider identity
    const std::string modulePath = provider_->GetModulePath();
    if (modulePath.empty()) {
        throw std::runtime_error("LicenseGateway: provider must implement GetModulePath() for identity verification");
    }

    auto identity = ComputeProviderIdentity(modulePath);
    if (!identity) {
        throw std::runtime_error("LicenseGateway: failed to compute provider identity hash for " + modulePath);
    }

    // Verify against official identity
    if (*identity != kOfficialProviderIdentity) {
        // In production, this would be a hard failure. For development, log warning.
        // TODO: Remove this fallback in production builds.
        #if defined(OMNIGHOST_PUBLISH_BUILD)
        throw std::runtime_error("LicenseGateway: provider identity verification failed - module hash mismatch for " + modulePath);
        #else
        // Development build: log warning but continue
        std::cerr << "[LICENSE][WARNING] Provider identity mismatch for " << modulePath 
                  << " - expected: " << std::hex << std::uppercase;
        for (auto b : kOfficialProviderIdentity.moduleHash) std::cerr << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        std::cerr << ", got: ";
        for (auto b : identity->moduleHash) std::cerr << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        std::cerr << std::dec << std::nouppercase << std::endl;
        #endif
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
    currentUsername_.clear();
    currentEntitlements_.clear();
}

bool LicenseGateway::IsAuthenticated() const noexcept {
    std::scoped_lock lock(mutex_);
    return authenticated_ &&
        std::chrono::steady_clock::now() - lastRemoteValidation_ <= std::chrono::minutes(20);
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
        currentUsername_ = remoteUsername;
        currentEntitlements_ = remoteEntitlements;
    } else if (result.status != LicenseStatus::Timeout &&
               result.status != LicenseStatus::Unavailable &&
               result.status != LicenseStatus::Cancelled) {
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
        lastRemoteValidation_ = std::chrono::steady_clock::now();
    }
    return result;
}

LicenseResult LicenseGateway::Login(std::string_view username, std::string_view password,
                                    std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Autenticacao cancelada.", "auth.cancelled");
    if (username.empty() || password.empty())
        return Result(LicenseStatus::Invalid, "Introduz o utilizador e a palavra-passe.", "auth.invalid_input");
    return CompleteRemoteOperation(provider_ ? provider_->Login(username, password, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth nao esta configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Register(std::string_view username, std::string_view password,
                                       std::string_view licenseKey, std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Registo cancelado.", "auth.cancelled");
    if (username.empty() || password.empty() || licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz utilizador, palavra-passe e chave de licenca.", "auth.invalid_input");
    return CompleteRemoteOperation(provider_ ? provider_->Register(username, password, licenseKey, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth nao esta configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Activate(std::string_view licenseKey, std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Ativacao cancelada.", "auth.cancelled");
    if (licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz uma chave de licenca.", "auth.invalid_input");
    return CompleteRemoteOperation(provider_ ? provider_->Activate(licenseKey, stopToken)
        : Result(LicenseStatus::NotConfigured, "KeyAuth nao esta configurado.", "auth.not_configured"));
}

LicenseResult LicenseGateway::Upgrade(std::string_view username, std::string_view licenseKey,
                                      std::stop_token stopToken) {
    if (stopToken.stop_requested())
        return Result(LicenseStatus::Cancelled, "Upgrade cancelado.", "auth.cancelled");
    if (username.empty() || licenseKey.empty())
        return Result(LicenseStatus::Invalid, "Introduz o utilizador e a chave de licenca.", "auth.invalid_input");
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

void LicenseGateway::SetOfflineGrantForTesting(
    std::string hardwareId, std::chrono::steady_clock::time_point expiresAt) {
    std::scoped_lock lock(mutex_);
    offlineHardwareId_ = std::move(hardwareId);
    offlineExpiresAt_ = expiresAt;
}

} // namespace OmniGhost::Auth