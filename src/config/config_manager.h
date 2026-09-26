#pragma once

#include "../ui/ui_models.h"
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>

namespace config_manager {

enum class GameProfile : int {
    Default = 0,
    Minimal = 1,
    Visual = 2,
    Custom = 3
};

struct SavedConfigInfo {
    std::string name;
    std::string path;
    std::string date_str;
    bool favorite = false;
};

extern std::vector<SavedConfigInfo> saved_list;
extern char new_config_name[64];
extern std::string last_status;
extern std::string active_config_name;
extern std::string active_profile_name;
extern std::string active_config_id;

void Initialize();
void ActivateCurrentGameConfig();
void RefreshList();
void SaveOrder();
void MoveConfig(int from, int to);
bool ToggleFavoriteConfig(const std::string& name);
bool IsFavoriteConfig(const std::string& name);

std::string SerializeAll();
bool DeserializeAll(const std::string& data);

bool SaveToFile(const std::string& name);
bool LoadFromFile(const std::string& name);
bool DeleteConfigFile(const std::string& name);

bool CopyToClipboard();
bool PasteFromClipboard();
void ResetToDefaults();
void ResetActiveGameToDefaults();
void ResetSettingsOnly();

std::string GetConfigFolder();
void OpenConfigFolder();
void TickAutoSave();
void FlushActiveConfig();
const char* ActiveConfigName();

void ApplyGameProfile(GameProfile profile);
bool SaveCustomGameProfile();
const char* CurrentGameProfileName();
void ApplyNamedProfile(const char* name);
void SaveNamedProfile(const char* name);
std::vector<std::string> ListHistory();
void MigrateLegacyConfigs();

void SerializeESP(std::ostringstream& o);
bool DeserializeESP(const std::string& key, const std::string& val);
void SerializeAim(std::ostringstream& o);
bool DeserializeAim(const std::string& key, const std::string& val);
void SerializeVehicle(std::ostringstream& o);
bool DeserializeVehicle(const std::string& key, const std::string& val);
void SerializeFriends(std::ostringstream& o);
bool DeserializeFriends(const std::string& key, const std::string& val);
void SerializeAimType(std::ostringstream& o);
bool DeserializeAimType(const std::string& key, const std::string& val);
void SerializeCS2(std::ostringstream& o);
bool DeserializeCS2(const std::string& key, const std::string& val);
void SerializeWarzone(std::ostringstream& o);
bool DeserializeWarzone(const std::string& key, const std::string& val);
void SerializeFiveM(std::ostringstream& o);
bool DeserializeFiveM(const std::string& key, const std::string& val);
void SerializeColorArray(std::ostringstream& o, const char* prefix, const float value[4]);
bool DeserializeColorArray(const std::string& key, const std::string& val, const char* prefix, float value[4]);

void ResetCS2ToDefaults();
void ResetWarzoneToDefaults();
void ApplyMinimalCS2();
void ApplyMinimalWarzone();
void ApplyVisualCS2();
void ApplyVisualWarzone();
void ApplyMinimalProfileForActiveGame();
void ApplyVisualProfileForActiveGame();

} // namespace config_manager
