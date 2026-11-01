#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace OmniGhost::Update {

// Native, dependency-free UI used by the temporary updater process.  It is
// deliberately isolated from the launcher's DirectX/ImGui lifetime.
class UpdaterWindow {
public:
    UpdaterWindow();
    ~UpdaterWindow();
    UpdaterWindow(const UpdaterWindow&) = delete;
    UpdaterWindow& operator=(const UpdaterWindow&) = delete;

    bool Start(const std::string& targetVersion);
    void SetStage(std::wstring title, std::wstring detail, double progress = -1.0);
    void SetTransfer(std::uint64_t received, std::uint64_t total, double bytesPerSecond);
    void SetSuccess();
    void SetError(std::wstring summary, std::wstring technicalDetail);
    // Blocks only after an error. Returns true when the user requests a retry.
    bool WaitForErrorAction();
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
