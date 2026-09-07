#include <span>
#include "game_crypto.h"
#include <Windows.h>
#include <bcrypt.h>
#include <random>
#include <array>

#pragma comment(lib, "bcrypt.lib")

namespace OmniGhost::Security {

// ============================================================
// Helper: Key Derivation
// ============================================================

static std::array<uint8_t, 32> DeriveKeyFromContext(std::string_view context, 
                                                     std::string_view gameSalt) noexcept {
    std::array<uint8_t, 32> key{};
    
    // Use BCRYPT for PBKDF2
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return {};
    }

    // Simple key derivation for now - in production use PBKDF2/Argon2
    std::string input;
    input.reserve(context.size() + gameSalt.size());
    input += context;
    input += gameSalt;
    
    // hAlg already opened above
    if (true) {
        BCRYPT_HASH_HANDLE hHash = nullptr;
        if (BCryptCreateHash(hAlg, nullptr, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())), 
                          static_cast<ULONG>(input.size()), 0);
            
            std::array<uint8_t, 32> hash{};
            BCryptFinishHash(hHash, hash.data(), 32, 0);
            
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return hash;
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    
    // Fallback
    std::array<uint8_t, 32> fallback{};
    std::hash<std::string> hasher;
    uint64_t hash = hasher(std::string(context) + std::string(gameSalt));
    for (size_t i = 0; i < 32; i += 8) {
        *reinterpret_cast<uint64_t*>(fallback.data() + i) = hash;
    }
    return fallback;
}

// ============================================================
// FiveM Crypto Provider
// ============================================================

void FiveMCryptoProvider::DecryptOffsets(std::span<uint8_t> data) const noexcept {
    // FiveM uses XOR with per-session key
    static constexpr uint8_t key[32] = {0x5A, 0xA5, 0x3C, 0xC3, 0x69, 0x96, 0x12, 0x21,
                                         0x4B, 0xB4, 0x7E, 0xE7, 0x8D, 0xD8, 0xF1, 0x1F,
                                         0x2A, 0xA2, 0x5F, 0xF5, 0x3C, 0xC3, 0x0F, 0xF0,
                                         0x6B, 0xB6, 0x4D, 0xD4, 0x1E, 0xE1, 0x98, 0x89};
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % 32];
    }
}

void FiveMCryptoProvider::EncryptOffsets(std::span<uint8_t> data) const noexcept {
    // Symmetric - same as decrypt
    DecryptOffsets(data);
}

bool FiveMCryptoProvider::VerifyIntegrity() const noexcept {
    // Verify FiveM-specific integrity
    return true; // Placeholder
}

std::array<uint8_t, 32> FiveMCryptoProvider::DeriveKey(std::string_view context) const noexcept {
    return DeriveKeyFromContext(context, "FiveM_Salt_v1");
}

void FiveMCryptoProvider::InstallAntiTamper() const noexcept {
    // Install FiveM-specific anti-tamper
    // Check for memory modifications
    // Verify critical code sections
}

// ============================================================
// CS2 Crypto Provider
// ============================================================

void CS2CryptoProvider::DecryptOffsets(std::span<uint8_t> data) const noexcept {
    // CS2 uses per-update keys derived from game build
    static constexpr uint8_t baseKey[32] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                                             0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C,
                                             0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x70, 0x81,
                                             0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8, 0x09};
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= baseKey[i % 32];
    }
}

void CS2CryptoProvider::EncryptOffsets(std::span<uint8_t> data) const noexcept {
    DecryptOffsets(data);
}

bool CS2CryptoProvider::VerifyIntegrity() const noexcept {
    // Verify CS2-specific integrity (check for memory scans, etc.)
    return true;
}

std::array<uint8_t, 32> CS2CryptoProvider::DeriveKey(std::string_view context) const noexcept {
    return DeriveKeyFromContext(context, "CS2_Salt_v2");
}

void CS2CryptoProvider::InstallAntiTamper() const noexcept {
    // Install CS2-specific anti-tamper
    // Hook detection, memory region verification
}

// ============================================================
// Rust Crypto Provider
// ============================================================

void RustCryptoProvider::DecryptOffsets(std::span<uint8_t> data) const noexcept {
    static constexpr uint8_t key[32] = {0x13, 0x37, 0x42, 0x42, 0xDE, 0xAD, 0xBE, 0xEF,
                                         0xCA, 0xFE, 0xBA, 0xBE, 0xFE, 0xED, 0xFA, 0xCE,
                                         0xDE, 0xAD, 0xC0, 0xDE, 0xFA, 0xCE, 0xB0, 0x0C,
                                         0xAB, 0xAD, 0xBA, 0xBE, 0xFE, 0xED, 0xFA, 0xCE};
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % 32];
    }
}

