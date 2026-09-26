#pragma once

#include "config_manager.h"
#include "../src/games/Cs2/cs2_game.h"
#include "../src/games/Warzone/warzone_game.h"
#include <string>
#include <array>

namespace config_manager {

// CS2 config serialization
void SerializeCS2(std::ostringstream& o);
bool DeserializeCS2(const std::string& key, const std::string& val);

// Warzone config serialization
void SerializeWarzone(std::ostringstream& o);
bool DeserializeWarzone(const std::string& key, const std::string& val);

// FiveM (shared) config serialization
void SerializeFiveM(std::ostringstream& o);
bool DeserializeFiveM(const std::string& key, const std::string& val);

// Color helpers
void SerializeColorArray(std::ostringstream& o, const char* prefix, const float value[4]);
bool DeserializeColorArray(const std::string& key, const std::string& val, const char* prefix, float value[4]);

// Reset to defaults
void ResetCS2ToDefaults();
void ResetWarzoneToDefaults();

// Profile application
void ApplyMinimalCS2();
void ApplyMinimalWarzone();

void ApplyVisualCS2();
void ApplyVisualWarzone();

} // namespace config_manager
