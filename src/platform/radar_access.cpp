#include "radar_access.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>

#pragma comment(lib, "bcrypt.lib")

namespace OmniGhost::RadarAccess {
namespace {

bool EqualsAsciiInsensitive(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

std::string_view Trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

} // namespace

bool GenerateSessionToken(std::string& token, std::string& error)
{
    std::array<unsigned char, 32> random{};
    const NTSTATUS status = BCryptGenRandom(
        nullptr, random.data(), static_cast<ULONG>(random.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        token.clear();
        error = "O Windows não conseguiu gerar o token seguro do radar.";
        return false;
    }

    static constexpr char hexadecimal[] = "0123456789abcdef";
    token.resize(random.size() * 2);
    for (std::size_t index = 0; index < random.size(); ++index) {
        token[index * 2] = hexadecimal[random[index] >> 4];
        token[index * 2 + 1] = hexadecimal[random[index] & 0x0f];
    }
    error.clear();
    SecureZeroMemory(random.data(), random.size());
    return true;
}

std::string HeaderValue(std::string_view request, std::string_view name)
{
    std::size_t position = request.find("\r\n");
    if (position == std::string_view::npos)
        return {};
    position += 2;

    while (position < request.size()) {
        const std::size_t end = request.find("\r\n", position);
        if (end == std::string_view::npos || end == position)
            break;
        const std::string_view line = request.substr(position, end - position);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos &&
            EqualsAsciiInsensitive(Trim(line.substr(0, colon)), name)) {
            return std::string(Trim(line.substr(colon + 1)));
        }
        position = end + 2;
    }
    return {};
}

bool ConstantTimeEquals(std::string_view left, std::string_view right) noexcept
{
    const std::size_t maximum = (std::max)(left.size(), right.size());
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t index = 0; index < maximum; ++index) {
        const unsigned char a = index < left.size()
            ? static_cast<unsigned char>(left[index]) : 0;
        const unsigned char b = index < right.size()
            ? static_cast<unsigned char>(right[index]) : 0;
        difference |= static_cast<std::size_t>(a ^ b);
    }
    return difference == 0;
}

bool HasValidBearerToken(std::string_view request, std::string_view expectedToken)
{
    if (expectedToken.empty())
        return false;
    const std::string authorization = HeaderValue(request, "Authorization");
    constexpr std::string_view prefix = "Bearer ";
    if (authorization.size() <= prefix.size() ||
        !EqualsAsciiInsensitive(std::string_view(authorization).substr(0, prefix.size()), prefix))
        return false;
    return ConstantTimeEquals(
        std::string_view(authorization).substr(prefix.size()), expectedToken);
}

bool HasValidWebSocketProtocolToken(std::string_view request, std::string_view expectedToken)
{
    if (expectedToken.empty())
        return false;
    const std::string protocols = HeaderValue(request, "Sec-WebSocket-Protocol");
    constexpr std::string_view prefix = "omnighost-radar.";
    std::size_t start = 0;
    while (start < protocols.size()) {
        const std::size_t comma = protocols.find(',', start);
        const std::string_view item = Trim(std::string_view(protocols).substr(
            start, comma == std::string::npos ? std::string::npos : comma - start));
        if (item.size() == prefix.size() + expectedToken.size() &&
            item.substr(0, prefix.size()) == prefix &&
            ConstantTimeEquals(item.substr(prefix.size()), expectedToken))
            return true;
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return false;
}

bool HasSafeSameOrigin(std::string_view request)
{
    const std::string origin = HeaderValue(request, "Origin");
    if (origin.empty())
        return true; // Non-browser clients normally omit Origin.
    const std::string host = HeaderValue(request, "Host");
    if (host.empty())
        return false;
    return ConstantTimeEquals(origin, "http://" + host) ||
           ConstantTimeEquals(origin, "https://" + host);
}

} // namespace OmniGhost::RadarAccess
