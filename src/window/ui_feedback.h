#pragma once

namespace OmniGhost::UiFeedback {

enum class SoundEvent {
    Click,
    ToggleOn,
    ToggleOff,
    Navigate,
    Success,
    Warning,
    Error,
    GameDetected,
    Startup,
    Update,
    Admin
};

// Audio is synthesized locally into short PCM buffers. No sound assets,
// device identifiers, or user data leave the process.
void Initialize() noexcept;
void Shutdown() noexcept;
void Play(SoundEvent event) noexcept;
void PlayStartupOnce() noexcept;

} // namespace OmniGhost::UiFeedback
