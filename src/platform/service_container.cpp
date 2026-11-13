#include "service_container.h"

namespace OmniGhost::Platform {

std::unique_ptr<ServiceContainer> ServiceContainer::CreateScope() const {
    auto child = std::make_unique<ServiceContainer>();
    {
        std::shared_lock lock(mutex_);




        if (auto lockedSelf = weak_from_this().lock()) {
            child->parent_ = std::const_pointer_cast<ServiceContainer>(lockedSelf);
        }
        for (const auto& [key, desc] : descriptors_) {
            if (desc.lifetime == ServiceLifetime::Singleton) {
                child->descriptors_[key] = desc;
            }
        }
    }
    return child;
}



ServiceContainer CreateProductionContainer() {
    return ServiceContainer{};
}

ServiceContainer CreateTestContainer() {
    return ServiceContainer{};
}

}
