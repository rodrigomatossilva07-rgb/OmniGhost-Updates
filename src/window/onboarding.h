#pragma once
#include <string>
#include <vector>
#include <functional>

namespace Onboarding {

    enum class Step {
        Welcome = 0,
        LicenseAgreement,
        HardwareDetection,
        Complete,
        Count
    };

    struct WizardStep {
        Step id;
        std::string title;
        std::string description;
        std::string icon;
        bool optional = false;
        std::function<bool()> validator;
        std::function<void()> onEnter;
        std::function<void()> onExit;
    };

    void Initialize();
    void Shutdown();


    bool ShouldRunOnboarding();


    void Start();


    Step GetCurrentStep();
    const WizardStep& GetStepInfo(Step step);


    bool NextStep();
    bool PreviousStep();
    bool GoToStep(Step step);


    void Complete();


    void Skip();


    void DrawWizard();


    bool IsActive();


    void SetCompleted(bool completed);
    bool IsCompleted();


    void Reset();

}
