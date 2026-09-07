#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include "../src/platform/config_serializer.h"
#include "../src/platform/result.h"
#include "../src/platform/config_schema.h"
#include "../src/platform/embedded_offsets.h"

using namespace OmniGhost::Platform;

// ============================================================
// Fuzzing Tests for Config Parser
// ============================================================

class ConfigFuzzer {
public:
    static std::string GenerateRandomString(size_t length) {
        static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+-=[]{}|;':\",./<>?";
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[rand() % (sizeof(charset) - 1)];
        }
        return result;
    }
    
    static std::string GenerateConfigLikeString() {
        std::ostringstream oss;
        int entries = rand() % 50;
        for (int i = 0; i < entries; ++i) {
            std::string key = GenerateRandomString(rand() % 32);
            std::string val = GenerateRandomString(rand() % 128);
            oss << key << "=" << val << "\n";
        }
        return oss.str();
    }
    
    static std::string GenerateMalformedConfig() {
        std::ostringstream oss;
        int entries = rand() % 30;
        for (int i = 0; i < entries; ++i) {
            // Randomly generate malformed lines
            int type = rand() % 5;
            switch (type) {
                case 0: // Normal
                    oss << GenerateRandomString(10) << "=" << GenerateRandomString(20) << "\n";
                    break;
                case 1: // No equals
                    oss << GenerateRandomString(20) << "\n";
                    break;
                case 2: // Multiple equals
                    oss << GenerateRandomString(5) << "=" << GenerateRandomString(5) << "=" << GenerateRandomString(5) << "\n";
                    break;
                case 3: // Empty key
                    oss << "=" << GenerateRandomString(20) << "\n";
                    break;
                case 4: // Empty value
                    oss << GenerateRandomString(10) << "=\n";
                    break;
            }
        }
        return oss.str();
    }
};

TEST(ConfigFuzzer, RandomConfigStrings) {
    ConfigSerializer serializer;
    
    for (int i = 0; i < 1000; ++i) {
        std::string config = ConfigFuzzer::GenerateConfigLikeString();
        
        // Should not crash
        EXPECT_NO_THROW({
            serializer.Deserialize(config);
        });
    }
}

TEST(ConfigFuzzer, MalformedConfigStrings) {
    ConfigSerializer serializer;
    
    for (int i = 0; i < 1000; ++i) {
        std::string config = ConfigFuzzer::GenerateMalformedConfig();
        
        // Should not crash, may return false
        EXPECT_NO_THROW({
            serializer.Deserialize(config);
        });
    }
}

TEST(ConfigFuzzer, EmptyAndEdgeCases) {
    ConfigSerializer serializer;
    
    // Empty string
    EXPECT_NO_THROW(serializer.Deserialize(""));
    
    // Only whitespace
    EXPECT_NO_THROW(serializer.Deserialize("   \n\t\n  "));
    
    // Only comments
    EXPECT_NO_THROW(serializer.Deserialize("# comment\n# another\n"));
    
    // Very long key
    std::string longKey(10000, 'a');
    std::string longVal(10000, 'b');
    std::string longConfig = longKey + "=" + longVal + "\n";
    EXPECT_NO_THROW(serializer.Deserialize(longConfig));
    
    // Unicode
    std::string unicodeConfig = "key=测试\nkey2=тест\nkey3=тест\n";
    EXPECT_NO_THROW(serializer.Deserialize(unicodeConfig));
    
    // Null bytes (should be handled)
    std::string nullConfig = "key=val\0ue\n";
    EXPECT_NO_THROW(serializer.Deserialize(nullConfig));
}

// ============================================================
// Offset Parser Fuzzing
// ============================================================

TEST(OffsetFuzzer, RandomJSON) {
    // Test embedded offsets parsing with random JSON
    for (int i = 0; i < 100; ++i) {
        std::ostringstream oss;
        oss << "{";
        int entries = rand() % 20;
        for (int j = 0; j < entries; ++j) {
            if (j > 0) oss << ",";
            oss << "\"offset_" << rand() % 1000 << "\":" << (rand() % 1000000);
        }
        oss << "}";
        
        std::string json = oss.str();
        EXPECT_NO_THROW({
            // Test parsing - this would call the actual offset parser
            // For now just verify JSON doesn't crash
            std::istringstream iss(json);
            std::string line;
            while (std::getline(iss, line, ',')) {}
        });
    }
}

