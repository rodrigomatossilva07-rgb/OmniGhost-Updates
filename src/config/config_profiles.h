#pragma once

#include "config_manager.h"

namespace config_manager {

void ApplyMinimalProfileForActiveGame();
void ApplyVisualProfileForActiveGame();
void ApplyGameProfile(GameProfile profile);
bool SaveCustomGameProfile();
const char* CurrentGameProfileName();
void ApplyNamedProfile(const char* name);
void SaveNamedProfile(const char* name);

void ResetActiveGameToDefaults();
void ResetToDefaults();
void ResetSettingsOnly();

}