#pragma once

#include <cstdint>
#include <string>

namespace FiveM::WebRadar {

bool Start(int port = 8765);
void Stop();
bool IsRunning() noexcept;
int Port() noexcept;

// Publishes one immutable copy of the already-acquired FiveM caches. This never
// performs DMA reads and is intentionally safe to call from the radar worker.
void PublishFromCaches();

std::string LocalUrl();
std::string PublicUrl();
std::string AccessToken();

bool StartCloudflare();
void StopCloudflare();
bool CloudflareRunning() noexcept;
std::string CloudflareStatus();

struct Diagnostics {
    bool running = false;
    bool sourceFresh = false;
    std::uint32_t clients = 0;
    std::uint32_t players = 0;
    std::uint32_t vehicles = 0;
    std::uint64_t lastSampleAgeMs = 0;
};
Diagnostics GetDiagnostics() noexcept;

}
