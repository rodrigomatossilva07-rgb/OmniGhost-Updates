#pragma once

#include "iconfig_manager.h"
#include "platform/expected.h"
#include <memory>

namespace OmniGhost::Config {





class ConfigManagerFactory final {
public:
    Expected<std::unique_ptr<IConfigManager>> Create();
};

}
