#pragma once

#include <string>
#include <string_view>

namespace OmniGhost::RadarAccess {



bool GenerateSessionToken(std::string& token, std::string& error);


std::string HeaderValue(std::string_view request, std::string_view name);
bool ConstantTimeEquals(std::string_view left, std::string_view right) noexcept;
bool HasValidBearerToken(std::string_view request, std::string_view expectedToken);
bool HasValidWebSocketProtocolToken(std::string_view request, std::string_view expectedToken);
bool HasSafeSameOrigin(std::string_view request);

}
