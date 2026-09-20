#pragma once
#include "cs2_collision_mesh.h"
#include <vector>
namespace CS2::Trajectory {
struct TrajectoryResult { std::vector<Vec3> points; Vec3 impact{}; float elapsed{}; bool hit{}; };
// Physics defaults are deliberately configurable after in-game calibration.
TrajectoryResult SimulateGrenade(const CollisionBvh& world, Vec3 origin, Vec3 velocity, float detonateSeconds, float gravity = 324.f);
// Graceful fallback for maps without an embedded collision mesh. It provides a
// visible ballistic estimate but intentionally makes no wall-collision claim.
TrajectoryResult SimulateBallistic(Vec3 origin, Vec3 velocity, float detonateSeconds, float gravity = 324.f);
}
