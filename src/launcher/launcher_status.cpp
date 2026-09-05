#include "launcher_status.h"
#include "../window/localization.h"
#include "../../Fivem/aimbot/aim_type.h"
#include "../makcu/makcu_wrapper.h"
#include "../kmbox/kmbox_net.h"
#include "../ferrum/ferrum_device.h"

#include <string>

namespace Launcher {
const char* InputDeviceName() {
    switch (aim_type::config.active) {
    case aim_type::DeviceType::Makcu: return "Makcu";
    case aim_type::DeviceType::Ferrum: return "Ferrum";
    case aim_type::DeviceType::KmboxNet: return "KMBox Net";
    default: return Loc::Tr("launcher.none");
    }
}

bool InputDeviceConnected() {
    switch (aim_type::config.active) {
    case aim_type::DeviceType::Makcu: return makcu_wrapper::IsConnected();
    case aim_type::DeviceType::Ferrum: return ferrum_device::IsConnected();
    case aim_type::DeviceType::KmboxNet: return kmbox_net::IsConnected();
    default: return false;
    }
}

const char* UpdateStatusText(const OmniGhost::Update::Snapshot& snapshot) {
    using OmniGhost::Update::Status;
    switch (snapshot.status) {
    case Status::Checking: return Loc::Tr("launcher.checking");
    case Status::Available:
    case Status::Downloading:
    case Status::Ready:
    case Status::Installing: return Loc::Tr("launcher.available");
    case Status::Error: return Loc::Tr("launcher.error");
    case Status::UpToDate:
    case Status::Idle:
    case Status::Deferred:
    default: return Loc::Tr("launcher.current");
    }
}

const char* LocalizedChannelLabel(std::string_view channel) {
    if (channel == "stable" || channel == "Stable") return Loc::Tr("launcher.stable");
    if (channel == "development" || channel == "Development") return Loc::Tr("launcher.development");
    if (channel == "beta" || channel == "Beta") return "Beta";
    if (channel.empty() || channel == "unknown") return Loc::Tr("launcher.unknown");
    thread_local std::string customChannel;
    customChannel.assign(channel);
    return customChannel.c_str();
}

} // namespace Launcher
