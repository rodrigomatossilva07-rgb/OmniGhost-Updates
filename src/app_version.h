#pragma once



#ifndef OMNIGHOST_BUILD_CHANNEL
#define OMNIGHOST_BUILD_CHANNEL "Stable"
#endif



namespace OmniGhost {
inline constexpr char Version[] = "1.4.6";
inline constexpr char BuildChannel[] = OMNIGHOST_BUILD_CHANNEL;
inline constexpr wchar_t MainExecutable[] = L"OmniGhost.exe";
}
