#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace CS2::Collision {

// Single owned worker for map BVH construction. Requests are latest-state-wins;
// reset/map-change cancels the current build cooperatively and invalidates the
// loaded cache before another map can be published.
class Loader final {
public:
    Loader() = default;
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    void Request(const char* map_name, bool needed, uint64_t now_ms);
    void Reset();
    void Stop();

private:
    void EnsureStartedLocked();
    void Run();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic_bool cancel_{false};
    bool stop_ = false;
    bool pending_ = false;
    std::string requested_map_;
    std::string active_map_;
    std::string attempted_map_;
    uint64_t retry_at_ms_ = 0;
    uint64_t epoch_ = 1;
};

} // namespace CS2::Collision
