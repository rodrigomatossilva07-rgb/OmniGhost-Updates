#pragma once

#include "config_manager.h"
#include "../platform/app_paths.h"
#include "../platform/text_encoding.h"
#include "../platform/scope_exit.h"
#include "../platform/session_log.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;

namespace config_manager {


inline constexpr int kConfigSchemaVersion = 4;
constexpr std::uintmax_t kMaxConfigBytes = 2u * 1024u * 1024u;


std::string CurrentAppVersion();
std::string NewConfigId();
bool FlushPathToDisk(const fs::path& path);
bool WriteTextAtomically(const fs::path& destination, const std::string& data, std::string& error);
bool ParseLine(const std::string& line, std::string& key, std::string& val);


void WBool(std::ostringstream& o, const char* k, bool v);
void WFloat(std::ostringstream& o, const char* k, float v);
void WInt(std::ostringstream& o, const char* k, int v);
void WU32(std::ostringstream& o, const char* k, ImU32 v);
void WStr(std::ostringstream& o, const char* k, const std::string& v);


bool toB(const std::string& val);
float toF(const std::string& val);
int toI(const std::string& val);
ImU32 toU(const std::string& val);


std::string ConfigDir();
void EnsureConfigDir();
void MigrateLegacyConfigs();
void RefreshList();
void SaveOrder();


bool SaveToFile(const std::string& name);
bool LoadFromFile(const std::string& name);
bool DeleteConfigFile(const std::string& name);
std::string SanitizeFileName(std::string name);
bool IsSafeConfigIdentifier(const std::string& name);


extern std::string last_status;
extern std::string active_config_name;
extern std::string active_profile_name;
extern std::vector<SavedConfigInfo> saved_list;
extern char new_config_name[64];


const char* ActiveGamePrefix();
const char* ActiveGameMeta();
bool NameBelongsToActiveGame(const std::string& name);


bool CopyToClipboard();
bool PasteFromClipboard();


void TickAutoSave();
void FlushActiveConfig();
const char* ActiveConfigName();

}