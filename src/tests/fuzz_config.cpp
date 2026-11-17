#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "config/config_manager.h"
#include "platform/expected.h"
#include "platform/logging.h"

// Fuzzer for config JSON validation
// This is a standalone fuzzer target that can be built with -fsanitize=fuzzer

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    
    try {
        std::string jsonStr(reinterpret_cast<const char*>(data), size);
        
        // Try to parse as JSON
        nlohmann::json json = nlohmann::json::parse(jsonStr);
        
        // Try to deserialize as global config
        OmniGhost::Config::ConfigManagerImpl manager;
        manager.Initialize();
        manager.DeserializeAll(jsonStr);
    } catch (...) {
        // Ignore exceptions
    }
    
    return 0;
}