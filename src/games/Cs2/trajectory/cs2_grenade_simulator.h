#pragma once
#include "cs2_collision_mesh.h"
#include <vector>
namespace CS2::Trajectory {
struct GrenadePhysics { float detonate_seconds=1.5f, gravity=324.f, restitution=.45f, surface_friction=.72f, max_segment_length=20.f; unsigned max_bounces=12; };
struct TrajectoryResult { std::vector<Vec3> points, bounces; Vec3 impact{}; float elapsed{}; bool hit{}, settled{}; };
TrajectoryResult SimulateGrenade(const CollisionBvh& world, Vec3 origin, Vec3 velocity, const GrenadePhysics& physics);
// Graceful fallback for maps without an embedded collision mesh. It provides a
// visible ballistic estimate but intentionally makes no wall-collision claim.
TrajectoryResult SimulateBallistic(Vec3 origin, Vec3 velocity, const GrenadePhysics& physics);
}
