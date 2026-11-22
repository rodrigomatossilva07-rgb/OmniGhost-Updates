#pragma once

#include "ioffset_manager.h"
#include "platform/expected.h"
#include <memory>

namespace OmniGhost::Platform {

// =============================================================================
// OffsetManagerFactory - Factory for creating OffsetManager instances
// =============================================================================

class OffsetManagerFactory final {
public:
    Expected<std::unique_ptr<IOffsetManager>> Create();
};

} // namespace OmniGhost::Platform
