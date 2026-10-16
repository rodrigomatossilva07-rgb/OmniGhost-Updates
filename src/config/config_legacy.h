#pragma once

#include "config_manager.h"

namespace config_manager {


void MigrateLegacyConfigs();


bool CopyToClipboard();
bool PasteFromClipboard();


void OpenConfigFolder();


std::vector<std::string> ListHistory();

}