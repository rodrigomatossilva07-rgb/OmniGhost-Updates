#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "platform/offset_source.h"
#include "platform/embedded_offsets.h"
#include "platform/offset_auto.h"

using namespace OmniGhost::EmbeddedOffsets;

// Fuzzer entry point for offset blob parsing
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 56) return 0; // Minimum header size
    
    // Create a ByteView from the fuzzer input
    ByteView blob(data, size);
    
    // Try parsing as each game type
    for (int g = 0; g <= static_cast<int>(Game::Apex); ++g) {
        Game game = static_cast<Game>(g);
        Snapshot snapshot;
        Diagnostics diagnostics;
        
        // This should not crash regardless of input
        ParseEmbeddedOffsetBlob(blob, game, snapshot, diagnostics);
    }
    
    return 0;
}

// Fuzzer for external JSON offset parsing
extern "C" int LLVMFuzzerTestOneInput_JSON(const uint8_t* data, size_t size) {
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

// Fuzzer for config JSON validation
extern "C" int LLVMFuzzerTestOneInput_Config(const uint8_t* data, size_t size) {
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

// Fuzzer for config schema validation
extern "C" int LLVMFuzzerTestOneInput_Schema(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    
    try {
        std::string jsonStr(reinterpret_cast<const char*>(data), size);
        nlohmann::json json = nlohmann::json::parse(jsonStr);
        
        // Try to validate against global config schema
        OmniGhost::Config::ConfigSchemaValidator validator;
        std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "fuzz_schemas";
        std::filesystem::create_directories(tempDir);
        
        validator.Initialize(tempDir);
        validator.Validate(json, OmniGhost::Config::ConfigSchemaValidator::SchemaId::GlobalConfig);
    } catch (...) {
        // Ignore exceptions
    }
    
    return 0;
}