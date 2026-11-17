#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "config/config_schema_validator.h"
#include "platform/expected.h"
#include "platform/logging.h"

// Fuzzer for config schema validation
// This is a standalone fuzzer target that can be built with -fsanitize=fuzzer

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
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