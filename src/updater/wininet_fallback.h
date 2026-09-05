#pragma once

#include "http_client.h"

namespace OmniGhost::Update {

HttpResult GetTextWithWinInet(
    const std::string& url,
    int timeoutMilliseconds,
    std::atomic_bool& cancelled);

HttpResult DownloadWithWinInet(
    const std::string& url,
    const std::filesystem::path& partialFile,
    std::uint64_t expectedSize,
    std::uint64_t maximumSize,
    int timeoutMilliseconds,
    std::atomic_bool& cancelled,
    const ProgressCallback& progress);

} // namespace OmniGhost::Update
