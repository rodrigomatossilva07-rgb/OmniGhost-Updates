#pragma once

#include "config_manager.h"
#include "config_core.h"
#include <sstream>

namespace config_manager {


void SerializeFriends(std::ostringstream& o);
bool DeserializeFriends(const std::string& key, const std::string& val);

}