#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>

namespace OmniGhost::Gameplay {

/**
 * @brief Unified process monitoring configuration
 * 
 * Standardizes process liveness checks across all games with consistent
 * thresholds based on wall-clock time rather than frame counts.
 */
struct ProcessMonitorConfig {
    // Time before considering process potentially dead (ms)
    uint64_t grace_period_ms = 3000;
    
    // Time before confirming process is dead (ms)
    uint64_t confirm_dead_ms = 5000;
    
    // Check interval (ms)
    uint64_t check_interval_ms = 1000;
    
    // Minimum consecutive misses before action
    int min_consecutive_misses = 2;
    
    // Whether to use local process list as fallback (for same-PC setups)
    bool use_local_fallback = true;
};

// Default configs per game type
inline ProcessMonitorConfig GetDefaultMonitorConfig(int game_type) {
    // 0 = CS2, 1 = FiveM, 2 = Warzone, 3 = Fortnite
    switch (game_type) {
        case 0: // CS2
            return {3000, 5000, 1000, 2, true};
        case 1: // FiveM
            return {5000, 10000, 1000, 3, true}; // Longer grace for FiveM's process name changes
        case 2: // Warzone
            return {2000, 4000, 1000, 2, false};
        case 3: // Fortnite
            return {1500, 3000, 750, 2, true};
        default:
            return {3000, 5000, 1000, 2, true};
    }
}

/**
 * @brief Generic process monitor with configurable thresholds
 * 
 * Replaces per-game ad-hoc process monitoring with a unified,
 * time-based approach using consistent thresholds.
 */
class ProcessMonitor {
public:
    explicit ProcessMonitor(ProcessMonitorConfig cfg = {}) : config_(cfg) {}
    
    // Update with current process alive status
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
    
    // Check if process should be considered dead
    bool ShouldTerminateSession() const {
        const auto now = GetTickCount64();
        
        // Not enough misses yet
        if (consecutive_misses_ < config_.min_consecutive_misses)
            return false;
        
        // Grace period not elapsed
        if (now - first_miss_ms_ < config_.grace_period_ms)
            return false;
        
        // Confirm period elapsed
        if (now - first_miss_ms_ >= config_.confirm_dead_ms) {
            return true;
        }
        
        return false;
    }
    
    // Get time since last confirmed alive (ms)
    uint64_t TimeSinceLastSeen() const {
        const auto now = GetTickCount64();
        return last_seen_ms_ ? now - last_seen_ms_ : UINT64_MAX;
    }
    
    // Reset monitor for new session
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

} // namespace OmniGhost::Gameplay