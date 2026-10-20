#include "third_party/keyauth/x64/lib/utils.hpp"
#include <atlsecurity.h>

std::string utils::get_hwid() {
    ATL::CAccessToken accessToken;
    ATL::CSid currentUserSid;
    if (accessToken.GetProcessToken(TOKEN_READ | TOKEN_QUERY) &&
        accessToken.GetUser(&currentUserSid))
        return std::string(CT2A(currentUserSid.Sid()));
    return "none";
}
