#pragma once
#include <string>
#include <vector>
#include <functional>

namespace Onboarding {

    enum class Step {
        Welcome = 0,
        LicenseAgreement,
        HardwareDetection,
        GameSelection,
        ThemeSelection,
        HotkeySetup,
        DMASetup,
        Complete,
        Count
    };

    struct WizardStep {
        Step id;
        std::string title;
        std::string description;
        std::string icon;
        bool optional = false;
        std::function<bool()> validator; // Returns true if step is complete
        std::function<void()> onEnter;
        std::function<void()> onExit;
    };

    void Initialize();
    void Shutdown();
    
    // Check if onboarding should run
    bool ShouldRunOnboarding();
    
    // Start onboarding
    void Start();
    
    // Get current step
    Step GetCurrentStep();
    const WizardStep& GetStepInfo(Step step);
    
    // Navigation
    bool NextStep();
    bool PreviousStep();
    bool GoToStep(Step step);
    
    // Complete onboarding
    void Complete();
    
    // Skip onboarding
    void Skip();
    
    // Draw wizard UI
    void DrawWizard();
    
    // Check if wizard is active
    bool IsActive();
    
    // Set completion flag
    void SetCompleted(bool completed);
    bool IsCompleted();
    
    // Reset onboarding (for testing)
    void Reset();

} // namespace Onboarding