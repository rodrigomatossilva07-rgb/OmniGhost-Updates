#pragma once
#include "cs2_collision_mesh.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

namespace CS2::Visibility {
using Trajectory::Vec3;

inline bool Finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}



inline std::optional<Vec3> CameraOrigin(const float* matrix) {
    if (!matrix) return std::nullopt;
    double a[3][4]{};
    constexpr int rows[] = {0, 1, 3};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float value = matrix[rows[r] * 4 + c];
            if (!std::isfinite(value)) return std::nullopt;
            a[r][c] = c == 3 ? -value : value;
        }
    }
    for (int c = 0; c < 3; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 3; ++r)
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
        if (std::abs(a[pivot][c]) < 1e-9) return std::nullopt;
        for (int k = 0; k < 4; ++k) std::swap(a[c][k], a[pivot][k]);
        const double scale = a[c][c];
        for (int k = c; k < 4; ++k) a[c][k] /= scale;
        for (int r = 0; r < 3; ++r) {
            if (r == c) continue;
            const double factor = a[r][c];
            for (int k = c; k < 4; ++k) a[r][k] -= factor * a[c][k];
        }
    }
    Vec3 origin{static_cast<float>(a[0][3]), static_cast<float>(a[1][3]), static_cast<float>(a[2][3])};
    return Finite(origin) ? std::optional<Vec3>(origin) : std::nullopt;
}



inline std::optional<bool> CheckPoints(const Trajectory::CollisionBvh* world,
                                       Vec3 origin, std::span<const Vec3> points,
                                       int* raysTested = nullptr,
                                       const std::atomic_bool* cancel = nullptr) {
    if (raysTested) *raysTested = 0;
    if (!world || !world->Ready() || !Finite(origin) || points.empty()) return std::nullopt;
    bool tested = false;
    for (const auto point : points) {
        if (!Finite(point)) continue;
        const float dx = point.x - origin.x, dy = point.y - origin.y, dz = point.z - origin.z;
        if (dx * dx + dy * dy + dz * dz < .0001f) continue;
        tested = true;
        if (raysTested) ++*raysTested;
        if (!world->TraceRay(origin, point, cancel).hit) return true;
    }
    return tested ? std::optional<bool>(false) : std::nullopt;
}

}