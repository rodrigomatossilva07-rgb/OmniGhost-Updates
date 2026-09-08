#pragma once
#include "game/game_setup.h"
#include "fivem_radar_config.h"

namespace Fivem_Radar {
    void Update();
    void Shutdown();
    void EnsureRunning(int port, bool lan);
    bool IsRunning();
    bool IsStarting();
    int  Port();
    const char* Status();
    const char* LanUrl();
    const char* LocalUrl(); // http://127.0.0.1:port/#token
    void StartCloudflareTunnel(int local_port);
    void StopCloudflareTunnel();
    bool CloudflareRunning();
    const char* PublicUrl(); // https://xxx.trycloudflare.com or empty
}