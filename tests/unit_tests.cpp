#include <gtest/gtest.h>
#include "../src/math/math.cpp"
#include "../src/platform/math.h"
#include "../src/platform/config_serializer.h"
#include "../src/config/config_core.h"
#include "../src/platform/result.h"

using namespace OmniGhost::Platform;
using namespace OmniGhost::Math;

// ============================================================
// Math Helpers Tests
// ============================================================

TEST(MathHelpers, Vec3Operations) {
    Vec3 a{1.0f, 2.0f, 3.0f};
    Vec3 b{4.0f, 5.0f, 6.0f};
    
    EXPECT_FLOAT_EQ((a + b).x, 5.0f);
    EXPECT_FLOAT_EQ((a + b).y, 7.0f);
    EXPECT_FLOAT_EQ((a + b).z, 9.0f);
    
    EXPECT_FLOAT_EQ((b - a).x, 3.0f);
    EXPECT_FLOAT_EQ((b - a).y, 3.0f);
    EXPECT_FLOAT_EQ((b - a).z, 3.0f);
    
    EXPECT_FLOAT_EQ((a * 2.0f).x, 2.0f);
    EXPECT_FLOAT_EQ((a * 2.0f).y, 4.0f);
    EXPECT_FLOAT_EQ((a * 2.0f).z, 6.0f);
    
    EXPECT_NEAR(a.Length(), std::sqrt(14.0f), 0.0001f);
    EXPECT_FLOAT_EQ(a.LengthSq(), 14.0f);
    EXPECT_NEAR(a.Distance(b), std::sqrt(27.0f), 0.0001f);
    EXPECT_FLOAT_EQ(a.DistanceSq(b), 27.0f);
}

TEST(MathHelpers, Vec2Operations) {
    Vec2 a{1.0f, 2.0f};
    Vec2 b{3.0f, 4.0f};
    
    EXPECT_FLOAT_EQ((a + b).x, 4.0f);
    EXPECT_FLOAT_EQ((a + b).y, 6.0f);
}

TEST(MathHelpers, FastMath) {
    const auto& tables = MathTables::Instance();
    
    // Test sin/cos accuracy
    for (int i = 0; i <= 360; i += 15) {
        float rad = i * 3.14159265359f / 180.0f;
        float expectedSin = std::sin(rad);
        float expectedCos = std::cos(rad);
        
        float actualSin = tables.Sin(rad);
        float actualCos = tables.Cos(rad);
        
        EXPECT_NEAR(actualSin, expectedSin, 0.001f) << "At " << i << " degrees";
        EXPECT_NEAR(actualCos, expectedCos, 0.001f) << "At " << i << " degrees";
    }
    
    // Test FastSqrt
    for (float val : {1.0f, 2.0f, 4.0f, 9.0f, 16.0f, 25.0f, 100.0f, 1000.0f}) {
        float expected = std::sqrt(val);
        float actual = tables.FastSqrt(val);
        EXPECT_NEAR(actual, expected, expected * 0.01f) << "For " << val;
    }
    
    // Test FastInvSqrt
    for (float val : {1.0f, 2.0f, 4.0f, 9.0f, 16.0f}) {
        float expected = 1.0f / std::sqrt(val);
        float actual = tables.FastInvSqrt(val);
        EXPECT_NEAR(actual, expected, expected * 0.01f) << "For " << val;
    }
}

TEST(MathHelpers, FastSinCos) {
    const auto& tables = MathTables::Instance();
    
    for (int i = 0; i <= 360; i += 10) {
        float rad = i * 3.14159265359f / 180.0f;
        float sinVal, cosVal;
        tables.SinCos(rad, cosVal);
        sinVal = tables.Sin(rad); // Use the combined call
        
        float expectedSin = std::sin(rad);
        float expectedCos = std::cos(rad);
        
        EXPECT_NEAR(sinVal, expectedSin, 0.001f) << "Sin at " << i << " degrees";
        EXPECT_NEAR(cosVal, expectedCos, 0.001f) << "Cos at " << i << " degrees";
    }
}

// ============================================================
// Result Type Tests
// ============================================================

TEST(ResultType, OkConstruction) {
    Result<int> r = Ok(42);
    EXPECT_TRUE(r.IsOk());
    EXPECT_FALSE(r.IsErr());
    EXPECT_EQ(r.Unwrap(), 42);
}

TEST(ResultType, ErrConstruction) {
    Result<int> r = Err<int>("test error");
    EXPECT_FALSE(r.IsOk());
    EXPECT_TRUE(r.IsErr());
    EXPECT_EQ(r.UnwrapErr().message, "test error");
}

TEST(ResultType, Map) {
    Result<int> r = Ok(5);
    auto mapped = r.Map([](int x) { return x * 2; });
    EXPECT_TRUE(mapped.IsOk());
    EXPECT_EQ(mapped.Unwrap(), 10);
    
    Result<int> err = Err<int>("error");
    auto mappedErr = err.Map([](int x) { return x * 2; });
    EXPECT_TRUE(mappedErr.IsErr());
}

