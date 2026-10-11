#pragma once

namespace CS2::CoreWorld {

[[nodiscard]] bool IsPlayableTeam(int team) noexcept;
[[nodiscard]] bool IsFinitePosition(const float* position) noexcept;
[[nodiscard]] bool InsideExpandedFrustum(const float* position,
                                         const float* view_matrix) noexcept;
[[nodiscard]] bool IsPlayableMapName(const char* map_name);

} // namespace CS2::CoreWorld
