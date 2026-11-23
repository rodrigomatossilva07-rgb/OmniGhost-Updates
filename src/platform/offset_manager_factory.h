#pragma once

#include "ioffset_manager.h"
#include "platform/expected.h"
#include <memory>

namespace OmniGhost::Platform {





class OffsetManagerFactory final {
public:
    Expected<std::unique_ptr<IOffsetManager>> Create();
};

}
