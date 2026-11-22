#pragma once

// Build channel is defined at compile time via CMake (OMNIGHOST_BUILD_CHANNEL)
// Possible values: "Stable" (public), "Beta", "Development" (DEV - admin only)
#ifndef OMNIGHOST_BUILD_CHANNEL
#define OMNIGHOST_BUILD_CHANNEL "Stable"
#endif

// Release builds keep the public semantic version. Development receives its
// own prerelease version from CMake (for example 1.3.5-dev.1), preventing a
// private package from being mistaken for a stable release.
#ifndef OMNIGHOST_BUILD_VERSION
#define OMNIGHOST_BUILD_VERSION "1.3.4"
#endif

namespace OmniGhost {
inline constexpr char Version[] = OMNIGHOST_BUILD_VERSION;
inline constexpr char BuildChannel[] = OMNIGHOST_BUILD_CHANNEL;
inline constexpr wchar_t MainExecutable[] = L"OmniGhost.exe";
}
