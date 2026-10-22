#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {




enum class ServiceLifetime : std::uint8_t {
    Singleton,
    Scoped,
    Transient
};




class ServiceContainer;

struct ServiceDescriptor {
    ServiceLifetime lifetime = ServiceLifetime::Singleton;
    std::function<std::shared_ptr<void>(ServiceContainer&)> factory;
    std::shared_ptr<void> instance;

    ServiceDescriptor() = default;
    ServiceDescriptor(ServiceLifetime lt, std::function<std::shared_ptr<void>(ServiceContainer&)> f)
        : lifetime(lt), factory(std::move(f)) {}
    ServiceDescriptor(ServiceLifetime lt, std::shared_ptr<void> inst)
        : lifetime(lt), instance(std::move(inst)) {}
};




class ServiceContainer : public std::enable_shared_from_this<ServiceContainer> {
public:
    ServiceContainer() = default;
    ~ServiceContainer() = default;
    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;

    ServiceContainer(ServiceContainer&& other) noexcept {
        std::unique_lock lock(other.mutex_);
        descriptors_ = std::move(other.descriptors_);
        singletons_ = std::move(other.singletons_);
        parent_ = std::move(other.parent_);
    }

    ServiceContainer& operator=(ServiceContainer&& other) noexcept {
        if (this != &other) {
            std::unique_lock lock1(mutex_, std::defer_lock);
            std::unique_lock lock2(other.mutex_, std::defer_lock);
            std::lock(lock1, lock2);
            descriptors_ = std::move(other.descriptors_);
            singletons_ = std::move(other.singletons_);
            parent_ = std::move(other.parent_);
        }
        return *this;
    }


    [[nodiscard]] std::unique_ptr<ServiceContainer> CreateScope() const;


    [[nodiscard]] ServiceContainer* GetParent() const noexcept {
        // PL-012: o parent é weak — se o dono partiu, devolve nullptr
        // em vez de dangling.
        std::shared_lock lock(mutex_);
        if (auto locked = parent_.lock())
            return locked.get();
        return nullptr;
    }




    template <typename Interface, typename Implementation, typename... Args>
    void RegisterSingleton(Args&&... args) {
        static_assert(std::is_base_of_v<Interface, Implementation>,
            "Implementation must derive from Interface");
        const auto type = std::type_index(typeid(Interface));

        auto factory = [args = std::make_tuple(std::forward<Args>(args)...)](
            ServiceContainer& container) mutable -> std::shared_ptr<void> {
            return std::apply([&container](auto&&... a) {
                return std::make_shared<Implementation>(std::forward<decltype(a)>(a)...);
            }, std::move(args));
        };

        std::unique_lock lock(mutex_);
        descriptors_[type] = ServiceDescriptor(ServiceLifetime::Singleton, std::move(factory));
        singletons_.erase(type);
    }


    template <typename Interface, typename Implementation, typename... Args>
    void RegisterScoped(Args&&... args) {
        static_assert(std::is_base_of_v<Interface, Implementation>,
            "Implementation must derive from Interface");
        const auto type = std::type_index(typeid(Interface));

        auto factory = [args = std::make_tuple(std::forward<Args>(args)...)](
            ServiceContainer& container) mutable -> std::shared_ptr<void> {
            return std::apply([&container](auto&&... a) {
                return std::make_shared<Implementation>(std::forward<decltype(a)>(a)...);
            }, std::move(args));
        };

        std::unique_lock lock(mutex_);
        descriptors_[type] = ServiceDescriptor(ServiceLifetime::Scoped, std::move(factory));
        singletons_.erase(type);
    }


    template <typename Interface, typename Implementation, typename... Args>
    void RegisterTransient(Args&&... args) {
        static_assert(std::is_base_of_v<Interface, Implementation>,
            "Implementation must derive from Interface");
        const auto type = std::type_index(typeid(Interface));

        auto factory = [args = std::make_tuple(std::forward<Args>(args)...)](
            ServiceContainer& container) mutable -> std::shared_ptr<void> {
            return std::apply([&container](auto&&... a) {
                return std::make_shared<Implementation>(std::forward<decltype(a)>(a)...);
            }, std::move(args));
        };

        std::unique_lock lock(mutex_);
        descriptors_[type] = ServiceDescriptor(ServiceLifetime::Transient, std::move(factory));
        singletons_.erase(type);
    }


