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

enum class CommunityScope : int { Community = 0, Marketplace = 1, Mine = 2, Favorites = 3, Downloads = 4 };

struct CommunityConfigInfo {
    std::string id;
    std::string game;
    std::string title;
    std::string description;
    std::string author;
    std::string visibility;
    std::string status;
    std::string updated_at;
    std::vector<std::string> tags;
    int version = 1;
    int schema_version = 1;
    int downloads = 0;
    int favorites = 0;
    int rating_count = 0;
    float rating = 0.0f;
    int my_rating = 0;
    bool official = false;
    bool verified = false;
    bool favorite = false;
    bool owned = false;
    bool compatible = true;
    std::string min_launcher_version;
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
bool DuplicateConfigFile(const std::string& name, const std::string& new_name);
bool RenameConfigFile(const std::string& name, const std::string& new_name);
bool ExportJson(const std::string& file_name = "OmniGhost-export.json");
bool ImportJson(const std::string& file_name = "OmniGhost-import.json");
bool UploadCloudConfig(const std::string& name);
bool DownloadCloudConfig(const std::string& name);
bool DeleteCloudConfig(const std::string& name);
bool RefreshCommunityConfigs(CommunityScope scope, const std::string& search = {}, const std::string& sort = "popular");
bool RefreshCommunityConfigsForGame(const std::string& game, CommunityScope scope,
                                    const std::string& search = {}, const std::string& sort = "popular");
const std::vector<CommunityConfigInfo>& CommunityConfigs();
bool PublishCommunityConfig(const std::string& title, const std::string& description,
                            const std::string& visibility, const std::string& tags,
                            const std::string& changelog = {}, const std::string& config_id = {});
bool ApplyCommunityConfig(const std::string& config_id, bool save_copy);
bool DownloadCommunityToLibrary(const std::string& config_id);
bool PreviewCommunityConfig(const std::string& config_id);
bool ToggleCommunityFavorite(const std::string& config_id, bool enabled);
bool RateCommunityConfig(const std::string& config_id, int rating);
bool ReportCommunityConfig(const std::string& config_id, const std::string& reason);
bool WithdrawCommunityConfig(const std::string& config_id);
bool RevertCommunityConfig();

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

}
