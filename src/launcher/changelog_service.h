#pragma once

#include "launcher_data.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <stop_token>
#include <vector>

namespace OmniGhost::Update {
class IHttpClient;
}

namespace Launcher {

enum class ChangelogStatus {
    Idle,
    Loading,
    Ready,
    Empty,
    OfflineCache,
    Error
};

struct ChangelogChange {
    ChangeType type{ChangeType::Improved};
    std::string text;
};

struct ChangelogModule {
    std::string id;
    std::string name;
    std::string version;
    std::vector<ChangelogChange> changes;
};

struct ChangelogRelease {
    std::string id;
    std::string version;
    std::string tag;
    std::string publishedAt;
    std::string title;
    std::string summary;
    std::string author;
    std::string commit;
    std::string releaseUrl;
    std::vector<ChangelogModule> modules;
};

struct ChangelogSnapshot {
    ChangelogStatus status{ChangelogStatus::Idle};
    std::string generatedAt;
    std::string channel;
    std::string message;
    bool fromCache{};
    std::vector<ChangelogRelease> releases;
};

class ChangelogService {
public:
    static ChangelogService& Instance();
    ~ChangelogService();

    void Initialize(std::unique_ptr<OmniGhost::Update::IHttpClient> http = {});
    void RefreshAsync(bool force = false);
    ChangelogSnapshot GetSnapshot() const;
    int UnreadCount() const;
    bool IsBusy() const { return busy_.load(); }
    void Shutdown();

    static std::string CardId(const ChangelogRelease& release,
                              const ChangelogModule& module);

private:
    ChangelogService() = default;
    ChangelogService(const ChangelogService&) = delete;
    ChangelogService& operator=(const ChangelogService&) = delete;

    void RunRefresh(bool force);
    void LoadCache();
    void SetSnapshot(ChangelogSnapshot value);

    mutable std::mutex mutex_;
    ChangelogSnapshot snapshot_;
    std::unique_ptr<OmniGhost::Update::IHttpClient> http_;
    std::jthread worker_;
    std::atomic_bool initialized_{false};
    std::atomic_bool busy_{false};
    std::atomic_bool cancelled_{false};
};

} // namespace Launcher
