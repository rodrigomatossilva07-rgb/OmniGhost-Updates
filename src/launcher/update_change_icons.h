#pragma once

#include "changelog_service.h"

namespace LauncherUpdates {

inline const char* ChangeTypeIcon(Launcher::ChangeType type) {
    switch (type) {
    case Launcher::ChangeType::Added: return "✨";
    case Launcher::ChangeType::Improved: return "🚀";
    case Launcher::ChangeType::Fixed: return "🔧";
    case Launcher::ChangeType::Performance: return "⚡";
    case Launcher::ChangeType::Compatibility: return "🔄";
    case Launcher::ChangeType::Security: return "🔒";
    case Launcher::ChangeType::Removed: return "🗑";
    case Launcher::ChangeType::Breaking: return "⚠";
    case Launcher::ChangeType::Maintenance: return "🔧";
    default: return "•";
    }
}

} // namespace LauncherUpdates
