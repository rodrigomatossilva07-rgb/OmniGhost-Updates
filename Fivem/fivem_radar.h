#pragma once
#include "game_setup.h"

namespace Fivem_Radar {
    void Update();
    void Shutdown();
    void EnsureRunning(int port, bool lan);
    bool IsRunning();
    bool IsStarting();
    int  Port();
    const char* Status();
    const char* LanUrl();
    void StartCloudflareTunnel(int local_port);
    void StopCloudflareTunnel();
    bool CloudflareRunning();
    const char* PublicUrl(); // https://xxx.trycloudflare.com or empty
}