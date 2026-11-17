#pragma once

#include "ihardware_manager.h"
#include "platform/expected.h"
#include <memory>

namespace OmniGhost::Platform {

// =============================================================================
// HardwareManagerFactory - Factory for creating HardwareManager instances
// =============================================================================

class HardwareManagerFactory final {
public:
    Expected<std::unique_ptr<IHardwareManager>> Create() {
        return Success(std::make_unique<Hardware::Manager>());
    }
};

} // namespace OmniGhost::Platform