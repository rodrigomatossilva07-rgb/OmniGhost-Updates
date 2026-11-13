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



void Initialize() noexcept;
void Shutdown() noexcept;
void Play(SoundEvent event) noexcept;
void PlayStartupOnce() noexcept;

}
