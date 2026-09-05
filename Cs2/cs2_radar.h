#pragma once
#include "cs2_game.h"
namespace CS2_Radar {
    void Update(const CS2::Runtime& rt, const CS2::Config& cfg);
    void Shutdown();
    // Force server start from UI (does not wait for RunFrame).
    void EnsureRunning(int port, bool lan);
    bool IsRunning();
    bool IsStarting();
    int  Port();
    const char* Status();
    // The LAN token is stored only in the URL fragment (#token), which browsers
    // do not send in HTTP requests or Referer headers. Frontend requests convert
    // it to an Authorization: Bearer header.
    const char* LanUrl();
    // Authenticated Cloudflare quick tunnel. The signed helper is embedded in
    // OmniGhost and repaired under the private LocalAppData runtime.
    void StartCloudflareTunnel(int local_port);
    void StopCloudflareTunnel();
    bool CloudflareRunning();
    const char* PublicUrl(); // https://xxx.trycloudflare.com or empty
}
