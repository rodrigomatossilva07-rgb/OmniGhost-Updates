#pragma once

#include "feature_flags.h"
#include "service_container.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Game Feature - Optional feature module
// ============================================================
class IGameFeature {
public:
    virtual ~IGameFeature() = default;

    // Feature metadata
    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetId() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetDescription() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetCategory() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetGameId() const noexcept = 0;  // Empty = universal
    [[nodiscard]] virtual bool IsExperimental() const noexcept = 0;
    [[nodiscard]] virtual bool IsEnabledByDefault() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::string_view> GetDependencies() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetFeatureFlag() const noexcept = 0;  // Feature flag that controls this

    // Lifecycle
    virtual Result<void> Initialize(const ServiceContainer& services) noexcept = 0;
    virtual void Shutdown() noexcept = 0;
    virtual void Tick() noexcept = 0;  // Called per frame when feature is active

    // Configuration
    virtual void OnConfigChanged(std::string_view key, std::string_view value) noexcept = 0;
    virtual std::vector<std::pair<std::string, std::string>> GetConfigSchema() const noexcept = 0;

    // State queries
    [[nodiscard]] virtual bool IsActive() const noexcept = 0;
    [[nodiscard]] virtual std::string GetStatus() const noexcept = 0;
};

// ============================================================
// Feature Registry - Manages optional game features
// ============================================================
class GameFeatureRegistry final {
public:
    struct FeatureInfo {
        std::string id;
        std::string name;
        std::string description;
        std::string category;
        std::string gameId;  // Empty = universal
        bool isExperimental = false;
        bool isEnabledByDefault = false;
        std::vector<std::string> dependencies;
        std::string featureFlag;
        std::function<std::unique_ptr<IGameFeature>(const ServiceContainer&)> factory;
    };

    GameFeatureRegistry() = default;
    ~GameFeatureRegistry() = default;

    // Register a feature
    void Register(const FeatureInfo& info) {
        std::unique_lock lock(mutex_);
        features_[info.id] = info;
        
        // Register feature flag if not exists
        if (!info.featureFlag.empty()) {
            FeatureFlags::Flag flag;
            flag.name = info.featureFlag;
            flag.description = info.description;
            flag.defaultValue = info.isEnabledByDefault;
            flag.category = info.category;
            flag.isExperimental = info.isExperimental;
            flag.dependencies = info.dependencies;
            GetFeatureFlags().Register(flag);
        }
    }

    // Register feature with simple factory
    template <typename Feature, typename... Args>
    void Register(Args&&... args) {
        FeatureInfo info;
        info.id = Feature::StaticGetId();
        info.name = Feature::StaticGetName();
        info.description = Feature::StaticGetDescription();
        info.category = Feature::StaticGetCategory();
        info.gameId = Feature::StaticGetGameId();
        info.isExperimental = Feature::StaticIsExperimental();
        info.isEnabledByDefault = Feature::StaticIsEnabledByDefault();
        info.dependencies.clear();
        for (const auto dep : Feature::StaticGetDependencies()) {
            info.dependencies.emplace_back(dep);
        }
        info.featureFlag = Feature::StaticGetFeatureFlag();
        
        info.factory = [args = std::make_tuple(std::forward<Args>(args)...)](const ServiceContainer& services) mutable {
            return std::apply([&services](auto&&... a) {
                return std::make_unique<Feature>(services, std::forward<decltype(a)>(a)...);
            }, std::move(args));
        };
        
        Register(info);
    }

    // Create feature instance
    [[nodiscard]] std::unique_ptr<IGameFeature> Create(const std::string& featureId, const ServiceContainer& services) {
        std::shared_lock lock(mutex_);
        auto it = features_.find(featureId);
        if (it == features_.end()) return nullptr;
        
        // Check feature flag
        if (!it->second.featureFlag.empty() && !GetFeatureFlags().IsEnabled(it->second.featureFlag)) {
            return nullptr;  // Feature disabled by flag
        }
        
        // Check dependencies
        for (const auto& dep : it->second.dependencies) {
            if (!GetFeatureFlags().IsEnabled(dep)) {
                return nullptr;  // Dependency not satisfied
            }
        }
        
        lock.unlock();
        try {
            return it->second.factory(services);
        } catch (...) {
            return nullptr;
        }
    }

