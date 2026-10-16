#pragma once

#include "config_manager.h"
#include "config_core.h"
#include <sstream>

namespace config_manager {


void SerializeAim(std::ostringstream& o);
bool DeserializeAim(const std::string& key, const std::string& val);

}