#include "cs2_visibility_worker.h"

#include "../trajectory/cs2_visibility.h"
#include "gameplay/dma_telemetry_log.h"

#include <Windows.h>

#include <chrono>
#include <utility>

namespace CS2::VisibilityPipeline {

Worker::~Worker() {
    Stop();
}

void Worker::Submit(std::shared_ptr<const Trajectory::CollisionBvh> world,
                    Trajectory::Vec3 origin, std::vector<Target> targets,
                    uint64_t generation) {
    std::unique_lock lock(mutex_);
    if (!thread_.joinable())
        thread_ = std::thread([this] { Run(); });
    job_ = {std::move(world), origin, std::move(targets), generation,
            epoch_.load(std::memory_order_acquire)};
    pending_ = true;
    cv_.notify_one();
}

Result Worker::Get(uintptr_t pawn, uint64_t generation) const {
    std::shared_lock lock(mutex_);
    const auto it = results_.find(pawn);
    return it != results_.end() && it->second.generation == generation
        ? it->second : Result{};
}

void Worker::Reset() {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    std::unique_lock lock(mutex_);
    pending_ = false;
    job_ = {};
    results_.clear();
}

void Worker::Stop() {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::unique_lock lock(mutex_);
        stop_.store(true, std::memory_order_release);
        pending_ = false;
        job_ = {};
    }
    cv_.notify_one();
    if (thread_.joinable())
        thread_.join();

    std::unique_lock lock(mutex_);
    results_.clear();
    stop_.store(false, std::memory_order_release);
}

void Worker::Run() {
    for (;;) {
        Job work;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_acquire) || pending_;
            });
            if (stop_.load(std::memory_order_acquire))
                return;
            work = std::move(job_);
            pending_ = false;
        }

        const auto begin = std::chrono::steady_clock::now();
        std::vector<std::pair<uintptr_t, Result>> complete;
        complete.reserve(work.targets.size());
        int rays_tested = 0;

        for (const Target& target : work.targets) {
            if (stop_.load(std::memory_order_acquire) ||
                work.epoch != epoch_.load(std::memory_order_acquire))
                break;

            int target_rays = 0;
            const auto visible = Visibility::CheckPoints(
                work.world.get(), work.origin, target.points, &target_rays, &stop_);
            rays_tested += target_rays;
            complete.push_back({target.pawn,
                {work.generation, GetTickCount64(), visible.has_value(),
                 visible.value_or(false)}});
        }

        auto& metrics = OmniGhost::Gameplay::DmaTelemetry::CS2();
        const float elapsed_ms = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
        metrics.visibility.visibility_ms.store(elapsed_ms, std::memory_order_relaxed);
        metrics.visibility.visibility_players_tested.store(
            static_cast<int>(complete.size()), std::memory_order_relaxed);
        metrics.visibility.visibility_rays.store(rays_tested, std::memory_order_relaxed);

        std::unique_lock lock(mutex_);
        if (!stop_.load(std::memory_order_acquire) &&
            work.epoch == epoch_.load(std::memory_order_acquire)) {
            for (const auto& [pawn, result] : complete)
                results_[pawn] = result;
        }
    }
}

}
