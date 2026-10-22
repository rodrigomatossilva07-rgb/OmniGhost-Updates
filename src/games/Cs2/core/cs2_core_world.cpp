#include "cs2_core_world.h"

#include <cmath>
#include <cstring>

namespace CS2::CoreWorld {

bool IsPlayableTeam(int team) noexcept {
    return team == 2 || team == 3;
}

bool IsFinitePosition(const float* position) noexcept {
    return position && std::isfinite(position[0]) &&
        std::isfinite(position[1]) && std::isfinite(position[2]);
}

bool InsideExpandedFrustum(const float* position,
                           const float* view_matrix) noexcept {
    if (!IsFinitePosition(position) || !view_matrix)
        return false;

    const float x = position[0];
    const float y = position[1];
    const float z = position[2] + 38.f;
    const float clip_x = x * view_matrix[0] + y * view_matrix[1] +
        z * view_matrix[2] + view_matrix[3];
    const float clip_y = x * view_matrix[4] + y * view_matrix[5] +
        z * view_matrix[6] + view_matrix[7];
    const float clip_w = x * view_matrix[12] + y * view_matrix[13] +
        z * view_matrix[14] + view_matrix[15];
    if (!std::isfinite(clip_w) || clip_w <= 0.01f)
        return false;

    const float normalized_x = clip_x / clip_w;
    const float normalized_y = clip_y / clip_w;
    return std::isfinite(normalized_x) && std::isfinite(normalized_y) &&
        normalized_x >= -1.35f && normalized_x <= 1.35f &&
        normalized_y >= -1.45f && normalized_y <= 1.45f;
}

bool IsPlayableMapName(const char* map_name) noexcept {
    if (!map_name || !*map_name)
        return false;
    // CS-007: comparação sem construir std::string (o hot loop chamava isto
    // a cada scan e alocava por frame).
    return std::strcmp(map_name, "lobby") != 0 &&
           std::strcmp(map_name, "none") != 0 &&
           std::strcmp(map_name, "mainmenu") != 0;
}

}
