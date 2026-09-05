#pragma once

namespace OmniGhost::CrashHandler {

enum class PreviousRunState {
    Clean,
    StartupInterrupted,
    RuntimeInterrupted
};

bool Install() noexcept;
void MarkStartupComplete() noexcept;
void MarkCleanShutdown() noexcept;
[[nodiscard]] PreviousRunState PreviousRun() noexcept;
[[nodiscard]] const char* PreviousRunStateName(PreviousRunState state) noexcept;

} // namespace OmniGhost::CrashHandler
