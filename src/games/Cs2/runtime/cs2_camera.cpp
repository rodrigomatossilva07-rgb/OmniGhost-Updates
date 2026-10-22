#include "cs2_camera.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace CS2::Camera {
namespace {

OmniGhost::Gameplay::SnapshotExchange<CameraSnapshot> g_snapshots;
std::mutex g_history_mutex;
std::array<CameraSnapshot, 10> g_history{};
std::size_t g_history_next = 0;
std::atomic<uint64_t> g_deadline_ms{0};
std::atomic_bool g_pending{false};

}

void Publish(const float* matrix, uint64_t sample_ms) {
    if (!matrix)
        return;

    auto slot = g_snapshots.TryBeginWrite();
    if (!slot)
        return;

    std::memcpy(slot.value->view_matrix, matrix, sizeof(slot.value->view_matrix));
    slot.value->timestamp_ms = sample_ms ? sample_ms : GetTickCount64();
    {
        std::lock_guard lock(g_history_mutex);
        g_history[g_history_next] = *slot.value;
        g_history_next = (g_history_next + 1u) % g_history.size();
    }
    g_snapshots.Publish(slot.index);
}

CameraSnapshotLease Acquire() {
    return g_snapshots.Acquire();
}

CameraSnapshot Select(uint64_t world_sample_ms) {
    CameraSnapshot best{};
    uint64_t best_delta = (std::numeric_limits<uint64_t>::max)();

    std::lock_guard lock(g_history_mutex);
    for (const CameraSnapshot& candidate : g_history) {
        if (!candidate.timestamp_ms || candidate.timestamp_ms > world_sample_ms)
            continue;

        const uint64_t delta = world_sample_ms - candidate.timestamp_ms;
        if (delta < best_delta) {
            best = candidate;
            best_delta = delta;
        }
    }
    return best;
}

CameraSnapshot Predict(uint64_t presentation_ms) {
    CameraSnapshot newest{};
    CameraSnapshot previous{};

    {
        std::lock_guard lock(g_history_mutex);
        for (const CameraSnapshot& candidate : g_history) {
            if (!candidate.timestamp_ms || candidate.timestamp_ms > presentation_ms)
                continue;
            if (candidate.timestamp_ms > newest.timestamp_ms) {
                previous = newest;
                newest = candidate;
            } else if (candidate.timestamp_ms > previous.timestamp_ms) {
                previous = candidate;
            }
        }
    }

    if (!newest.timestamp_ms || !previous.timestamp_ms ||
        newest.timestamp_ms <= previous.timestamp_ms ||
        presentation_ms <= newest.timestamp_ms)
        return newest;

    const uint64_t sampleDelta = newest.timestamp_ms - previous.timestamp_ms;
    const uint64_t sourceAge = presentation_ms - newest.timestamp_ms;
    if (sampleDelta > 20u || sourceAge > 20u)
        return newest;

    // Compensa apenas até um intervalo da câmara. Uma extrapolação curta dos
    // coeficientes da matriz remove o arrasto visível durante rotações rápidas
    // sem permitir que uma leitura atrasada produza uma projeção instável.
    const float factor = std::clamp(
        static_cast<float>((std::min)(sourceAge, uint64_t{8})) /
            static_cast<float>((std::max)(sampleDelta, uint64_t{1})),
        0.f, 1.f);
    CameraSnapshot predicted = newest;
    for (std::size_t i = 0; i < 16; ++i) {
        const float value = newest.view_matrix[i] +
            (newest.view_matrix[i] - previous.view_matrix[i]) * factor;
        if (!std::isfinite(value))
            return newest;
        predicted.view_matrix[i] = value;
    }
    predicted.timestamp_ms = presentation_ms;
    return predicted;
}

void Reset() noexcept {
    g_deadline_ms.store(0, std::memory_order_release);
    g_pending.store(false, std::memory_order_release);
    std::lock_guard lock(g_history_mutex);
    g_history = {};
    g_history_next = 0;
}

void SetDeadline(uint64_t deadline_ms) noexcept {
    g_deadline_ms.store(deadline_ms, std::memory_order_release);
}

uint64_t Deadline() noexcept {
    return g_deadline_ms.load(std::memory_order_acquire);
}

void SetPending(bool pending) noexcept {
    g_pending.store(pending, std::memory_order_release);
}

bool Pending() noexcept {
    return g_pending.load(std::memory_order_acquire);
}

}
