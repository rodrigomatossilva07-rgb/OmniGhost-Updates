#pragma once

#include <cstdint>
#include <string>

namespace CS2::WebRadar {

bool Start(int port = 8766);
void Stop();
bool IsRunning() noexcept;
int Port() noexcept;

struct Settings {
    std::uint32_t maxPlayers = 64;
    float updateHz = 20.f;
};
void Configure(const Settings& settings) noexcept;



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
    std::uint64_t lastSampleAgeMs = 0;
    float updateHz = 0.f;
    float configuredHz = 20.f;
    std::uint32_t maxPlayers = 64;
};
Diagnostics GetDiagnostics() noexcept;

}
