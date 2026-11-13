#include "cs2_collision_loader.h"

#include "../trajectory/cs2_map_collision_cache.h"

#include <iostream>
#include <utility>

namespace CS2::Collision {

Loader::~Loader() {
    Stop();
}

void Loader::EnsureStartedLocked() {
    if (!thread_.joinable())
        thread_ = std::thread([this] { Run(); });
}

void Loader::Request(const char* map_name, bool needed, uint64_t now_ms) {
    auto& cache = Trajectory::CollisionCache();
    if (!needed) {





        return;
    }
    if (!map_name || !*map_name || cache.IsLoadedFor(map_name))
        return;

    std::lock_guard lock(mutex_);
    const std::string requested(map_name);
    if (requested == attempted_map_ && now_ms < retry_at_ms_)
        return;
    if (requested == requested_map_ && pending_)
        return;
    if (requested == active_map_ && !cancel_.load(std::memory_order_acquire))
        return;

    EnsureStartedLocked();
    cancel_.store(true, std::memory_order_release);
    requested_map_ = requested;
    attempted_map_ = requested;
    retry_at_ms_ = now_ms + 5000u;
    ++epoch_;
    pending_ = true;
    cv_.notify_one();
}

void Loader::Reset() {
    cancel_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(mutex_);
        ++epoch_;
        pending_ = false;
        requested_map_.clear();
        active_map_.clear();
        attempted_map_.clear();
        retry_at_ms_ = 0;
    }
    Trajectory::CollisionCache().Clear();
}

void Loader::Stop() {
    cancel_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(mutex_);
        if (!thread_.joinable()) {
            Trajectory::CollisionCache().Clear();
            return;
        }
        stop_ = true;
        pending_ = false;
        requested_map_.clear();
    }
    cv_.notify_one();
    thread_.join();
    {
        std::lock_guard lock(mutex_);
        stop_ = false;
        attempted_map_.clear();
        retry_at_ms_ = 0;
    }
    Trajectory::CollisionCache().Clear();
}

void Loader::Run() {
    for (;;) {
        std::string map;
        uint64_t epoch = 0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || pending_; });
            if (stop_)
                return;
            map = requested_map_;
            requested_map_.clear();
            pending_ = false;
            active_map_ = map;
            epoch = epoch_;
            cancel_.store(false, std::memory_order_release);
        }

        bool loaded = false;
        try {
            loaded = Trajectory::CollisionCache().LoadForMap(map.c_str(), &cancel_);
        } catch (const std::exception& ex) {
            std::cerr << "[CS2] Falha ao preparar colisao de " << map
                      << ": " << ex.what() << '\n';
        } catch (...) {
            std::cerr << "[CS2] Falha desconhecida ao preparar colisao de "
                      << map << '\n';
        }

        std::lock_guard lock(mutex_);
        if (active_map_ == map)
            active_map_.clear();
        if (epoch != epoch_ || cancel_.load(std::memory_order_acquire)) {
            if (Trajectory::CollisionCache().IsLoadedFor(map.c_str()))
                Trajectory::CollisionCache().Clear();
        } else if (loaded) {
            attempted_map_.clear();
            retry_at_ms_ = 0;
        }


        cancel_.store(true, std::memory_order_release);
    }
}

}
