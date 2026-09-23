#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <span>
#include <shared_mutex>
#include <functional>
#include <vector>

namespace OmniGhost::Security {

// ============================================================
// Game-Specific Cryptographic Isolation
// ============================================================

enum class GameId : uint8_t {
    FiveM = 0,
    CS2 = 1,
    Warzone = 3,
    Fortnite = 5,
    Apex = 6
};

class GameCryptoProvider {
public:
    virtual ~GameCryptoProvider() = default;
    
    // Game-specific decryption
    virtual void DecryptOffsets(std::span<uint8_t> data) const noexcept = 0;
    virtual void EncryptOffsets(std::span<uint8_t> data) const noexcept = 0;
    
    // Game-specific integrity checks
    virtual bool VerifyIntegrity() const noexcept = 0;
    
    // Game-specific key derivation
    virtual std::array<uint8_t, 32> DeriveKey(std::string_view context) const noexcept = 0;
    
    // Game-specific anti-tamper
    virtual void InstallAntiTamper() const noexcept = 0;
    
    [[nodiscard]] virtual GameId GetGameId() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetGameName() const noexcept = 0;
};

// ============================================================
// Crypto Registry
// ============================================================

class CryptoRegistry {
public:
    static CryptoRegistry& Instance() noexcept {
        static CryptoRegistry instance;
        return instance;
    }
    
    void RegisterProvider(std::unique_ptr<GameCryptoProvider> provider) {
        std::lock_guard lock(mutex_);
        providers_[provider->GetGameId()] = std::move(provider);
    }
    
    [[nodiscard]] GameCryptoProvider* GetProvider(GameId game) const noexcept {
        std::shared_lock lock(mutex_);
        auto it = providers_.find(game);
        return it != providers_.end() ? it->second.get() : nullptr;
    }
    
    [[nodiscard]] GameCryptoProvider* GetProvider(std::string_view gameName) const noexcept {
        std::shared_lock lock(mutex_);
        for (const auto& [_, provider] : providers_) {
            if (provider->GetGameName() == gameName) {
                return provider.get();
            }
        }
        return nullptr;
    }
    
    void ForEachProvider(std::function<void(const GameCryptoProvider*)> func) const noexcept {
        std::shared_lock lock(mutex_);
        for (const auto& [_, provider] : providers_) {
            func(provider.get());
        }
    }
    
    size_t Count() const noexcept {
        std::shared_lock lock(mutex_);
        return providers_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<GameId, std::unique_ptr<GameCryptoProvider>> providers_;
};

// ============================================================
// Game-Specific Implementations (Declarations)
// ============================================================

// FiveM Crypto Provider
class FiveMCryptoProvider final : public GameCryptoProvider {
public:
    [[nodiscard]] GameId GetGameId() const noexcept override { return GameId::FiveM; }
    [[nodiscard]] std::string_view GetGameName() const noexcept override { return "FiveM"; }
    
    void DecryptOffsets(std::span<uint8_t> data) const noexcept override;
    void EncryptOffsets(std::span<uint8_t> data) const noexcept override;
    bool VerifyIntegrity() const noexcept override;
    std::array<uint8_t, 32> DeriveKey(std::string_view context) const noexcept override;
    void InstallAntiTamper() const noexcept override;
};

// CS2 Crypto Provider
class CS2CryptoProvider final : public GameCryptoProvider {
public:
    [[nodiscard]] GameId GetGameId() const noexcept override { return GameId::CS2; }
    [[nodiscard]] std::string_view GetGameName() const noexcept override { return "CS2"; }
    
    void DecryptOffsets(std::span<uint8_t> data) const noexcept override;
    void EncryptOffsets(std::span<uint8_t> data) const noexcept override;
    bool VerifyIntegrity() const noexcept override;
    std::array<uint8_t, 32> DeriveKey(std::string_view context) const noexcept override;
    void InstallAntiTamper() const noexcept override;
};

// Warzone Crypto Provider
class WarzoneCryptoProvider final : public GameCryptoProvider {
public:
    [[nodiscard]] GameId GetGameId() const noexcept override { return GameId::Warzone; }
    [[nodiscard]] std::string_view GetGameName() const noexcept override { return "Warzone"; }
    
    void DecryptOffsets(std::span<uint8_t> data) const noexcept override;
    void EncryptOffsets(std::span<uint8_t> data) const noexcept override;
    bool VerifyIntegrity() const noexcept override;
    std::array<uint8_t, 32> DeriveKey(std::string_view context) const noexcept override;
    void InstallAntiTamper() const noexcept override;
};

// Fortnite Crypto Provider
class FortniteCryptoProvider final : public GameCryptoProvider {
public:
    [[nodiscard]] GameId GetGameId() const noexcept override { return GameId::Fortnite; }
    [[nodiscard]] std::string_view GetGameName() const noexcept override { return "Fortnite"; }
    
    void DecryptOffsets(std::span<uint8_t> data) const noexcept override;
    void EncryptOffsets(std::span<uint8_t> data) const noexcept override;
    bool VerifyIntegrity() const noexcept override;
    std::array<uint8_t, 32> DeriveKey(std::string_view context) const noexcept override;
    void InstallAntiTamper() const noexcept override;
};

// Apex Crypto Provider
class ApexCryptoProvider final : public GameCryptoProvider {
public:
    [[nodiscard]] GameId GetGameId() const noexcept override { return GameId::Apex; }
    [[nodiscard]] std::string_view GetGameName() const noexcept override { return "Apex"; }
    
    void DecryptOffsets(std::span<uint8_t> data) const noexcept override;
    void EncryptOffsets(std::span<uint8_t> data) const noexcept override;
    bool VerifyIntegrity() const noexcept override;
    std::array<uint8_t, 32> DeriveKey(std::string_view context) const noexcept override;
    void InstallAntiTamper() const noexcept override;
};

// ============================================================
// Initialization
// ============================================================

inline void InitializeGameCrypto() noexcept {
    auto& registry = CryptoRegistry::Instance();
    registry.RegisterProvider(std::make_unique<FiveMCryptoProvider>());
    registry.RegisterProvider(std::make_unique<CS2CryptoProvider>());
    registry.RegisterProvider(std::make_unique<WarzoneCryptoProvider>());
    registry.RegisterProvider(std::make_unique<FortniteCryptoProvider>());
    registry.RegisterProvider(std::make_unique<ApexCryptoProvider>());
    
    // Install anti-tamper for all games
    registry.ForEachProvider([](const GameCryptoProvider* provider) {
        provider->InstallAntiTamper();
    });
}

inline GameCryptoProvider* GetGameCryptoProvider(GameId game) noexcept {
    return CryptoRegistry::Instance().GetProvider(game);
}

inline GameCryptoProvider* GetGameCryptoProvider(std::string_view gameName) noexcept {
    return CryptoRegistry::Instance().GetProvider(gameName);
}

} // namespace OmniGhost::Security
