#include <catch2/catch_test_macros.hpp>

#include "security/isecurity_manager.h"
#include "security/security_manager.h"
#include "security/security_manager_factory.h"
#include "platform/expected.h"
#include "platform/logging.h"

#include <thread>
#include <chrono>

using namespace OmniGhost::Security;

TEST_CASE("SecurityManagerFactory creates instance", "[security]") {
    SecurityManagerFactory factory;
    auto result = factory.Create();
    
    REQUIRE(result);
    REQUIRE(result.value() != nullptr);
    
    // Should return ISecurityManager interface
    auto manager = std::move(result.value());
    REQUIRE(manager != nullptr);
}

TEST_CASE("SecurityManager VerifyHostExecutablePresent", "[security]") {
    SecurityManager& manager = SecurityManager::Instance();
    
    // Test legacy method
    bool legacyResult = manager.VerifyHostExecutablePresentLegacy();
    // Should not crash, result depends on environment
    
    // Test Expected-based method
    auto expectedResult = manager.VerifyHostExecutablePresent();
    // Should not throw
}

TEST_CASE("SecurityManager VerifyStartup", "[security]") {
    SecurityManager& manager = SecurityManager::Instance();
    
    // Test legacy method
    bool legacyResult = manager.VerifyStartupLegacy();
    
    // Test Expected-based method
    auto expectedResult = manager.VerifyStartup();
    // Should not throw
}

TEST_CASE("SecurityManager VerifyProtectedFiles", "[security]") {
    SecurityManager& manager = SecurityManager::Instance();
    
    // Test legacy method
    bool legacyResult = manager.VerifyProtectedFilesLegacy();
    
    // Test Expected-based method
    auto expectedResult = manager.VerifyProtectedFiles();
    // Should not throw
}

TEST_CASE("SecurityManager CanUseProtectedFeatures", "[security]") {
    SecurityManager& manager = SecurityManager::Instance();
    
    // Should not crash
    bool canUse = manager.CanUseProtectedFeatures();
    // Result depends on security state
}

TEST_CASE("SecurityManager Record events", "[security]") {
    SecurityManager& manager = SecurityManager::Instance();
    
    // Record various events - should not crash
    manager.Record(SecurityManager::EventId::InstallManifestInvalid);
    manager.Record(SecurityManager::EventId::RuntimeFileInvalid);
    manager.Record(SecurityManager::EventId::SessionInvalid);
    manager.Record(SecurityManager::EventId::SessionUnavailable);
    manager.Record(SecurityManager::EventId::SessionRestored);
    manager.Record(SecurityManager::EventId::DebuggerObserved);
    manager.Record(SecurityManager::EventId::UnknownModuleObserved);
    manager.Record(SecurityManager::EventId::InstallManifestMissing);
    
    // Check CanUseProtectedFeatures after recording
    bool canUse = SecurityManager::Instance().CanUseProtectedFeatures();
    // Result depends on recorded events
}

TEST_CASE("SecurityManager Classify and Code", "[security]") {
    using EventId = SecurityManager::EventId;
    
    // Test Classify for each event
    auto assessment1 = SecurityManager::Classify(EventId::InstallManifestInvalid);
    REQUIRE(assessment1.severity == Severity::Critical);
    REQUIRE(assessment1.enforcement == Enforcement::DenyFeatures);
    
    auto assessment2 = SecurityManager::Classify(EventId::RuntimeFileInvalid);
    REQUIRE(assessment2.severity == Severity::Critical);
    REQUIRE(assessment2.enforcement == Enforcement::DenyFeatures);
    
    auto assessment3 = SecurityManager::Classify(EventId::SessionInvalid);
    REQUIRE(assessment3.severity == Severity::High);
    REQUIRE(assessment3.enforcement == Enforcement::DenyFeatures);
    
    auto assessment4 = SecurityManager::Classify(EventId::SessionUnavailable);
    REQUIRE(assessment4.severity == Severity::Medium);
    REQUIRE(assessment4.enforcement == Enforcement::Recheck);
    
    auto assessment5 = SecurityManager::Classify(EventId::InstallManifestMissing);
    REQUIRE(assessment5.severity == Severity::Medium);
    REQUIRE(assessment5.enforcement == Enforcement::Recheck);
    
    auto assessment6 = SecurityManager::Classify(EventId::SessionRestored);
    REQUIRE(assessment6.severity == Severity::Info);
    REQUIRE(assessment6.enforcement == Enforcement::None);
    
    auto assessment7 = SecurityManager::Classify(EventId::DebuggerObserved);
    REQUIRE(assessment7.severity == Severity::Low);
    REQUIRE(assessment7.enforcement == Enforcement::Observe);
    
    auto assessment8 = SecurityManager::Classify(EventId::UnknownModuleObserved);
    REQUIRE(assessment8.severity == Severity::Low);
    REQUIRE(assessment8.enforcement == Enforcement::Observe);
    
    // Test Code method
    REQUIRE(SecurityManager::Code(EventId::InstallManifestInvalid) == "SEC-003");
    REQUIRE(SecurityManager::Code(EventId::RuntimeFileInvalid) == "SEC-004");
    REQUIRE(SecurityManager::Code(EventId::SessionInvalid) == "SEC-005");
    REQUIRE(SecurityManager::Code(EventId::SessionUnavailable) == "SEC-006");
    REQUIRE(SecurityManager::Code(EventId::SessionRestored) == "SEC-007");
    REQUIRE(SecurityManager::Code(EventId::DebuggerObserved) == "SEC-008");
    REQUIRE(SecurityManager::Code(EventId::UnknownModuleObserved) == "SEC-009");
    REQUIRE(SecurityManager::Code(EventId::InstallManifestMissing) == "SEC-011");
}

TEST_CASE("SecurityManager enum values", "[security]") {
    // Test Severity enum
    REQUIRE(static_cast<int>(Severity::Info) == 0);
    REQUIRE(static_cast<int>(Severity::Low) == 1);
    REQUIRE(static_cast<int>(Severity::Medium) == 2);
    REQUIRE(static_cast<int>(Severity::High) == 3);
    REQUIRE(static_cast<int>(Severity::Critical) == 4);
    
    // Test Enforcement enum
    REQUIRE(static_cast<int>(Enforcement::None) == 0);
    REQUIRE(static_cast<int>(Enforcement::Observe) == 1);
    REQUIRE(static_cast<int>(Enforcement::Recheck) == 2);
    REQUIRE(static_cast<int>(Enforcement::DenyFeatures) == 3);
    
    // Test EventId enum
    REQUIRE(static_cast<int>(SecurityManager::EventId::InstallManifestInvalid) == 0);
    REQUIRE(static_cast<int>(SecurityManager::EventId::InstallManifestMissing) == 1);
    REQUIRE(static_cast<int>(SecurityManager::EventId::RuntimeFileInvalid) == 2);
    REQUIRE(static_cast<int>(SecurityManager::EventId::SessionInvalid) == 3);
    REQUIRE(static_cast<int>(SecurityManager::EventId::SessionUnavailable) == 4);
    REQUIRE(static_cast<int>(SecurityManager::EventId::SessionRestored) == 5);
    REQUIRE(static_cast<int>(SecurityManager::EventId::DebuggerObserved) == 6);
    REQUIRE(static_cast<int>(SecurityManager::EventId::UnknownModuleObserved) == 7);
}

} // namespace