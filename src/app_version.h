#pragma once

// Build channel is defined at compile time via CMake (OMNIGHOST_BUILD_CHANNEL)
// Possible values: "Stable" (public), "Beta", "Development" (DEV - admin only)
#ifndef OMNIGHOST_BUILD_CHANNEL
#define OMNIGHOST_BUILD_CHANNEL "Stable"
#endif

// Version is synchronized from src/project/config/release/version.txt by sync_version.ps1
// DO NOT EDIT MANUALLY - use the sync script instead.
namespace OmniGhost {
inline constexpr char Version[] = "1.3.4";
inline constexpr char BuildChannel[] = OMNIGHOST_BUILD_CHANNEL;
inline constexpr wchar_t MainExecutable[] = L"OmniGhost.exe";
}
