#pragma once

#include "config_manager.h"
#include "config_core.h"
#include <sstream>

namespace config_manager {


void SerializeESP(std::ostringstream& o);
bool DeserializeESP(const std::string& key, const std::string& val);

}