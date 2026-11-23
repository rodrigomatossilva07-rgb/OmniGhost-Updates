#pragma once

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace OmniGhost::Config {






nlohmann::json SerializeAllToJson();


bool DeserializeAllFromJson(std::string_view json);


nlohmann::json SerializeESPSettings();
nlohmann::json SerializeAimSettings();
nlohmann::json SerializeTriggerSettings();
nlohmann::json SerializeRadarSettings();
nlohmann::json SerializeMiscSettings();


bool DeserializeESPSettings(const nlohmann::json& j);
bool DeserializeAimSettings(const nlohmann::json& j);
bool DeserializeTriggerSettings(const nlohmann::json& j);
bool DeserializeRadarSettings(const nlohmann::json& j);
bool DeserializeMiscSettings(const nlohmann::json& j);


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


std::string GetCurrentGameName();

}