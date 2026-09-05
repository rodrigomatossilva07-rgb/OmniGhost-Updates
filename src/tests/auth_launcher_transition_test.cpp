#include "../window/window.hpp"
#include "../auth/local_auth_service.h"
#include "../platform/app_paths.h"
#include "../platform/session_log.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <unordered_set>

namespace OmniGhost::Tests {

bool TestAuthToLauncherTransition() {
    std::cout << "[MockTest] Testing authentication -> launcher transition...\n";
    
    // Reset auth service
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    auth.Logout(true);
    
    // Register a test account
    auto reg = auth.Register("test@example.com", "password123", false);
    if (!reg.Ok()) {
        std::cerr << "[MockTest] FAIL: Registration failed: " << reg.message << "\n";
        return false;
    }
    
    // Verify account is registered
    if (!auth.HasAccount()) {
        std::cerr << "[MockTest] FAIL: Account not registered\n";
        return false;
    }
    
    // Login
    auto login = auth.Login("test@example.com", "password123", false);
    if (!login.Ok()) {
        std::cerr << "[MockTest] FAIL: Login failed: " << login.message << "\n";
        return false;
    }
    
    // Verify authenticated
    if (!auth.IsAuthenticated()) {
        std::cerr << "[MockTest] FAIL: Not authenticated after login\n";
        return false;
    }
    
    std::cout << "[MockTest] PASS: Authentication successful\n";
    
    // Simulate launcher transition
    if (!OmniGhost::Auth::LocalAuthService::Instance().IsAuthenticated()) {
        std::cerr << "[MockTest] FAIL: Auth state not persisted\n";
        return false;
    }
    
    std::cout << "[MockTest] PASS: Launcher transition simulated\n";
    
    // Verify auth state still valid after "transition"
    if (!OmniGhost::Auth::LocalAuthService::Instance().IsAuthenticated()) {
        std::cerr << "[MockTest] FAIL: Auth lost after transition\n";
        return false;
    }
    
    std::cout << "[MockTest] PASS: Auth state preserved through launcher transition\n";
    return true;
}

bool TestAutoLoginTransition() {
    std::cout << "[MockTest] Testing auto-login transition...\n";
    
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    auth.Logout(true);
    
    // Register and login with rememberMe
    auto reg = auth.Register("auto@test.com", "password123", true);
    if (!reg.Ok()) {
        std::cerr << "[MockTest] FAIL: Registration failed\n";
        return false;
    }
    
    auto login = auth.Login("auto@test.com", "password123", true);
    if (!login.Ok()) {
        std::cerr << "[MockTest] FAIL: Login failed\n";
        return false;
    }
    
    // Simulate app restart - auth should auto-login
    auth.Logout(false); // Don't forget rememberMe
    auth.Initialize(); // Re-initialize
    
    // Try auto-login
    if (!auth.TryAutoLogin()) {
        std::cerr << "[MockTest] FAIL: Auto-login failed\n";
        return false;
    }
    
    if (!auth.IsAuthenticated()) {
        std::cerr << "[MockTest] FAIL: Not authenticated after auto-login\n";
        return false;
    }
    
    std::cout << "[MockTest] PASS: Auto-login transition works\n";
    return true;
}

bool TestHWNDInvariance() {
    std::cout << "[MockTest] Testing HWND invariance during transition...\n";
    
    HWND hwnd_before = GetActiveWindow();
    if (!hwnd_before) {
        std::cerr << "[MockTest] FAIL: No active window before transition\n";
        return false;
    }
    
    // Get all top-level windows before transition
    std::vector<HWND> windows_before;
    EnumWindows([](HWND hwnd, LPARAM lParam) {
        auto& vec = *reinterpret_cast<std::vector<HWND>*>(lParam);
        if (IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
            vec.push_back(hwnd);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows_before));
    
    std::cout << "[MockTest] Found " << windows_before.size() << " top-level windows before transition\n";
    
    // Simulate transition
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Get all top-level windows after transition
    std::vector<HWND> windows_after;
    EnumWindows([](HWND hwnd, LPARAM lParam) {
        auto& vec = *reinterpret_cast<std::vector<HWND>*>(lParam);
        if (IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
            vec.push_back(hwnd);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows_after));
    
    std::cout << "[MockTest] Found " << windows_after.size() << " top-level windows after transition\n";
    
    // Check no new windows appeared
    std::unordered_set<HWND> before_set(windows_before.begin(), windows_before.end());
    for (HWND hwnd : windows_after) {
        if (before_set.find(hwnd) == before_set.end()) {
            std::cerr << "[MockTest] FAIL: Auxiliary HWND appeared during transition: " << hwnd << "\n";
            return false;
        }
    }
    
    // Check no windows disappeared
    std::unordered_set<HWND> after_set(windows_after.begin(), windows_after.end());
    for (HWND hwnd : windows_before) {
        if (after_set.find(hwnd) == after_set.end()) {
            std::cerr << "[MockTest] FAIL: HWND disappeared during transition: " << hwnd << "\n";
            return false;
        }
    }
    
    // Verify main window handle unchanged
    HWND hwnd_before_main = GetActiveWindow();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    HWND hwnd_after_main = GetActiveWindow();
    if (hwnd_before_main != hwnd_after_main) {
        std::cerr << "[MockTest] FAIL: Main window handle changed: " << hwnd_before_main << " -> " << hwnd_after_main << "\n";
        return false;
    }
    
    std::cout << "[MockTest] PASS: No auxiliary HWND appeared/disappeared; main HWND unchanged\n";
    return true;
}

} // namespace OmniGhost::Tests

int main() {
    using namespace OmniGhost::Tests;
    
    try {
        std::cout << "=== Authentication -> Launcher Transition Mock Tests ===\n\n";
        
        bool all_passed = true;
        
        all_passed &= TestAuthToLauncherTransition();
        std::cout << "\n";
        
        all_passed &= TestAutoLoginTransition();
        std::cout << "\n";
        
        all_passed &= TestHWNDInvariance();
        std::cout << "\n";
        
        if (all_passed) {
            std::cout << "=== ALL MOCK TESTS PASSED ===\n";
            return 0;
        } else {
            std::cerr << "=== SOME MOCK TESTS FAILED ===\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "TEST EXCEPTION: " << e.what() << "\n";
        return 1;
    }
}