    template <typename Interface>
    void RegisterFactory(ServiceLifetime lifetime, std::function<std::shared_ptr<Interface>(ServiceContainer&)> factory) {
        const auto type = std::type_index(typeid(Interface));
        std::unique_lock lock(mutex_);
        descriptors_[type] = ServiceDescriptor(lifetime,
            [factory = std::move(factory)](ServiceContainer& c) -> std::shared_ptr<void> {
                return factory(c);
            });
        singletons_.erase(type);
    }


    template <typename Interface>
    void RegisterInstance(std::shared_ptr<Interface> instance) {
        const auto type = std::type_index(typeid(Interface));
        std::unique_lock lock(mutex_);
        descriptors_[type] = ServiceDescriptor(ServiceLifetime::Singleton, instance);
        singletons_[type] = std::static_pointer_cast<void>(std::move(instance));
    }


    template <typename Interface, typename Implementation>
    void RegisterInstance(Implementation&& instance) {
        RegisterInstance<Interface>(std::make_shared<Implementation>(std::forward<Implementation>(instance)));
    }




    template <typename Interface>
    [[nodiscard]] std::shared_ptr<Interface> Get() const {
        auto ptr = TryGet<Interface>();
        if (!ptr) {
            throw std::runtime_error("Service not registered: " + std::string(typeid(Interface).name()));
        }
        return ptr;
    }


    template <typename Interface>
    [[nodiscard]] std::shared_ptr<Interface> TryGet() const noexcept {
        const auto type = std::type_index(typeid(Interface));


        {
            std::shared_lock lock(mutex_);
            auto it = singletons_.find(type);
            if (it != singletons_.end()) {
                return std::static_pointer_cast<Interface>(it->second);
            }
        }


        ServiceLifetime lifetime;
        std::function<std::shared_ptr<void>(ServiceContainer&)> factory;
        std::shared_ptr<void> existingInstance;
        std::weak_ptr<ServiceContainer> parentSnapshot;
        {
            std::shared_lock lock(mutex_);
            auto it = descriptors_.find(type);
            if (it == descriptors_.end()) {

                parentSnapshot = parent_;
            } else {
                lifetime = it->second.lifetime;
                factory = it->second.factory;
                existingInstance = it->second.instance;
                parentSnapshot = parent_;
            }
        }
        if (it == descriptors_.end() && parentSnapshot.expired())
            return nullptr;
        if (it == descriptors_.end()) {
            if (auto parent = parentSnapshot.lock())
                return parent->TryGet<Interface>();
            return nullptr;
        }

        if (lifetime == ServiceLifetime::Singleton) {
            if (existingInstance) {
                return std::static_pointer_cast<Interface>(existingInstance);
            }
            if (factory) {
                // PL-012: serializa a criação lazy de singletons — sem isto,
                // duas threads corriam a factory e obtinham instâncias
                // distintas de um "singleton". O resolveMutex_ é separado
                // do mutex_ para a factory poder resolver dependências do
                // próprio container sem deadlock.
                std::lock_guard resolveLock(resolveMutex_);
                {
                    std::shared_lock lock(mutex_);
                    auto it2 = singletons_.find(type);
                    if (it2 != singletons_.end()) {
                        return std::static_pointer_cast<Interface>(it2->second);
                    }
                }
                auto instance = factory(*const_cast<ServiceContainer*>(this));
                std::unique_lock lock(mutex_);
                singletons_[type] = instance;
                return std::static_pointer_cast<Interface>(instance);
            }
        } else if (lifetime == ServiceLifetime::Scoped || lifetime == ServiceLifetime::Transient) {
            if (factory) {
                return std::static_pointer_cast<Interface>(factory(*const_cast<ServiceContainer*>(this)));
            }
        }


        if (auto parent = parentSnapshot.lock())
            return parent->TryGet<Interface>();
        return nullptr;
    }


