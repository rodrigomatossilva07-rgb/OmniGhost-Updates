#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "platform/offset_auto.h"
#include "platform/expected.h"
#include "platform/logging.h"

#include <chrono>
#include <thread>

using namespace OmniGhost::Platform;

TEST_CASE("OffsetAuto compatibility state management", "[offset_auto]") {
    // Test initial state
    auto snapshot = OffsetAuto::Compatibility(ActiveGame::CS2);
    REQUIRE(snapshot.state == OffsetAuto::CompatibilityState::Unknown);
    
    // Test state transitions
    OffsetAuto::MarkChecking(ActiveGame::CS2);
    snapshot = OffsetAuto::Compatibility(ActiveGame::CS2);
    REQUIRE(snapshot.state == OffsetAuto::CompatibilityState::Checking);
    
    OffsetAuto::MarkLiveValid(ActiveGame::CS2, "Test validation");
    snapshot = OffsetAuto::Compatibility(ActiveGame::CS2);
    REQUIRE(snapshot.state == OffsetAuto::CompatibilityState::Ready);
    REQUIRE(snapshot.detail == "Test validation");
    
    OffsetAuto::MarkOutdated(ActiveGame::CS2, "Build mismatch");
    snapshot = OffsetAuto::Compatibility(ActiveGame::CS2);
    REQUIRE(snapshot.state == OffsetAuto::CompatibilityState::Outdated);
    
    OffsetAuto::MarkUnsupported(ActiveGame::CS2, "Unsupported build");
    snapshot = OffsetAuto::Compatibility(ActiveGame::CS2);
    REQUIRE(snapshot.state == OffsetAuto::CompatibilityState::Unsupported);
}

TEST_CASE("OffsetAuto status tracking", "[offset_auto]") {
    // Test g_status proxy
    OffsetAuto::g_status = "Test status";
    REQUIRE(std::string(OffsetAuto::g_status) == "Test status");
    
    OffsetAuto::g_status = "Another status";
    REQUIRE(std::string(OffsetAuto::g_status) == "Another status");
}

TEST_CASE("OffsetAuto busy flag", "[offset_auto]") {
    REQUIRE_FALSE(OffsetAuto::g_busy.load());
    
    OffsetAuto::g_busy.store(true);
    REQUIRE(OffsetAuto::g_busy.load());
    
    OffsetAuto::g_busy.store(false);
    REQUIRE_FALSE(OffsetAuto::g_busy.load());
}

TEST_CASE("OffsetAuto compatibility helpers", "[offset_auto]") {
    // Test SupportsAutomaticRefresh
    bool supports = OffsetAuto::SupportsAutomaticRefresh(ActiveGame::CS2);
    // Should not crash, result depends on implementation
    
    // Test BlocksLaunch
    bool blocks = OffsetAuto::BlocksLaunch(ActiveGame::CS2);
    // Should not crash
    
    // Test HasCriticalOffsets for each game
    for (int i = 0; i <= static_cast<int>(ActiveGame::Apex); ++i) {
        ActiveGame game = static_cast<ActiveGame>(i);
        bool hasCritical = OffsetAuto::HasCriticalOffsets(game);
        // Should not crash
    }
}

TEST_CASE("OffsetAuto RefreshAllSupported", "[offset_auto]") {
    // Test RefreshAllSupported
    int result = OffsetAuto::RefreshAllSupported(false);
    // Should return a count (>= 0)
    REQUIRE(result >= 0);
    
    int resultForced = OffsetAuto::RefreshAllSupported(true);
    REQUIRE(resultForced >= 0);
}

TEST_CASE("Expected helpers", "[expected]") {
    using namespace OmniGhost;
    
    // Test Success
    auto success = Success(42);
    REQUIRE(success.has_value());
    REQUIRE(success.value() == 42);
    REQUIRE(IsSuccess(success));
    REQUIRE_FALSE(IsFailure(success));
    
    // Test Failure
    auto failure = Failure<int>(ErrorCode::NotFound, "Not found");
    REQUIRE_FALSE(failure.has_value());
    REQUIRE(IsFailure(failure));
    REQUIRE(failure.error().code == ErrorCode::NotFound);
    REQUIRE(failure.error().message == "Not found");
    
    // Test VoidExpected
    auto voidSuccess = Success();
    REQUIRE(voidSuccess.has_value());
    
    auto voidFailure = Failure<void>(ErrorCode::InternalError, "Error");
    REQUIRE_FALSE(voidFailure.has_value());
    
    // Test Map
    auto mapped = Map(Success(10), [](int x) { return x * 2; });
    REQUIRE(mapped.has_value());
    REQUIRE(mapped.value() == 20);
    
    auto mappedFailure = Map(Failure<int>(ErrorCode::NotFound, "Not found"), 
                             [](int x) { return x * 2; });
    REQUIRE_FALSE(mappedFailure.has_value());
    
    // Test AndThen
    auto chained = AndThen(Success(5), [](int x) -> Expected<int> {
        return Success(x * 3);
    });
    REQUIRE(chained.has_value());
    REQUIRE(chained.value() == 15);
    
    // Test OrElse
    auto recovered = OrElse(Failure<int>(ErrorCode::NotFound, "Not found"),
                           [](const Error&) -> Expected<int> {
                               return Success(99);
                           });
    REQUIRE(recovered.has_value());
    REQUIRE(recovered.value() == 99);
}

TEST_CASE("Expected error codes", "[expected]") {
    using namespace OmniGhost;
    
    REQUIRE(ErrorCode::InvalidArgument == "INVALID_ARGUMENT");
    REQUIRE(ErrorCode::NotFound == "NOT_FOUND");
    REQUIRE(ErrorCode::AlreadyExists == "ALREADY_EXISTS");
    REQUIRE(ErrorCode::PermissionDenied == "PERMISSION_DENIED");
    REQUIRE(ErrorCode::IOError == "IO_ERROR");
    REQUIRE(ErrorCode::ParseError == "PARSE_ERROR");
    REQUIRE(ErrorCode::ValidationError == "VALIDATION_ERROR");
    REQUIRE(ErrorCode::OffsetsMissing == "OFFSETS_MISSING");
    REQUIRE(ErrorCode::OffsetsOutdated == "OFFSETS_OUTDATED");
    REQUIRE(ErrorCode::DmaError == "DMA_ERROR");
    REQUIRE(ErrorCode::DmaDeviceNotFound == "DMA_DEVICE_NOT_FOUND");
    REQUIRE(ErrorCode::DmaProcessNotFound == "DMA_PROCESS_NOT_FOUND");
    REQUIRE(ErrorCode::MemoryReadError == "MEMORY_READ_ERROR");
    REQUIRE(ErrorCode::ModuleNotFound == "MODULE_NOT_FOUND");
    REQUIRE(ErrorCode::SecurityError == "SECURITY_ERROR");
    REQUIRE(ErrorCode::LicenseError == "LICENSE_ERROR");
    REQUIRE(ErrorCode::NetworkError == "NETWORK_ERROR");
    REQUIRE(ErrorCode::Timeout == "TIMEOUT");
    REQUIRE(ErrorCode::Cancelled == "CANCELLED");
    REQUIRE(ErrorCode::InternalError == "INTERNAL_ERROR");
    REQUIRE(ErrorCode::UnsupportedOperation == "UNSUPPORTED_OPERATION");
}

} // namespace