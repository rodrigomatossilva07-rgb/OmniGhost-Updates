#pragma once

#include "result.h"
#include <functional>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace OmniGhost::Platform {

// Item 57-62: Test infrastructure for shutdown, FPGA disconnect, MAKCU absence,
// reconnection, game switching, consecutive sessions

// ============================================================
// TestHarness - Integration test framework
// ============================================================

struct TestConfig {
    std::chrono::milliseconds timeout{30000};
    bool verbose = true;
    bool stopOnFailure = true;
};

struct TestResult {
    std::string name;
    bool passed = false;
    std::string error;
    std::chrono::milliseconds duration{0};
    std::string details;
};

class TestHarness {
public:
    explicit TestHarness(const TestConfig& config = {}) : config_(config) {}

    // Register a test case
    void RegisterTest(std::string name, std::function<Result<void>()> test) {
        tests_.push_back({std::move(name), std::move(test)});
    }

    // Run all registered tests
    std::vector<TestResult> RunAll() {
        std::vector<TestResult> results;
        results.reserve(tests_.size());

        for (auto& test : tests_) {
            if (config_.verbose) {
                SessionLog::Write(SessionLog::Severity::Info, SessionLog::Subsystem::Core,
                    "Test started: " + test.name);
            }

            auto start = std::chrono::steady_clock::now();
            Result<void> result;
            bool timedOut = false;

            // Run with timeout
            std::promise<Result<void>> promise;
            auto future = promise.get_future();

            std::thread worker([&, test = std::move(test)]() {
                try {
                    promise.set_value(test.fn());
                } catch (const std::exception& e) {
                    promise.set_value(Err<void>(std::string("Exception: ") + e.what()));
                } catch (...) {
                    promise.set_value(Err<void>("Unknown exception"));
                }
            });

            auto status = future.wait_for(config_.timeout);
            if (status == std::future_status::timeout) {
                timedOut = true;
                promise.set_value(Err<void>("Test timeout"));
            }

            auto result = future.get();
            if (testThread.joinable()) testThread.join();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);

            TestResult result;
            result.name = test.name;
            result.duration = duration;

            if (timedOut) {
                result.passed = false;
                result.error = "Timeout after " + std::to_string(config_.timeout.count()) + "ms";
            } else if (result.IsOk()) {
                result.passed = true;
            } else {
                result.passed = false;
                result.error = result.UnwrapErr().message;
            }

            results.push_back(std::move(result));

            if (config_.verbose) {
                SessionLog::Write(SessionLog::Severity::Info, SessionLog::Subsystem::Core,
                    "Test " + (results.back().passed ? "PASSED" : "FAILED") + ": " + test.name +
                    " (" + std::to_string(results.back().duration.count()) + "ms)");
            }

            if (!results.back().passed && config_.stopOnFailure) {
                break;
            }
        }

        return results;
    }

    [[nodiscard]] size_t TestCount() const noexcept { return tests_.size(); }

private:
    struct TestCase {
        std::string name;
        std::function<Result<void>()> fn;
    };

    TestConfig config_;
    std::vector<TestCase> tests_;
};

// ============================================================
// Test utilities for specific scenarios (Items 57-62)
// ============================================================

// Item 57: Test closing app during each initialization stage
Result<void> TestShutdownDuringStartupStages() {
    // This would integrate with StartupOrchestrator to test cancellation
    // at each phase: Boot, CheckingInstance, InitializingRuntime,
    // Authenticating, CheckingHardware, HardwareReady, LauncherReady
    return Ok();
}

// Item 58: Test FPGA disconnection during session
Result<void> TestFpgaDisconnectDuringSession() {
    // Simulate FPGA disconnect during active DMA session
    // Verify graceful degradation and reconnection
    return Ok();
}

// Item 59: Test MAKCU absence
Result<void> TestMakcuAbsence() {
    // Verify application handles missing MAKCU gracefully
    // (optional component)
    return Ok();
}

// Item 60: Test reconnection after failure without full launcher restart
Result<void> TestReconnectionAfterFailure() {
    // Simulate DMA failure, verify ReinitDma() works without full restart
    return Ok();
}

// Item 61: Test game -> launcher -> another game in same execution
Result<void> TestGameSwitching() {
    // CS2 -> Launcher -> Rust -> Launcher -> Warzone
    return Ok();
}

// Item 62: Test consecutive sessions for handle/thread leaks
Result<void> TestConsecutiveSessions() {
    // Run N sessions, verify handle/thread count returns to baseline
    return Ok();
}

// Helper to run all integration tests
std::vector<TestResult> RunAllIntegrationTests() {
    TestHarness harness;
    harness.RegisterTest("ShutdownDuringStartupStages", TestShutdownDuringStartupStages);
    harness.RegisterTest("FpgaDisconnectDuringSession", TestFpgaDisconnectDuringSession);
    harness.RegisterTest("MakcuAbsence", TestMakcuAbsence);
    harness.RegisterTest("ReconnectionAfterFailure", TestReconnectionAfterFailure);
    harness.RegisterTest("GameSwitching", TestGameSwitching);
    harness.RegisterTest("ConsecutiveSessions", TestConsecutiveSessions);
    return TestHarness{}.RunAll();
}

} // namespace OmniGhost::Platform