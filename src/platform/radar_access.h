#pragma once

#include <string>
#include <string_view>

namespace OmniGhost::RadarAccess {

// Creates a 256-bit session token using the Windows system RNG. The token is
// never persisted: stopping the radar invalidates every previously shared URL.
bool GenerateSessionToken(std::string& token, std::string& error);

// These HTTP helpers do not depend on Winsock and are covered by unit tests.
std::string HeaderValue(std::string_view request, std::string_view name);
bool ConstantTimeEquals(std::string_view left, std::string_view right) noexcept;
bool HasValidBearerToken(std::string_view request, std::string_view expectedToken);
bool HasValidWebSocketProtocolToken(std::string_view request, std::string_view expectedToken);
bool HasSafeSameOrigin(std::string_view request);

} // namespace OmniGhost::RadarAccess