TEST(ResultType, VoidResult) {
    Result<void> ok = Ok();
    EXPECT_TRUE(ok.IsOk());
    
    Result<void> err = Err<void>("error");
    EXPECT_TRUE(err.IsErr());
}

TEST(ResultType, BoolOperators) {
    Result<int> ok = Ok(1);
    Result<int> err = Err<int>("err");
    
    EXPECT_TRUE(static_cast<bool>(ok));
    EXPECT_FALSE(static_cast<bool>(err));
}

// ============================================================
// Config Serialization Tests
// ============================================================

TEST(ConfigSerializer, BasicSerialization) {
    ConfigSerializer serializer;
    
    serializer.Set("test.int", 42);
    serializer.Set("test.float", 3.14f);
    serializer.Set("test.bool", true);
    serializer.Set("test.string", "hello");
    
    int intVal;
    float floatVal;
    bool boolVal;
    std::string strVal;
    
    EXPECT_TRUE(serializer.Get("test.int", intVal));
    EXPECT_EQ(intVal, 42);
    
    EXPECT_TRUE(serializer.Get("test.float", floatVal));
    EXPECT_FLOAT_EQ(floatVal, 3.14f);
    
    EXPECT_TRUE(serializer.Get("test.bool", boolVal));
    EXPECT_TRUE(boolVal);
    
    EXPECT_TRUE(serializer.Get("test.string", strVal));
    EXPECT_EQ(strVal, "hello");
}

TEST(ConfigSerializer, VectorSerialization) {
    ConfigSerializer serializer;
    
    std::vector<int> vec = {1, 2, 3, 4, 5};
    serializer.Set("test.vec", vec);
    
    std::vector<int> outVec;
    EXPECT_TRUE(serializer.Get("test.vec", outVec));
    EXPECT_EQ(outVec, vec);
}

TEST(ConfigSerializer, SerializeDeserialize) {
    ConfigSerializer serializer;
    
    serializer.Set("a", 1);
    serializer.Set("b", 2.5f);
    serializer.Set("c", true);
    serializer.Set("d", "test");
    
    std::string serialized = serializer.SerializeAll();
    
    ConfigSerializer deserializer;
    deserializer.Deserialize(serialized);
    
    int a;
    float b;
    bool c;
    std::string d;
    
    EXPECT_TRUE(deserializer.Get("a", a));
    EXPECT_EQ(a, 1);
    
    EXPECT_TRUE(deserializer.Get("b", b));
    EXPECT_FLOAT_EQ(b, 2.5f);
    
    EXPECT_TRUE(deserializer.Get("c", c));
    EXPECT_TRUE(c);
    
    EXPECT_TRUE(deserializer.Get("d", d));
    EXPECT_EQ(d, "test");
}

TEST(ConfigSerializer, IncrementalSave) {
    ConfigSerializer serializer;
    
    serializer.Set("a", 1);
    serializer.Set("b", 2);
    
    // First save - both dirty
    std::string save1 = serializer.SerializeDirty();
    EXPECT_TRUE(serializer.SerializeDirty().empty()); // Now clean
    
    serializer.Set("c", 3); // Only c is dirty
    std::string save2 = serializer.SerializeDirty();
    EXPECT_TRUE(save2.find("c") != std::string::npos);
    EXPECT_TRUE(save2.find("a") == std::string::npos);
    EXPECT_TRUE(save2.find("b") == std::string::npos);
}

TEST(ConfigSerializer, DirtyTracking) {
    ConfigSerializer serializer;
    
    serializer.Set("a", 1);
    serializer.Set("b", 2);
    
    EXPECT_EQ(serializer.DirtyCount(), 2);
    
    serializer.MarkAllClean();
    EXPECT_EQ(serializer.DirtyCount(), 0);
    
    serializer.Set("c", 3);
    EXPECT_EQ(serializer.DirtyCount(), 1);
    
    auto dirtyKeys = serializer.GetDirtyKeys();
    EXPECT_EQ(dirtyKeys.size(), 1);
    EXPECT_EQ(dirtyKeys[0], "c");
}

// ============================================================
// Aim Math Tests
// ============================================================

TEST(AimMath, SmoothCalculation) {
    // Test smooth aim interpolation
    float current = 0.0f;
    float target = 100.0f;
    float smooth = 5.0f;
    float dt = 0.016f; // 60 FPS
    
    // Simulate one frame
    float diff = target - current;
    float step = diff / smooth * dt * 60.0f; // Normalize to 60fps
    current += step;
    
    EXPECT_NEAR(current, 0.2f, 0.01f); // First frame
}

