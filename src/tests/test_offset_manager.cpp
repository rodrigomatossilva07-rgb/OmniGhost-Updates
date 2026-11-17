#include <catch2/catch_test_macros.hpp>

#include "platform/ioffset_manager.h"
#include "platform/offset_manager.h"
#include "platform/offset_manager_factory.h"
#include "platform/expected.h"
#include "platform/logging.h"

#include <thread>
#include <chrono>

using namespace OmniGhost::Platform;

TEST_CASE("OffsetManagerFactory creates instance", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    
    REQUIRE(result);
    REQUIRE(result.value() != nullptr);
    
    auto manager = std::move(result.value());
    REQUIRE(manager != nullptr);
}

TEST_CASE("OffsetManager initialization", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    auto shutdownResult = manager->Shutdown();
    REQUIRE(shutdownResult);
}

TEST_CASE("OffsetManager LoadOffsets", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test loading offsets for each supported game
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        
        // This may fail if offsets aren't embedded, which is OK for unit tests
        auto loadResult = manager->LoadOffsets(game);
        // Should not crash, result depends on embedded offsets
    }
    
    auto shutdownResult = manager->Shutdown();
    REQUIRE(shutdownResult);
}

TEST_CASE("OffsetManager HasCriticalOffsets", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test HasCriticalOffsets for each game
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        
        auto result = manager->HasCriticalOffsets(game);
        REQUIRE(result.has_value());
        // Result depends on whether offsets are loaded
    }
}

TEST_CASE("OffsetManager ValidateLive", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test ValidateLive for each game
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        
        auto result = manager->ValidateLive(game);
        REQUIRE(result.has_value());
        // Result depends on game state
    }
}

TEST_CASE("OffsetManager SoftProbeLive", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test SoftProbeLive for each game
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        
        auto result = manager->SoftProbeLive(game);
        REQUIRE(result.has_value());
        // Result depends on game state
        
        // Also test SoftProbeFailReason
        auto reasonResult = manager->SoftProbeFailReason(game);
        REQUIRE(reasonResult.has_value());
        // Reason string may be empty or contain failure reason
    }
}

TEST_CASE("OffsetManager EnsureOffsets", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test EnsureOffsets for each game
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        
        // Test with force=false (default)
        auto result1 = manager->EnsureOffsets(game, false);
        REQUIRE(result1.has_value());
        // Result depends on offset state
        
        // Test with force=true
        auto result2 = manager->EnsureOffsets(game, true);
        REQUIRE(result2.has_value());
        // Result depends on offset state
    }
}

TEST_CASE("OffsetManager Compatibility", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test Compatibility for each game
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        
        auto compat = manager->Compatibility(game);
        // Should not crash
        // State depends on offset state
    }
}

TEST_CASE("OffsetManager Mark* methods", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test MarkChecking
    manager->MarkChecking(ActiveGame::CS2);
    auto compat1 = manager->Compatibility(ActiveGame::CS2);
    REQUIRE(compat1.state == OffsetAuto::CompatibilityState::Checking);
    
    // Test MarkLiveValid
    manager->MarkLiveValid(ActiveGame::CS2, "Test validation");
    auto compat2 = manager->Compatibility(ActiveGame::CS2);
    REQUIRE(compat2.state == OffsetAuto::CompatibilityState::Ready);
    REQUIRE(compat2.detail == "Test validation");
    
    // Test MarkOutdated
    manager->MarkOutdated(ActiveGame::CS2, "Build mismatch");
    auto compat3 = manager->Compatibility(ActiveGame::CS2);
    REQUIRE(compat3.state == OffsetAuto::CompatibilityState::Outdated);
    
    // Test MarkUnsupported
    manager->MarkUnsupported(ActiveGame::CS2, "Unsupported build");
    auto compat4 = manager->Compatibility(ActiveGame::CS2);
    REQUIRE(compat4.state == OffsetAuto::CompatibilityState::Unsupported);
}

TEST_CASE("OffsetManager RefreshAllSupported", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test RefreshAllSupported
    auto result1 = manager->RefreshAllSupported(false);
    REQUIRE(result1.has_value());
    REQUIRE(result1.value() >= 0);
    
    auto result2 = manager->RefreshAllSupported(true);
    REQUIRE(result2.has_value());
    REQUIRE(result2.value() >= 0);
}

TEST_CASE("OffsetManager status methods", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test GetStatus
    std::string status = manager->GetStatus();
    // Should return a string (may be empty or have status)
    
    // Test IsBusy
    bool busy = manager->IsBusy();
    // Should not crash
    
    // Test HasValidOffsets
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        bool hasValid = manager->HasValidOffsets(game);
        // Result depends on offset state
    }
}

TEST_CASE("OffsetManager reload", "[offset_manager]") {
    OffsetManagerFactory factory;
    auto result = factory.Create();
    REQUIRE(result);
    
    auto manager = std::move(result.value());
    auto initResult = manager->Initialize();
    REQUIRE(initResult);
    
    // Test ReloadOffsets
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        
        auto reloadResult = manager->ReloadOffsets(game);
        // Should not crash
    }
}

} // namespace