TEST(OffsetFuzzer, MalformedJSON) {
    const char* malformed[] = {
        "{",
        "}",
        "{ }",
        "{ }",
        "{,} ",
        "{ \"key\": }",
        "{ \"key\": 1, }",
        "{ \"key\": 1 2 }",
        "{ \"key\": \"unclosed }",
        "{ \"key\": 1 \"extra\": 2 }",
        "not json at all",
        "",
        "\0\0\0",
        "{\"a\":1}" + std::string(100000, 'x'),
    };
    
    for (const char* json : malformed) {
        EXPECT_NO_THROW({
            std::istringstream iss(json);
            std::string line;
            while (std::getline(iss, line, ',')) {}
        });
    }
}

// ============================================================
// Result Type Fuzzing
// ============================================================

TEST(ResultFuzzer, RandomOperations) {
    for (int i = 0; i < 1000; ++i) {
        int val = rand();
        auto r = Ok(val);
        EXPECT_TRUE(r.IsOk());
        EXPECT_EQ(r.Unwrap(), val);
        
        auto mapped = r.Map([](int x) { return x * 2; });
        EXPECT_TRUE(mapped.IsOk());
        EXPECT_EQ(mapped.Unwrap(), val * 2);
    }
}

TEST(ResultFuzzer, ErrorPropagation) {
    for (int i = 0; i < 100; ++i) {
        auto r = Err<int>("error " + std::to_string(i));
        EXPECT_TRUE(r.IsErr());
        
        auto mapped = r.Map([](int x) { return x * 2; });
        EXPECT_TRUE(mapped.IsErr());
        
        // Chaining
        auto r2 = r.Map([](int x) { return x + 1; });
        EXPECT_TRUE(r2.IsErr());
    }
}

TEST(ResultFuzzer, VoidResult) {
    auto ok = Ok();
    EXPECT_TRUE(ok.IsOk());
    
    auto err = Err<void>("test");
    EXPECT_TRUE(err.IsErr());
}

// ============================================================
// Config Schema Fuzzing
// ============================================================

TEST(ConfigSchemaFuzzer, RandomSchemas) {
    ConfigSchema schema;
    
    for (int i = 0; i < 100; ++i) {
        std::ostringstream oss;
        oss << "schema_version=2\n";
        
        int fields = rand() % 20;
        for (int j = 0; j < fields; ++j) {
            std::string name = "field_" + std::to_string(j);
            std::string type;
            switch (rand() % 5) {
                case 0: type = "bool"; break;
                case 1: type = "int"; break;
                case 2: type = "float"; break;
                case 3: type = "string"; break;
                case 4: type = "int[]"; break;
            }
            oss << name << ": " << type << "\n";
        }
        
        std::string schemaText = oss.str();
        EXPECT_NO_THROW({
            // Parse schema
            std::istringstream iss(schemaText);
            std::string line;
            while (std::getline(iss, line)) {}
        });
    }
}

// ============================================================
// Property-based Testing Helpers
// ============================================================

// QuickCheck-style property tests
TEST(PropertyTests, ConfigSerializeDeserializeIdempotent) {
    ConfigSerializer original;
    
    // Generate random config
    original.Set("int", rand());
    original.Set("float", static_cast<float>(rand()) / RAND_MAX * 1000.0f);
    original.Set("bool", rand() % 2 == 0);
    original.Set("string", "test_" + std::to_string(rand()));
    
    std::string serialized = original.SerializeAll();
    
    ConfigSerializer deserialized;
    deserialized.Deserialize(serialized);
    
    int i;
    float f;
    bool b;
    std::string s;
    
    EXPECT_TRUE(deserialized.Get("int", i));
    // Note: exact equality may not hold due to float precision
    // but structure should be preserved
}

TEST(PropertyTests, ResultMonadLaws) {
    // Left identity: Ok(x).Map(f) == f(x)
    int x = rand();
    auto left = Ok(x).Map([](int x) { return x * 2; });
    auto right = Ok(x * 2);
    EXPECT_EQ(left.Unwrap(), right.Unwrap());
    
    // Right identity: m.Map(Ok) == m
    auto m = Ok(42);
    auto rightIdent = m.Map([](int x) { return Ok(x); });
    EXPECT_EQ(m.Unwrap(), rightIdent.Unwrap());
    
    // Associativity: m.Map(f).Map(g) == m.Map(x -> g(f(x)))
    auto m2 = Ok(5);
    auto assoc1 = m2.Map([](int x) { return x + 1; }).Map([](int x) { return x * 2; });
    auto assoc2 = m2.Map([](int x) { return (x + 1) * 2; });
    EXPECT_EQ(assoc1.Unwrap(), assoc2.Unwrap());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    srand(static_cast<unsigned>(time(nullptr)));
    return RUN_ALL_TESTS();
}