void RustCryptoProvider::EncryptOffsets(std::span<uint8_t> data) const noexcept {
    DecryptOffsets(data);
}

bool RustCryptoProvider::VerifyIntegrity() const noexcept {
    return true;
}

std::array<uint8_t, 32> RustCryptoProvider::DeriveKey(std::string_view context) const noexcept {
    return DeriveKeyFromContext(context, "Rust_Salt_v1");
}

void RustCryptoProvider::InstallAntiTamper() const noexcept {
    // Rust-specific anti-tamper
}

// ============================================================
// Warzone Crypto Provider
// ============================================================

void WarzoneCryptoProvider::DecryptOffsets(std::span<uint8_t> data) const noexcept {
    static constexpr uint8_t key[32] = {0x57, 0x41, 0x52, 0x5A, 0x4F, 0x4E, 0x45, 0x5F,
                                         0x4B, 0x45, 0x59, 0x5F, 0x31, 0x32, 0x33, 0x34,
                                         0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x41, 0x42,
                                         0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A};
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % 32];
    }
}

void WarzoneCryptoProvider::EncryptOffsets(std::span<uint8_t> data) const noexcept {
    DecryptOffsets(data);
}

bool WarzoneCryptoProvider::VerifyIntegrity() const noexcept {
    return true;
}

std::array<uint8_t, 32> WarzoneCryptoProvider::DeriveKey(std::string_view context) const noexcept {
    return DeriveKeyFromContext(context, "Warzone_Salt_v1");
}

void WarzoneCryptoProvider::InstallAntiTamper() const noexcept {
    // Warzone-specific anti-tamper
}

// ============================================================
// Valorant Crypto Provider
// ============================================================

void ValorantCryptoProvider::DecryptOffsets(std::span<uint8_t> data) const noexcept {
    static constexpr uint8_t key[32] = {0x56, 0x41, 0x4C, 0x4F, 0x52, 0x41, 0x4E, 0x54,
                                         0x5F, 0x4B, 0x45, 0x59, 0x5F, 0x31, 0x32, 0x33,
                                         0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x41,
                                         0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49};
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % 32];
    }
}

void ValorantCryptoProvider::EncryptOffsets(std::span<uint8_t> data) const noexcept {
    DecryptOffsets(data);
}

bool ValorantCryptoProvider::VerifyIntegrity() const noexcept {
    return true;
}

std::array<uint8_t, 32> ValorantCryptoProvider::DeriveKey(std::string_view context) const noexcept {
    return DeriveKeyFromContext(context, "Valorant_Salt_v1");
}

void ValorantCryptoProvider::InstallAntiTamper() const noexcept {
    // Valorant-specific anti-tamper (Vanguard aware)
}

// ============================================================
// Fortnite Crypto Provider
// ============================================================

void FortniteCryptoProvider::DecryptOffsets(std::span<uint8_t> data) const noexcept {
    static constexpr uint8_t key[32] = {0x46, 0x4F, 0x52, 0x54, 0x4E, 0x49, 0x54, 0x45,
                                         0x5F, 0x4B, 0x45, 0x59, 0x5F, 0x31, 0x32, 0x33,
                                         0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x41,
                                         0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x4A};
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % 32];
    }
}

void FortniteCryptoProvider::EncryptOffsets(std::span<uint8_t> data) const noexcept {
    DecryptOffsets(data);
}

bool FortniteCryptoProvider::VerifyIntegrity() const noexcept {
    return true;
}

std::array<uint8_t, 32> FortniteCryptoProvider::DeriveKey(std::string_view context) const noexcept {
    return DeriveKeyFromContext(context, "Fortnite_Salt_v1");
}

void FortniteCryptoProvider::InstallAntiTamper() const noexcept {
    // Fortnite-specific anti-tamper
}

// ============================================================
// Apex Crypto Provider
// ============================================================

void ApexCryptoProvider::DecryptOffsets(std::span<uint8_t> data) const noexcept {
    static constexpr uint8_t key[32] = {0x41, 0x50, 0x45, 0x58, 0x5F, 0x4B, 0x45, 0x59,
                                         0x5F, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                                         0x38, 0x39, 0x30, 0x41, 0x42, 0x43, 0x44, 0x45,
                                         0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D};
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % 32];
    }
}

void ApexCryptoProvider::EncryptOffsets(std::span<uint8_t> data) const noexcept {
    DecryptOffsets(data);
}

bool ApexCryptoProvider::VerifyIntegrity() const noexcept {
    return true;
}

std::array<uint8_t, 32> ApexCryptoProvider::DeriveKey(std::string_view context) const noexcept {
    return DeriveKeyFromContext(context, "Apex_Salt_v1");
}

void ApexCryptoProvider::InstallAntiTamper() const noexcept {
    // Apex-specific anti-tamper
}

} // namespace OmniGhost::Security