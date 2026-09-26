#pragma once

#include "config_manager.h"
#include "config_core.h"
#include <sstream>

namespace config_manager {

// Aim type config serialization
void SerializeAimType(std::ostringstream& o);
bool DeserializeAimType(const std::string& key, const std::string& val);

} // namespace config_manager