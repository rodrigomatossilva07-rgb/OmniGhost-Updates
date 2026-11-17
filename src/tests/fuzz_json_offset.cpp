#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "platform/offset_source.h"
#include "platform/expected.h"
#include "platform/logging.h"

// Fuzzer for external JSON offset parsing
// This is a standalone fuzzer target that can be built with -fsanitize=fuzzer

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    
    try {
        std::string jsonStr(reinterpret_cast<const char*>(data), size);
        
        // Try to parse as JSON
        nlohmann::json json = nlohmann::json::parse(jsonStr);
        
        // Try to parse as offset JSON for each game
        for (int g = 0; g <= static_cast<int>(OmniGhost::OffsetSource::GameId::Apex); ++g) {
            OmniGhost::OffsetSource::GameId game = static_cast<OmniGhost::OffsetSource::GameId>(g);
            
            // This should handle malformed JSON gracefully
            auto result = OmniGhost::OffsetSource::ParseExternalJsonToSnapshot(jsonStr, game);
        }
    } catch (...) {
        // Ignore exceptions - fuzzer should not crash
    }
    
    return 0;
}