#pragma once
#include "http_policy.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace OmniGhost::Update {
struct HttpResult {
    int statusCode{};
    std::string body;
    std::string error;
    int retryAfterSeconds{};
    HttpPolicy::ErrorClass errorClass{HttpPolicy::ErrorClass::None};
};
using ProgressCallback = std::function<void(std::uint64_t, std::uint64_t, double)>;

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResult GetText(const std::string& url, int timeoutMilliseconds, std::atomic_bool& cancelled) = 0;
    virtual HttpResult Download(const std::string& url, const std::filesystem::path& partialFile,
        std::uint64_t expectedSize, std::uint64_t maximumSize, int timeoutMilliseconds,
        std::atomic_bool& cancelled, const ProgressCallback& progress) = 0;
};

class WinHttpClient final : public IHttpClient {
public:
    HttpResult GetText(const std::string& url, int timeoutMilliseconds, std::atomic_bool& cancelled) override;
    HttpResult Download(const std::string& url, const std::filesystem::path& partialFile,
        std::uint64_t expectedSize, std::uint64_t maximumSize, int timeoutMilliseconds,
        std::atomic_bool& cancelled, const ProgressCallback& progress) override;
};
}
