#pragma once

#include "config_manager.h"
#include "config_core.h"
#include <sstream>

namespace config_manager {

// Aimbot config serialization
void SerializeAim(std::ostringstream& o);
bool DeserializeAim(const std::string& key, const std::string& val);

} // namespace config_manager