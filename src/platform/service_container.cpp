#include "service_container.h"

namespace OmniGhost::Platform {

std::unique_ptr<ServiceContainer> ServiceContainer::CreateScope() const {
    auto child = std::make_unique<ServiceContainer>();
    {
        std::shared_lock lock(mutex_);
        child->parent_ = const_cast<ServiceContainer*>(this);
        for (const auto& [key, desc] : descriptors_) {
            if (desc.lifetime == ServiceLifetime::Singleton) {
                child->descriptors_[key] = desc;
            }
        }
    }
    return child;
}

// Lightweight factories: do not pull interfaces.h here (avoids MSVC C1202).
// Callers can RegisterInstance/RegisterSingleton for IClock/IFilesystem as needed.
ServiceContainer CreateProductionContainer() {
    return ServiceContainer{};
}

ServiceContainer CreateTestContainer() {
    return ServiceContainer{};
}

} // namespace OmniGhost::Platform
