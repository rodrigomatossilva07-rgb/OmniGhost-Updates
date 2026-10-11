#include "ui_feedback.h"

#include "../config/app_settings.h"

#include <Windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace OmniGhost::UiFeedback {
namespace {

constexpr int kSampleRate = 44100;
constexpr float kPi = 3.14159265358979323846f;

struct Note {
    float frequency;
    float durationMs;
    float gain;
};

std::mutex g_audioMutex;
std::vector<std::uint8_t> g_activeWave;
std::array<ULONGLONG, 11> g_lastPlayed{};
bool g_startupPlayed = false;

bool IsNotification(SoundEvent event) noexcept {
    switch (event) {
    case SoundEvent::Success:
    case SoundEvent::Warning:
    case SoundEvent::Error:
    case SoundEvent::GameDetected:
    case SoundEvent::Update:
    case SoundEvent::Admin:
        return true;
    default:
        return false;
    }
}

std::vector<Note> NotesFor(SoundEvent event) {
    switch (event) {
    case SoundEvent::Click:        return {{720.f, 34.f, .42f}};
    case SoundEvent::ToggleOn:     return {{610.f, 32.f, .34f}, {920.f, 46.f, .43f}};
    case SoundEvent::ToggleOff:    return {{760.f, 32.f, .32f}, {470.f, 45.f, .36f}};
    case SoundEvent::Navigate:     return {{520.f, 30.f, .24f}, {680.f, 42.f, .31f}};
    case SoundEvent::Success:      return {{520.f, 50.f, .30f}, {700.f, 55.f, .38f}, {980.f, 74.f, .42f}};
    case SoundEvent::Warning:      return {{440.f, 68.f, .35f}, {440.f, 68.f, .31f}};
    case SoundEvent::Error:        return {{310.f, 58.f, .38f}, {220.f, 82.f, .43f}};
    case SoundEvent::GameDetected: return {{420.f, 42.f, .25f}, {630.f, 48.f, .34f}, {840.f, 68.f, .38f}};
    case SoundEvent::Startup:      return {{330.f, 48.f, .20f}, {495.f, 52.f, .28f}, {660.f, 60.f, .34f}, {990.f, 95.f, .39f}};
    case SoundEvent::Update:       return {{560.f, 44.f, .25f}, {740.f, 48.f, .31f}, {930.f, 52.f, .36f}, {1120.f, 74.f, .39f}};
    case SoundEvent::Admin:        return {{260.f, 45.f, .25f}, {520.f, 48.f, .33f}, {780.f, 72.f, .39f}};
    }
    return {};
}

void Write16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
}

void Write32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu);
}

std::vector<std::uint8_t> BuildWave(SoundEvent event, float volumePercent) {
    const std::vector<Note> notes = NotesFor(event);
    constexpr float gapMs = 8.f;
    std::size_t totalSamples = 0;
    for (const Note& note : notes)
        totalSamples += static_cast<std::size_t>((note.durationMs + gapMs) * kSampleRate / 1000.f);

    std::vector<std::int16_t> pcm(totalSamples, 0);
    std::size_t cursor = 0;
    const float master = std::clamp(volumePercent, 0.f, 100.f) / 100.f;
    for (const Note& note : notes) {
        const std::size_t noteSamples = static_cast<std::size_t>(note.durationMs * kSampleRate / 1000.f);
        for (std::size_t i = 0; i < noteSamples && cursor + i < pcm.size(); ++i) {
            const float phase = static_cast<float>(i) / static_cast<float>(kSampleRate);
            const float progress = static_cast<float>(i) / static_cast<float>(std::max<std::size_t>(1, noteSamples - 1));
            const float attack = std::min(1.f, progress * 14.f);
            const float release = std::pow(std::max(0.f, 1.f - progress), 2.2f);
            const float fundamental = std::sin(2.f * kPi * note.frequency * phase);
            const float shimmer = std::sin(2.f * kPi * note.frequency * 2.01f * phase) * .16f;
            const float sample = (fundamental + shimmer) * attack * release * note.gain * master;
            pcm[cursor + i] = static_cast<std::int16_t>(std::clamp(sample, -1.f, 1.f) * 32767.f);
        }
        cursor += noteSamples + static_cast<std::size_t>(gapMs * kSampleRate / 1000.f);
    }

    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    std::vector<std::uint8_t> wave(44u + dataBytes, 0);
    std::memcpy(wave.data(), "RIFF", 4);
    Write32(wave, 4, 36u + dataBytes);
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
    Write32(wave, 16, 16);
    Write16(wave, 20, 1);
    Write16(wave, 22, 1);
    Write32(wave, 24, kSampleRate);
    Write32(wave, 28, kSampleRate * 2);
    Write16(wave, 32, 2);
    Write16(wave, 34, 16);
    std::memcpy(wave.data() + 36, "data", 4);
    Write32(wave, 40, dataBytes);
    std::memcpy(wave.data() + 44, pcm.data(), dataBytes);
    return wave;
}

} // namespace

void Initialize() noexcept {
    std::scoped_lock lock(g_audioMutex);
    g_lastPlayed.fill(0);
}

void Shutdown() noexcept {
    std::scoped_lock lock(g_audioMutex);
    ::PlaySoundW(nullptr, nullptr, 0);
    g_activeWave.clear();
}

void Play(SoundEvent event) noexcept {
    const bool notification = IsNotification(event);
    if ((notification && !app_settings::config.notification_sounds) ||
        (!notification && !app_settings::config.ui_sounds) ||
        app_settings::config.ui_sound_volume <= 0.5f)
        return;

    try {
        std::scoped_lock lock(g_audioMutex);
        const std::size_t index = static_cast<std::size_t>(event);
        const ULONGLONG now = ::GetTickCount64();
        const ULONGLONG cooldown = notification ? 120u : 24u;
        if (index >= g_lastPlayed.size() || now - g_lastPlayed[index] < cooldown)
            return;
        g_lastPlayed[index] = now;

        // PlaySound replaces the previous asynchronous memory sound. Stop it
        // before replacing its backing storage so the pointer never dangles.
        ::PlaySoundW(nullptr, nullptr, 0);
        g_activeWave = BuildWave(event, app_settings::config.ui_sound_volume);
        if (!g_activeWave.empty())
            ::PlaySoundW(reinterpret_cast<LPCWSTR>(g_activeWave.data()), nullptr,
                         SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
    }
    catch (...) {
        // UI feedback is optional and must never affect input or rendering.
        ::OutputDebugStringW(L"OmniGhost: UI sound synthesis failed.\n");
    }
}

void PlayStartupOnce() noexcept {
    std::unique_lock lock(g_audioMutex);
    if (g_startupPlayed || !app_settings::config.startup_sound)
        return;
    g_startupPlayed = true;
    lock.unlock();
    Play(SoundEvent::Startup);
}

} // namespace OmniGhost::UiFeedback