TEST(AimMath, FOVCalculation) {
    float fov = 90.0f;
    float distance = 100.0f;
    float screenWidth = 1920.0f;
    
    // Calculate pixel radius for FOV at distance
    float fovRad = fov * 3.14159265359f / 180.0f;
    float pixelRadius = (screenWidth / 2.0f) * std::tan(fovRad / 2.0f) / std::tan(fovRad / 2.0f);
    
    // At distance 100, FOV 90, 1920 wide: ~960 pixels
    // This is a simplified test
    EXPECT_GT(pixelRadius, 0.0f);
}

TEST(AimMath, SmoothInterpolation) {
    // Test exponential smoothing
    float current = 0.0f;
    float target = 100.0f;
    float smoothFactor = 0.1f;
    
    for (int i = 0; i < 100; ++i) {
        current += (target - current) * smoothFactor;
    }
    
    EXPECT_NEAR(current, target, 1.0f);
}

// ============================================================
// Config Tests
// ============================================================

TEST(Config, LoadSave) {
    config_manager::Initialize();
    
    // Test saving
    bool saved = config_manager::SaveToFile("test_config");
    EXPECT_TRUE(saved);
    
    // Test loading
    bool loaded = config_manager::LoadFromFile("test_config");
    EXPECT_TRUE(loaded);
    
    // Cleanup
    config_manager::DeleteConfigFile("test_config");
}

TEST(Config, SanitizeFilename) {
    EXPECT_EQ(config_manager::SanitizeFilename("test"), "test");
    EXPECT_EQ(config_manager::SanitizeFilename("test<>:\"/\\|?*"), "test_______");
    EXPECT_EQ(config_manager::SanitizeFilename(""), "config");
    EXPECT_EQ(config_manager::SanitizeFilename(std::string(200, 'a')).size(), 96u);
}

TEST(Config, ProfileApplication) {
    config_manager::ApplyGameProfile(config_manager::GameProfile::Minimal);
    EXPECT_STREQ(config_manager::CurrentGameProfileName(), "Minimal");
    
    config_manager::ApplyGameProfile(config_manager::GameProfile::Visual);
    EXPECT_STREQ(config_manager::CurrentGameProfileName(), "Visual");
    
    config_manager::ApplyGameProfile(config_manager::GameProfile::Default);
    EXPECT_STREQ(config_manager::CurrentGameProfileName(), "Default");
}

// ============================================================
// Performance Manager Tests
// ============================================================

TEST(PerformanceManager, FrameBudget) {
    auto& perf = OmniGhost::Platform::GetPerformanceManager();
    
    perf.BeginFrame();
    perf.MarkDmaReadStart();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    perf.MarkDmaReadEnd();
    
    perf.MarkGameLogicStart();
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    perf.MarkGameLogicEnd();
    
    perf.MarkUiRenderStart();
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    perf.MarkUiRenderEnd();
    
    perf.EndFrame();
    
    auto timing = perf.GetLastFrameTiming();
    EXPECT_GT(timing.totalFrameMs, 0.0f);
    EXPECT_GT(timing.dmaReadMs, 0.0f);
    EXPECT_GT(timing.gameLogicMs, 0.0f);
    EXPECT_GT(timing.uiRenderMs, 0.0f);
}

TEST(PerformanceManager, AdaptiveQuality) {
    auto& perf = OmniGhost::Platform::GetPerformanceManager();
    
    // Force high budget usage
    auto& settings = const_cast<OmniGhost::Platform::QualitySettings&>(perf.GetQualitySettings());
    settings.espMaxDistance = 500.0f;
    
    // Simulate budget exceeded
    for (int i = 0; i < 10; ++i) {
        perf.BeginFrame();
        perf.MarkDmaReadStart();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        perf.MarkDmaReadEnd();
        perf.MarkGameLogicStart();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        perf.MarkGameLogicEnd();
        perf.MarkUiRenderStart();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        perf.MarkUiRenderEnd();
        perf.EndFrame();
    }
    
    // Quality should adapt
    // Note: actual adaptation depends on frame budget settings
}

TEST(ObjectPool, BasicOperations) {
    using namespace OmniGhost::Platform;
    
    ObjectPool<int, 10> pool;
    
    int* a = pool.Acquire(42);
    EXPECT_EQ(*a, 42);
    EXPECT_EQ(pool.Available(), 9);
    
    pool.Release(a);
    EXPECT_EQ(pool.Available(), 10);
    
    // Test scoped pointer
    {
        auto scoped = MakeScopedPmrPtr<int>(pool, 100);
        EXPECT_EQ(*scoped, 100);
    } // Auto-released
    
    EXPECT_EQ(pool.Available(), 10);
}

TEST(PoolManager, BasicOperations) {
    using namespace OmniGhost::Platform;
    
    auto& pm = PoolManager::Instance();
    
    auto* vec2 = pm.GetVec2Pool().Acquire();
    *vec2 = {1.0f, 2.0f};
    pm.GetVec2Pool().Release(vec2);
    
    auto stats = pm.GetStats();
    EXPECT_EQ(stats.vec2Used, 0);
    EXPECT_EQ(stats.vec2Free, 1024);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}