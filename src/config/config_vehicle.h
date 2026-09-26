#pragma once

#include "config_manager.h"
#include "config_core.h"
#include <sstream>

namespace config_manager {

// Vehicle ESP config serialization
void SerializeVehicle(std::ostringstream& o);
bool DeserializeVehicle(const std::string& key, const std::string& val);

} // namespace config_manager