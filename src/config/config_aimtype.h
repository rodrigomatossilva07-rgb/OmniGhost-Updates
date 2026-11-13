#pragma once

#include "config_manager.h"
#include "config_core.h"
#include <sstream>

namespace config_manager {


void SerializeAimType(std::ostringstream& o);
bool DeserializeAimType(const std::string& key, const std::string& val);

}