#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>

namespace OmniGhost::Gameplay {







struct ProcessMonitorConfig {

    uint64_t grace_period_ms = 3000;


    uint64_t confirm_dead_ms = 5000;


    uint64_t check_interval_ms = 1000;


    int min_consecutive_misses = 2;


    bool use_local_fallback = true;
};


inline ProcessMonitorConfig GetDefaultMonitorConfig(int game_type) {

    switch (game_type) {
        case 0:
            return {3000, 5000, 1000, 2, true};
        case 1:
            return {5000, 10000, 1000, 3, true};
        case 2:
            return {2000, 4000, 1000, 2, false};
        case 3:
            return {1500, 3000, 750, 2, true};
        default:
            return {3000, 5000, 1000, 2, true};
    }
}







class ProcessMonitor {
public:
    explicit ProcessMonitor(ProcessMonitorConfig cfg = {}) : config_(cfg) {}


    void Update(bool dma_process_alive, bool local_process_alive = false) {
        const auto now = GetTickCount64();
        const bool any_alive = dma_process_alive || (config_.use_local_fallback && local_process_alive);

        if (any_alive) {
            consecutive_misses_ = 0;
            last_seen_ms_ = now;
            process_confirmed_dead_ = false;
        } else {
            consecutive_misses_++;
            if (consecutive_misses_ == 1) {
                first_miss_ms_ = now;
            }
        }
    }


    bool ShouldTerminateSession() const {
        const auto now = GetTickCount64();


        if (consecutive_misses_ < config_.min_consecutive_misses)
            return false;


        if (now - first_miss_ms_ < config_.grace_period_ms)
            return false;


        if (now - first_miss_ms_ >= config_.confirm_dead_ms) {
            return true;
        }

        return false;
    }


    uint64_t TimeSinceLastSeen() const {
        const auto now = GetTickCount64();
        return last_seen_ms_ ? now - last_seen_ms_ : UINT64_MAX;
    }


    void Reset() {
        consecutive_misses_ = 0;
        first_miss_ms_ = 0;
        last_seen_ms_ = 0;
        process_confirmed_dead_ = false;
    }

    bool IsProcessConfirmedDead() const { return process_confirmed_dead_; }
    int GetConsecutiveMisses() const { return consecutive_misses_; }

private:
    ProcessMonitorConfig config_;
    mutable int consecutive_misses_ = 0;
    mutable uint64_t first_miss_ms_ = 0;
    mutable uint64_t last_seen_ms_ = 0;
    mutable bool process_confirmed_dead_ = false;
};

}