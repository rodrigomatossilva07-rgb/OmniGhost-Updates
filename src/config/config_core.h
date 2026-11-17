#pragma once

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace OmniGhost::Config {

// =============================================================================
// Core Config Serialization Functions
// =============================================================================

// Serialize all current settings to JSON
nlohmann::json SerializeAllToJson();

// Deserialize all settings from JSON string
bool DeserializeAllFromJson(std::string_view json);

// Serialize specific sections
nlohmann::json SerializeESPSettings();
nlohmann::json SerializeAimSettings();
nlohmann::json SerializeTriggerSettings();
nlohmann::json SerializeRadarSettings();
nlohmann::json SerializeMiscSettings();

// Deserialize specific sections
bool DeserializeESPSettings(const nlohmann::json& j);
bool DeserializeAimSettings(const nlohmann::json& j);
bool DeserializeTriggerSettings(const nlohmann::json& j);
bool DeserializeRadarSettings(const nlohmann::json& j);
bool DeserializeMiscSettings(const nlohmann::json& j);

// Game-specific serialization
nlohmann::json SerializeCS2Settings();
nlohmann::json SerializeRustSettings();
nlohmann::json SerializeFiveMSettings();
nlohmann::json SerializeFortniteSettings();
nlohmann::json SerializeWarzoneSettings();
nlohmann::json SerializeApexSettings();

bool DeserializeCS2Settings(const nlohmann::json& j);
bool DeserializeRustSettings(const nlohmann::json& j);
bool DeserializeFiveMSettings(const nlohmann::json& j);
bool DeserializeFortniteSettings(const nlohmann::json& j);
bool DeserializeWarzoneSettings(const nlohmann::json& j);
bool DeserializeApexSettings(const nlohmann::json& j);

// Get current game name
std::string GetCurrentGameName();

} // namespace OmniGhost::Config