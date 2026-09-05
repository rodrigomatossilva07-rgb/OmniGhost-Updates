#pragma once

#include <cstdint>
#include "offsets.h"

namespace FiveM {
namespace OffsetAuto {

inline bool PointersLookValid() {
    using namespace offset;
    auto looks = [](uintptr_t v) -> bool {
#if defined(_WIN64)
        return v >= 0x10000ull && v <= 0x00007FFFFFFFFFFFull;
#else
        return v >= 0x10000u && v <= 0x7FFF0000u;
#endif
    };
    if (!looks(world) || !looks(viewport))
        return false;
    if (localplayer && !looks(localplayer))
        return false;
    return true;
}

} // namespace OffsetAuto
} // namespace FiveM
