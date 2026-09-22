#pragma once

#include <string>

namespace CS2::WebRadar {

// Start/stop local HTTP server that serves radar_webapp + /api/live JSON.
bool Start(int port);
void Stop();
bool IsRunning() noexcept;
int Port() noexcept;

// Pump accepts + request handling (call from CS2::RunFrame).
void Tick();

// LAN URL e.g. http://192.168.1.10:8080/#token
std::string LocalUrl();
std::string LocalIp();
// Cloudflare public URL when tunnel is up (empty otherwise).
std::string PublicUrl();
std::string AccessToken();

bool StartCloudflare();
void StopCloudflare();
bool CloudflareRunning() noexcept;
std::string CloudflareStatus();

// Build snapshot JSON from CS2::runtime (thread-safe enough for single consumer).
std::string BuildLiveJson();

} // namespace CS2::WebRadar
