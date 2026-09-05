#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace OmniGhost {
namespace Update {
namespace HttpPolicy {

enum class ErrorClass {
    None,
    Cancelled,
    Timeout,
    Dns,
    Connection,
    Tls,
    Http,
    Redirect,
    SizeLimit,
    Io,
    InvalidUrl,
    Unknown
};

constexpr int DefaultTimeoutMilliseconds = 15000;
constexpr int MinimumTimeoutMilliseconds = 3000;
constexpr int MaximumTimeoutMilliseconds = 120000;
constexpr std::uint64_t MaximumManifestBytes = 1024ull * 1024ull;
constexpr unsigned MaximumRetries = 2;

int ClampTimeoutMilliseconds(int requested) noexcept;
bool IsTlsOnlyUrl(std::string_view url) noexcept;
bool IsRetryableStatus(int statusCode) noexcept;
unsigned RetryDelayMilliseconds(unsigned attempt, int retryAfterSeconds = 0) noexcept;
std::string StatusMessage(int statusCode, std::string_view finalUrl = {});
ErrorClass ClassifyFailure(int statusCode, std::string_view error) noexcept;
const char* ErrorClassName(ErrorClass value) noexcept;

} // namespace HttpPolicy
} // namespace Update
} // namespace OmniGhost
