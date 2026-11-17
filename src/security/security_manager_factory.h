#pragma once

#include "isecurity_manager.h"
#include "platform/expected.h"
#include <memory>

namespace OmniGhost::Security {

// =============================================================================
// SecurityManagerFactory - Factory for creating SecurityManager instances
// =============================================================================

class SecurityManagerFactory final {
public:
    Expected<std::unique_ptr<ISecurityManager>> Create() {
        return Success(std::make_unique<SecurityManager>());
    }
};

} // namespace OmniGhost::Security