    template <typename Interface, typename Implementation, typename... Args>
    [[nodiscard]] std::shared_ptr<Interface> GetOrCreate(Args&&... args) {
        auto ptr = TryGet<Interface>();
        if (ptr) return ptr;

        RegisterSingleton<Interface, Implementation>(std::forward<Args>(args)...);
        return Get<Interface>();
    }


    template <typename Interface>
    [[nodiscard]] bool Has() const noexcept {
        const auto type = std::type_index(typeid(Interface));
        std::shared_lock lock(mutex_);
        if (descriptors_.contains(type) || singletons_.contains(type)) return true;
        if (parent_) return parent_->Has<Interface>();
        return false;
    }


    template <typename Interface>
    void Unregister() noexcept {
        const auto type = std::type_index(typeid(Interface));
        std::unique_lock lock(mutex_);
        descriptors_.erase(type);
        singletons_.erase(type);
    }


    void Clear() noexcept {
        std::unique_lock lock(mutex_);
        descriptors_.clear();
        singletons_.clear();
    }


    [[nodiscard]] std::vector<std::string> ListServices() const noexcept {
        std::shared_lock lock(mutex_);
        std::vector<std::string> names;
        names.reserve(descriptors_.size() + singletons_.size());
        for (const auto& [key, _] : descriptors_) {
            names.push_back(key.name());
        }
        for (const auto& [key, _] : singletons_) {
            if (!descriptors_.contains(key)) names.push_back(key.name());
        }
        return names;
    }


    template <typename Interface>
    [[nodiscard]] std::optional<ServiceLifetime> GetLifetime() const noexcept {
        const auto type = std::type_index(typeid(Interface));
        std::shared_lock lock(mutex_);
        auto it = descriptors_.find(type);
        if (it != descriptors_.end()) return it->second.lifetime;
        if (parent_) return parent_->GetLifetime<Interface>();
        return std::nullopt;
    }


    [[nodiscard]] std::unique_ptr<ServiceContainer> Clone() const {
        auto clone = std::make_unique<ServiceContainer>();
        std::shared_lock lock(mutex_);
        clone->descriptors_ = descriptors_;
        clone->singletons_ = singletons_;
        clone->parent_ = parent_;
        return clone;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, ServiceDescriptor> descriptors_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> singletons_;
    ServiceContainer* parent_ = nullptr;
};




class ServiceBuilder {
public:
    explicit ServiceBuilder(ServiceContainer& container) : container_(container) {}

    template <typename Interface, typename Implementation, typename... Args>
    ServiceBuilder& AddSingleton(Args&&... args) {
        container_.RegisterSingleton<Interface, Implementation>(std::forward<Args>(args)...);
        return *this;
    }

    template <typename Interface, typename Implementation, typename... Args>
    ServiceBuilder& AddScoped(Args&&... args) {
        container_.RegisterScoped<Interface, Implementation>(std::forward<Args>(args)...);
        return *this;
    }

    template <typename Interface, typename Implementation, typename... Args>
    ServiceBuilder& AddTransient(Args&&... args) {
        container_.RegisterTransient<Interface, Implementation>(std::forward<Args>(args)...);
        return *this;
    }

    template <typename Interface>
    ServiceBuilder& AddFactory(ServiceLifetime lifetime, std::function<std::shared_ptr<Interface>(ServiceContainer&)> factory) {
        container_.RegisterFactory<Interface>(lifetime, std::move(factory));
        return *this;
    }

    template <typename Interface>
    ServiceBuilder& AddInstance(std::shared_ptr<Interface> instance) {
        container_.RegisterInstance(std::move(instance));
        return *this;
    }

    ServiceContainer& Build() { return container_; }

private:
    ServiceContainer& container_;
};




class Services {
public:
    static ServiceContainer& Instance() noexcept {
        static ServiceContainer container;
        return container;
    }

    static ServiceBuilder Builder() {
        return ServiceBuilder(Instance());
    }
};


[[nodiscard]] ServiceContainer CreateProductionContainer();
[[nodiscard]] ServiceContainer CreateTestContainer();

}