    // Create all features for a game (or all universal if gameId empty)
    [[nodiscard]] std::vector<std::unique_ptr<IGameFeature>> CreateForGame(
        const ServiceContainer& services, std::string_view gameId = {}) {
        std::shared_lock lock(mutex_);
        std::vector<std::unique_ptr<IGameFeature>> result;
        
        for (const auto& [id, info] : features_) {
            if (!gameId.empty() && info.gameId != gameId && !info.gameId.empty()) continue;
            
            // Check feature flag
            if (!info.featureFlag.empty() && !GetFeatureFlags().IsEnabled(info.featureFlag)) continue;
            
            // Check dependencies
            bool depsOk = true;
            for (const auto& dep : info.dependencies) {
                if (!GetFeatureFlags().IsEnabled(dep)) {
                    depsOk = false;
                    break;
                }
            }
            if (!depsOk) continue;
            
            try {
                auto feature = info.factory(services);
                if (feature) result.push_back(std::move(feature));
            } catch (...) {
                // Skip failed features
            }
        }
        
        return result;
    }

    // Get feature info
    [[nodiscard]] std::optional<FeatureInfo> GetInfo(const std::string& featureId) const {
        std::shared_lock lock(mutex_);
        auto it = features_.find(featureId);
        if (it == features_.end()) return std::nullopt;
        return it->second;
    }

    // Get all features
    [[nodiscard]] std::vector<FeatureInfo> GetAllFeatures() const {
        std::shared_lock lock(mutex_);
        std::vector<FeatureInfo> result;
        result.reserve(features_.size());
        for (const auto& [_, info] : features_) {
            result.push_back(info);
        }
        return result;
    }

    // Get features by category
    [[nodiscard]] std::vector<FeatureInfo> GetByCategory(std::string_view category) const {
        std::shared_lock lock(mutex_);
        std::vector<FeatureInfo> result;
        for (const auto& [_, info] : features_) {
            if (info.category == category) result.push_back(info);
        }
        return result;
    }

    // Get features for a specific game
    [[nodiscard]] std::vector<FeatureInfo> GetForGame(std::string_view gameId) const {
        std::shared_lock lock(mutex_);
        std::vector<FeatureInfo> result;
        for (const auto& [_, info] : features_) {
            if (info.gameId == gameId || info.gameId.empty()) {
                result.push_back(info);
            }
        }
        return result;
    }

    // Get experimental features
    [[nodiscard]] std::vector<FeatureInfo> GetExperimental() const {
        std::shared_lock lock(mutex_);
        std::vector<FeatureInfo> result;
        for (const auto& [_, info] : features_) {
            if (info.isExperimental) result.push_back(info);
        }
        return result;
    }

    // Enable/disable feature via flag
    bool SetFeatureEnabled(const std::string& featureId, bool enabled) {
        std::shared_lock lock(mutex_);
        auto it = features_.find(featureId);
        if (it == features_.end()) return false;
        
        lock.unlock();
        return GetFeatureFlags().Set(it->second.featureFlag, enabled);
    }

    // Check if feature is enabled
    [[nodiscard]] bool IsFeatureEnabled(const std::string& featureId) const {
        std::shared_lock lock(mutex_);
        auto it = features_.find(featureId);
        if (it == features_.end()) return false;
        if (it->second.featureFlag.empty()) return true;
        return GetFeatureFlags().IsEnabled(it->second.featureFlag);
    }

    // Clear all features
    void Clear() noexcept {
        std::unique_lock lock(mutex_);
        features_.clear();
    }

