#pragma once

#include "config_manager.h"

namespace config_manager {

// Legacy migration
void MigrateLegacyConfigs();

// Clipboard
bool CopyToClipboard();
bool PasteFromClipboard();

// Folder operations
void OpenConfigFolder();

// History
std::vector<std::string> ListHistory();

} // namespace config_manager