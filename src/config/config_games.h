#pragma once

#include "config_manager.h"
#include "../src/games/Cs2/cs2_game.h"
#include "../src/games/Fortnite/fortnite_game.h"
#include "../src/games/Rust/rust_game.h"
#include <string>
#include <array>

namespace config_manager {


void SerializeCS2(std::ostringstream& o);
bool DeserializeCS2(const std::string& key, const std::string& val);



void SerializeRust(std::ostringstream& o);
bool DeserializeRust(const std::string& key, const std::string& val);




void SerializeFortnite(std::ostringstream& o);
bool DeserializeFortnite(const std::string& key, const std::string& val);


void SerializeFiveM(std::ostringstream& o);
bool DeserializeFiveM(const std::string& key, const std::string& val);


void SerializeColorArray(std::ostringstream& o, const char* prefix, const float value[4]);
bool DeserializeColorArray(const std::string& key, const std::string& val, const char* prefix, float value[4]);


void ResetCS2ToDefaults();


void ApplyMinimalCS2();

void ApplyVisualCS2();

}
