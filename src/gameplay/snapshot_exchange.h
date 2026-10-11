#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>

namespace OmniGhost::Gameplay {

/**
 * @brief Single-Producer / Multi-Consumer Lock-Free Snapshot Exchange
 *
 * THREADING MODEL:
 * ----------------
 * This class implements a lock-free snapshot exchange with the following guarantees:
 *
 * 1. SINGLE PRODUCER: Only ONE thread may call TryBeginWrite() and Publish().
 *    The producer thread is registered on first use (debug builds assert on violation).
 *    Legitimate producer handoff (e.g., main thread init → worker thread) requires
 *    calling ResetProducerThread() while no other producer is active.
 *
 * 2. MULTI CONSUMER: Any number of threads may call Acquire() simultaneously.
 *    Each Acquire() returns a ReadLease that pins the current snapshot.
 *    ReadLease destructor automatically unpins (reference counting).
 *
 * 3. SLOT COUNT: Minimum 3 slots (enforced by static_assert).
 *    - Slot 0: Published snapshot (readers may hold)
 *    - Slot 1: Next snapshot being written (producer owns)
 *    - Slot 2: Spare/previous snapshot (readers may still hold)
 *    This prevents a slow renderer from blocking the acquisition thread.
 *
 * 4. MEMORY ORDERING:
 *    - Publish(): release store to published_ + generation increment (release)
 *    - Acquire(): acquire load of published_, then acquire fence via reader count
 *    - This ensures snapshot writes are visible to readers after Acquire() returns.
 *
 * 5. ABA PROTECTION: Generation counter increments on each Publish().
 *    Consumers can track Generation() to detect missed snapshots.
 *
 * 6. NON-BLOCKING: TryBeginWrite() never waits. Returns empty if no free slot.
 *    Acquire() spin-locks briefly on publication race (typically 1 iteration).
 *
 * USAGE PATTERNS:
 * --------------
 * Producer (acquisition thread):
 *     auto slot = exchange.TryBeginWrite();
 *     if (slot) {
 *         *slot.value = new_snapshot;
 *         exchange.Publish(slot.index);
 *     }
 *
 * Consumer (render thread):
 *     if (auto lease = exchange.Acquire()) {
 *         use(*lease);  // immutable snapshot
 *     }  // auto-unpin on lease destruction
 *
 * Producer handoff (initialization):
 *     // Main thread publishes initial snapshot
 *     exchange.ResetProducerThread();  // allow worker to become producer
 *     // Worker thread now owns production
 */
template <typename T, std::size_t SlotCount = 3>
class SnapshotExchange final {
    static_assert(SlotCount >= 3, "Three slots prevent a slow renderer from blocking acquisition");

public:
    // ... rest of class unchanged
    class ReadLease final {
    public:
        ReadLease() = default;
        ReadLease(const ReadLease&) = delete;
        ReadLease& operator=(const ReadLease&) = delete;

        ReadLease(ReadLease&& other) noexcept { MoveFrom(std::move(other)); }
        ReadLease& operator=(ReadLease&& other) noexcept {
            if (this != &other) {
                Reset();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        ~ReadLease() { Reset(); }

        [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }
        [[nodiscard]] const T* operator->() const noexcept { return value_; }
        [[nodiscard]] const T& operator*() const noexcept { return *value_; }

    private:
        friend class SnapshotExchange;
        ReadLease(const SnapshotExchange* owner, std::uint32_t slot, const T* value) noexcept
            : owner_(owner), slot_(slot), value_(value) {}

        void Reset() noexcept {
            if (owner_)
                owner_->readers_[slot_].fetch_sub(1, std::memory_order_release);
            owner_ = nullptr;
            value_ = nullptr;
        }

        void MoveFrom(ReadLease&& other) noexcept {
            owner_ = other.owner_;
            slot_ = other.slot_;
            value_ = other.value_;
            other.owner_ = nullptr;
            other.value_ = nullptr;
        }

        const SnapshotExchange* owner_ = nullptr;
        std::uint32_t slot_ = 0;
        const T* value_ = nullptr;
    };

    struct WriteSlot final {
        T* value = nullptr;
        std::uint32_t index = 0;
        [[nodiscard]] explicit operator bool() const noexcept { return value != nullptr; }
    };

    SnapshotExchange() = default;
    SnapshotExchange(const SnapshotExchange&) = delete;
    SnapshotExchange& operator=(const SnapshotExchange&) = delete;

    [[nodiscard]] WriteSlot TryBeginWrite() noexcept {
        AssertSingleProducer();
        const std::uint32_t published = published_.load(std::memory_order_acquire);
        for (std::size_t n = 1; n <= SlotCount; ++n) {
            const auto candidate = static_cast<std::uint32_t>((write_cursor_ + n) % SlotCount);
            if (candidate == published) continue;
            if (readers_[candidate].load(std::memory_order_acquire) != 0) continue;
            write_cursor_ = candidate;
            return { &slots_[candidate], candidate };
        }
        return {};
    }

    void Publish(std::uint32_t slot) noexcept {
        AssertSingleProducer();
        if (slot >= SlotCount) return;
        // FIX BUG-009: Use seq_cst for generation to prevent ABA reordering
        // between published_ store and generation_ increment.
        // seq_cst ensures all threads see a consistent order of these operations.
        published_.store(slot, std::memory_order_release);
        generation_.fetch_add(1, std::memory_order_seq_cst);
    }

    [[nodiscard]] ReadLease Acquire() const noexcept {
        for (;;) {
            const std::uint32_t slot = published_.load(std::memory_order_acquire);
            readers_[slot].fetch_add(1, std::memory_order_acquire);
            if (slot == published_.load(std::memory_order_acquire))
                return ReadLease(this, slot, &slots_[slot]);
            readers_[slot].fetch_sub(1, std::memory_order_release);
        }
    }

    [[nodiscard]] std::uint64_t Generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

    // Call only after the previous producer has stopped (or before its first
    // publication). This permits an explicit main-thread -> worker handoff.
    void ResetProducerThread() noexcept {
#ifndef NDEBUG
        producer_thread_.store(0, std::memory_order_relaxed);
#endif
    }

private:
    std::array<T, SlotCount> slots_{};
    mutable std::array<std::atomic<std::uint32_t>, SlotCount> readers_{};
    std::atomic<std::uint32_t> published_{0};
    std::atomic<std::uint64_t> generation_{0};
    std::uint32_t write_cursor_ = 0; // producer-only

#ifndef NDEBUG
    // Regression guard: this exchanger is single-producer by design
    // (write_cursor_ is a plain member). A second producer thread is a
    // data race on both the cursor and the slot payload — fail fast.
    // A legitimate handoff (e.g. init on main thread, then the worker)
    // must call ResetProducerThread() while no other producer is running.
    void AssertSingleProducer() const noexcept {
        const std::uintptr_t self =
            static_cast<std::uintptr_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::uintptr_t expected = 0;
        if (producer_thread_.compare_exchange_strong(expected, self, std::memory_order_relaxed))
            return;
        assert(expected == self && "SnapshotExchange: multiple producer threads detected");
        (void)expected;
    }
    mutable std::atomic<std::uintptr_t> producer_thread_{0};
#else
    void AssertSingleProducer() const noexcept {}
#endif
};

} // namespace OmniGhost::Gameplay
