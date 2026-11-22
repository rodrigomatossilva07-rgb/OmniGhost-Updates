#pragma once

#include "iconfig_manager.h"
#include "platform/expected.h"
#include <memory>

namespace OmniGhost::Config {

// =============================================================================
// ConfigManagerFactory - Factory for creating ConfigManagerImpl instances
// =============================================================================

class ConfigManagerFactory final {
public:
    Expected<std::unique_ptr<IConfigManager>> Create();
};

} // namespace OmniGhost::Config
