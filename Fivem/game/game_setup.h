#pragma once

namespace FiveM {
    // Core setup function
    void Setup();
    // Re-bind process + re-read world/local (city change)
    bool ReinitDma();

    // Function to get current build version (useful for UI display)
    int GetCurrentBuildVersion();
}