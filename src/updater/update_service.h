#pragma once
#include "http_client.h"
#include "update_types.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <stop_token>

namespace OmniGhost::Update {
class UpdateService {
public:
    static UpdateService& Instance();
    ~UpdateService();
    void Initialize(Configuration configuration = {}, std::unique_ptr<IHttpClient> http = {});
    void CheckAsync(bool force = false);
    bool ApplyUserPreferences(Channel channel, bool automaticDownload, bool installOnExit);
    void DownloadAsync();
    void DownloadAndScheduleInstallOnExitAsync();
    void InstallPreparedUpdateAsync();
    bool InstallPreparedUpdate();
    bool ScheduleInstallOnExit();
    void DeferForSession();
    void RetryLastFailure();
    void VerifyInstallationAsync();
    void PrepareRepairAsync();
    void DismissStartupNotice();
    void Cancel();
    Snapshot GetSnapshot() const;
    bool ShouldExitForUpdate() const { return exitRequested_.load(); }
    bool BlocksGameLaunch() const;
    void Shutdown();
private:
    UpdateService() = default;
    void RunCheck(bool force);
    void StartDownloadWorker();
    void RunDownload();
    void RunPrepareRepair();
    bool ValidateManifestSignature(const std::string& manifestUrl, const std::string& manifestBytes,
                                   std::string& error);
    void SetError(const std::string& userMessage, const std::string& detail, const std::string& stage);
    void SetSnapshot(const std::function<void(Snapshot&)>& change);
    Configuration config_{}; std::unique_ptr<IHttpClient> http_;
    mutable std::mutex mutex_; Snapshot snapshot_{}; std::optional<Manifest> manifest_; std::optional<Package> package_;
    std::filesystem::path downloadedPackage_; std::jthread worker_; std::atomic_bool cancelled_{false};
    std::atomic_bool busy_{false}; std::atomic_bool initialized_{false}; std::atomic_bool exitRequested_{false};
    std::atomic_bool installOnExit_{false}; bool deferred_{};
};

// Called by the normal application entrypoint before UpdateService::Initialize.
// It captures a successful update or rollback marker passed by the temporary updater.
void CaptureUpdaterStartupNoticeFromCommandLine(int argc, wchar_t** argv);
bool ConfirmUpdaterStartupFromCommandLine(int argc, wchar_t** argv);
}
