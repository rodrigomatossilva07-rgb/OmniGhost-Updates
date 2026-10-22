#pragma once

#include "../trajectory/cs2_collision_mesh.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace CS2::VisibilityPipeline {

struct Target {
    uintptr_t pawn = 0;
    std::array<Trajectory::Vec3, 4> points{};
};

struct Result {
    uint64_t generation = 0;
    uint64_t checked_ms = 0;
    bool known = false;
    bool visible = false;
    uint8_t point_known_mask = 0;
    uint8_t point_visible_mask = 0;
};



class Worker final {
public:
    Worker() = default;
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void Submit(std::shared_ptr<const Trajectory::CollisionBvh> world,
                Trajectory::Vec3 origin, std::vector<Target> targets,
                uint64_t generation);
    [[nodiscard]] Result Get(uintptr_t pawn, uint64_t generation) const;
    void Reset();
    void Stop();

private:
    struct Job {
        std::shared_ptr<const Trajectory::CollisionBvh> world;
        Trajectory::Vec3 origin{};
        std::vector<Target> targets;
        uint64_t generation = 0;
        uint64_t epoch = 0;
    };

    void Run();

    mutable std::shared_mutex mutex_;
    std::condition_variable_any cv_;
    std::thread thread_;
    std::atomic_bool stop_{false};
    std::atomic<uint64_t> epoch_{1};
    bool pending_ = false;
    Job job_{};
    std::unordered_map<uintptr_t, Result> results_;
};

}
