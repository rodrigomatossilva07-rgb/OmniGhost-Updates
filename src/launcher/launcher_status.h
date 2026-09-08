#pragma once

#include "../updater/update_service.h"

#include <string_view>

namespace Launcher {
const char* InputDeviceName();
bool InputDeviceConnected();
const char* UpdateStatusText(const OmniGhost::Update::Snapshot& snapshot);
const char* LocalizedChannelLabel(std::string_view channel);
} // namespace Launcher
