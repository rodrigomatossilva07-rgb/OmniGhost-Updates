#pragma once

// Centralised per-entity cache policy for ESP acquisition lanes.
// Keys are always (session_generation, pawn). Lookups never create entries.
// Prune on generation change, detach, map change, and periodic last_seen TTL.

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace OmniGhost::Gameplay {

template <typename T>
class EntityCacheHub {
public:
    struct Entry {
        T value{};
        uint64_t generation = 0;
        uint64_t last_seen_ms = 0;
        uint64_t expires_ms = 0; // 0 = no soft TTL
    };

    // Lookup only — never inserts.
    [[nodiscard]] Entry* Find(uintptr_t pawn, uint64_t generation) noexcept {
        auto it = map_.find(pawn);
        if (it == map_.end()) return nullptr;
        if (it->second.generation != generation) return nullptr;
        return &it->second;
    }

    [[nodiscard]] const Entry* Find(uintptr_t pawn, uint64_t generation) const noexcept {
        auto it = map_.find(pawn);
        if (it == map_.end()) return nullptr;
        if (it->second.generation != generation) return nullptr;
        return &it->second;
    }

    // Insert or replace. Caller owns policy for TTL.
    Entry& Upsert(uintptr_t pawn, uint64_t generation, uint64_t now_ms, uint64_t ttl_ms = 0) {
        auto& e = map_[pawn];
        if (e.generation != generation) {
            e = Entry{};
            e.generation = generation;
        }
        e.last_seen_ms = now_ms;
        e.expires_ms = ttl_ms ? (now_ms + ttl_ms) : 0;
        return e;
    }

    void Touch(uintptr_t pawn, uint64_t generation, uint64_t now_ms) noexcept {
        auto it = map_.find(pawn);
        if (it == map_.end()) return;
        if (it->second.generation != generation) return;
        it->second.last_seen_ms = now_ms;
    }

    void Erase(uintptr_t pawn) noexcept { map_.erase(pawn); }

    void Clear() noexcept { map_.clear(); }

    // Drop entries from other generations or past hard grace.
    void Prune(uint64_t generation, uint64_t now_ms, uint64_t hard_grace_ms) {
        for (auto it = map_.begin(); it != map_.end();) {
            const bool wrongGen = it->second.generation != generation;
            const bool stale = hard_grace_ms && now_ms > it->second.last_seen_ms &&
                (now_ms - it->second.last_seen_ms) > hard_grace_ms;
            const bool expired = it->second.expires_ms && now_ms >= it->second.expires_ms &&
                (now_ms - it->second.last_seen_ms) > hard_grace_ms;
            if (wrongGen || stale || expired)
                it = map_.erase(it);
            else
                ++it;
        }
    }

    void OnGenerationChange(uint64_t new_generation) {
        for (auto it = map_.begin(); it != map_.end();) {
            if (it->second.generation != new_generation)
                it = map_.erase(it);
            else
                ++it;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

    // Enumerate live keys for telemetry.
    void CollectSizes(std::size_t& out) const noexcept { out = map_.size(); }

private:
    std::unordered_map<uintptr_t, Entry> map_;
};

// Wall-clock budget for optional DMA phases (bones / metadata).
// Resets each scan; Consume returns false when budget is exhausted.
struct OptionalPhaseBudget {
    std::chrono::steady_clock::time_point start{};
    float budget_ms = 6.f;

    void Reset(float ms) noexcept {
        start = std::chrono::steady_clock::now();
        budget_ms = ms > 0.f ? ms : 0.f;
    }

    [[nodiscard]] float ElapsedMs() const noexcept {
        using namespace std::chrono;
        return duration<float, std::milli>(steady_clock::now() - start).count();
    }

    [[nodiscard]] bool Remaining() const noexcept {
        return ElapsedMs() < budget_ms;
    }

    [[nodiscard]] float LeftMs() const noexcept {
        const float e = ElapsedMs();
        return e < budget_ms ? (budget_ms - e) : 0.f;
    }
};

} // namespace OmniGhost::Gameplay
