#pragma once

#include "security_manager.h"
#include "platform/expected.h"
#include <memory>

namespace OmniGhost::Security {

// =============================================================================
// SecurityManagerFactory - Factory for creating SecurityManager instances
// =============================================================================

class SecurityManagerFactory final {
public:
    Expected<std::shared_ptr<ISecurityManager>> Create() {
        // SecurityManager is a process-wide singleton.  The container keeps a
        // non-owning shared reference so all call sites observe one state.
        return Success(std::shared_ptr<ISecurityManager>(
            &SecurityManager::Instance(), [](ISecurityManager*) {}));
    }
};

} // namespace OmniGhost::Security
