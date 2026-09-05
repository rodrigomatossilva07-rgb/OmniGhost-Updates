#pragma once

#include "interfaces.h"
#include <memory>
#include <unordered_map>
#include <typeindex>
#include <mutex>

namespace OmniGhost::Platform {

// Service locator with explicit ownership - replaces global singletons
class ServiceContainer final {
public:
    ServiceContainer() = default;
    ~ServiceContainer() = default;
    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;
    
    ServiceContainer(ServiceContainer&& other) noexcept 
        : services_(std::move(other.services_)) {}
    ServiceContainer& operator=(ServiceContainer&& other) noexcept {
        if (this != &other) {
            std::lock_guard lock(mutex_);
            services_ = std::move(other.services_);
        }
        return *this;
    }

    // Register a service implementation
    template <typename Interface, typename Implementation, typename... Args>
    void Register(Args&&... args) {
        static_assert(std::is_base_of_v<Interface, Implementation>,
            "Implementation must derive from Interface");
        std::lock_guard lock(mutex_);
        services_[std::type_index(typeid(Interface))] =
            std::make_shared<Implementation>(std::forward<Args>(args)...);
    }

    // Register a pre-created instance
    template <typename Interface>
    void RegisterInstance(std::shared_ptr<Interface> instance) {
        std::lock_guard lock(mutex_);
        services_[std::type_index(typeid(Interface))] = std::move(instance);
    }

    // Get a service (throws if not registered)
    template <typename Interface>
    [[nodiscard]] std::shared_ptr<Interface> Get() const {
        std::lock_guard lock(mutex_);
        auto it = services_.find(std::type_index(typeid(Interface)));
        if (it == services_.end()) {
            throw std::runtime_error("Service not registered: " +
                std::string(typeid(Interface).name()));
        }
        return std::static_pointer_cast<Interface>(it->second);
    }

    // Try to get a service (returns null if not registered)
    template <typename Interface>
    [[nodiscard]] std::shared_ptr<Interface> TryGet() const noexcept {
        std::lock_guard lock(mutex_);
        auto it = services_.find(std::type_index(typeid(Interface)));
        if (it == services_.end()) return nullptr;
        return std::static_pointer_cast<Interface>(it->second);
    }

    // Check if a service is registered
    template <typename Interface>
    [[nodiscard]] bool Has() const noexcept {
        std::lock_guard lock(mutex_);
        return services_.contains(std::type_index(typeid(Interface)));
    }

    // Remove a service
    template <typename Interface>
    void Unregister() noexcept {
        std::lock_guard lock(mutex_);
        services_.erase(std::type_index(typeid(Interface)));
    }

    // Clear all services
    void Clear() noexcept {
        std::lock_guard lock(mutex_);
        services_.clear();
    }

    // Get all registered service types (for debugging)
    [[nodiscard]] std::vector<std::string> ListServices() const noexcept {
        std::lock_guard lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [key, _] : services_) {
            names.push_back(key.name());
        }
        return names;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
};

// Global accessor for backward compatibility during migration
// TODO: Remove after full DI migration
class Services {
public:
    static ServiceContainer& Instance() noexcept {
        static ServiceContainer container;
        return container;
    }
};

// Helper to create default production services
inline ServiceContainer CreateProductionContainer() {
    ServiceContainer container;
    container.RegisterInstance<IClock>(std::make_shared<SteadyClock>());
    container.RegisterInstance<IFilesystem>(std::make_shared<StdFilesystem>());
    // INetwork, IProcess, IHardware will be registered by their respective modules
    return container;
}

// Helper to create test container with mocks
inline ServiceContainer CreateTestContainer() {
    ServiceContainer container;
    container.RegisterInstance<IClock>(std::make_shared<SteadyClock>());
    container.RegisterInstance<IFilesystem>(std::make_shared<StdFilesystem>());
    return container;
}

} // namespace OmniGhost::Platform