    // Get count
    [[nodiscard]] std::size_t Count() const noexcept {
        std::shared_lock lock(mutex_);
        return features_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, FeatureInfo> features_;
};

// ============================================================
// Feature Base Class - For easy implementation
// ============================================================
template <typename Derived>
class GameFeatureBase : public IGameFeature {
public:
    // Static metadata (must be specialized by derived class)
    static constexpr std::string_view StaticGetId() { return Derived::Id; }
    static constexpr std::string_view StaticGetName() { return Derived::Name; }
    static constexpr std::string_view StaticGetDescription() { return Derived::Description; }
    static constexpr std::string_view StaticGetCategory() { return Derived::Category; }
    static constexpr std::string_view StaticGetGameId() { return Derived::GameId; }
    static constexpr bool StaticIsExperimental() { return Derived::IsExperimental; }
    static constexpr bool StaticIsEnabledByDefault() { return Derived::IsEnabledByDefault; }
    static std::vector<std::string_view> StaticGetDependencies() { return Derived::Dependencies(); }
    static constexpr std::string_view StaticGetFeatureFlag() { return Derived::FeatureFlag; }

    explicit GameFeatureBase(const ServiceContainer& services) : services_(&services) {}
    ~GameFeatureBase() override = default;

    [[nodiscard]] std::string_view GetName() const noexcept override { return Derived::Name; }
    [[nodiscard]] std::string_view GetId() const noexcept override { return Derived::Id; }
    [[nodiscard]] std::string_view GetDescription() const noexcept override { return Derived::Description; }
    [[nodiscard]] std::string_view GetCategory() const noexcept override { return Derived::Category; }
    [[nodiscard]] std::string_view GetGameId() const noexcept override { return Derived::GameId; }
    [[nodiscard]] bool IsExperimental() const noexcept override { return Derived::IsExperimental; }
    [[nodiscard]] bool IsEnabledByDefault() const noexcept override { return Derived::IsEnabledByDefault; }
    [[nodiscard]] std::vector<std::string_view> GetDependencies() const noexcept override { return StaticGetDependencies(); }
    [[nodiscard]] std::string_view GetFeatureFlag() const noexcept override { return Derived::FeatureFlag; }

    virtual Result<void> Initialize(const ServiceContainer& services) noexcept override {
        services_ = &services;
        return OnInitialize();
    }

    virtual void Shutdown() noexcept override {
        OnShutdown();
    }

    virtual void Tick() noexcept override {
        if (active_) OnTick();
    }

    virtual void OnConfigChanged(std::string_view key, std::string_view value) noexcept override {
        OnConfigChange(key, value);
    }

    virtual std::vector<std::pair<std::string, std::string>> GetConfigSchema() const noexcept override {
        return {};
    }

    [[nodiscard]] bool IsActive() const noexcept override { return active_; }
    [[nodiscard]] std::string GetStatus() const noexcept override { return OnGetStatus(); }

protected:
    const ServiceContainer& GetServices() const noexcept { return *services_; }
    ServiceContainer& GetServices() noexcept { return *const_cast<ServiceContainer*>(services_); }
    
    void SetActive(bool active) noexcept { active_ = active; }

private:
    virtual Result<void> OnInitialize() noexcept { return Ok(); }
    virtual void OnShutdown() noexcept {}
    virtual void OnTick() noexcept {}
    virtual void OnConfigChange(std::string_view, std::string_view) noexcept {}
    virtual std::string OnGetStatus() const noexcept { return active_ ? "Active" : "Inactive"; }

    const ServiceContainer* services_ = nullptr;
    bool active_ = true;
};

// ============================================================
// Global registry accessor
// ============================================================
inline GameFeatureRegistry& GetFeatureRegistry() noexcept {
    static GameFeatureRegistry instance;
    return instance;
}

} // namespace OmniGhost::Platform