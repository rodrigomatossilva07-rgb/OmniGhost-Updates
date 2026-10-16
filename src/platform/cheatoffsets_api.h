#pragma once
#include <string>

namespace OmniGhost {
namespace CheatOffsets {



bool FetchGame(const char* slug, const char* out_json_path, std::string* status = nullptr);
bool FetchGameIfChanged(const char* slug, const char* out_json_path, std::string* status = nullptr);


bool FetchRust(std::string* status = nullptr);
bool FetchCS2(std::string* status = nullptr);
bool FetchApex(std::string* status = nullptr);
bool FetchFiveM(std::string* status = nullptr);
bool FetchWarzone(std::string* status = nullptr);


int FetchAll(std::string* status = nullptr);

}
}
