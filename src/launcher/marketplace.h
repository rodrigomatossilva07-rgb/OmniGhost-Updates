#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace Launcher::Marketplace {

enum class ItemType : uint8_t {
    Config = 0,
    Theme = 1,
    Script = 2,
    Preset = 3
};

enum class ItemCategory : uint8_t {
    All = 0,
    Aim = 1,
    ESP = 2,
    Visuals = 3,
    Misc = 4,
    GameSpecific = 5
};

enum class ItemStatus : uint8_t {
    Pending = 0,
    Approved = 1,
    Rejected = 2,
    Deprecated = 3
};

struct ItemMetadata {
    std::string id;
    std::string name;
    std::string description;
    std::string author;
    std::string authorId;
    ItemType type;
    ItemCategory category;
    std::vector<std::string> tags;
    std::string version;
    std::string gameId;
    std::string previewImageUrl;
    std::string downloadUrl;
    std::string sha256;
    int64_t fileSize = 0;
    ItemStatus status = ItemStatus::Pending;
    int rating = 0;
    int downloadCount = 0;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    std::string changelog;
    bool isOfficial = false;
    bool isVerified = false;
    std::vector<std::string> dependencies;
};

struct SearchFilters {
    std::string query;
    ItemType type = ItemType::Config;
    ItemCategory category = ItemCategory::All;
    std::string gameId;
    std::string authorId;
    int minRating = 0;
    int maxRating = 5;
    bool onlyVerified = false;
    bool onlyOfficial = false;
    std::string sortBy = "rating";
    bool ascending = false;
    int page = 1;
    int pageSize = 20;
};

struct SearchResult {
    std::vector<ItemMetadata> items;
    int totalCount = 0;
    int currentPage = 1;
    int totalPages = 1;
    bool hasMore = false;
};

struct UserLibraryItem {
    std::string itemId;
    std::chrono::system_clock::time_point acquiredAt;
    bool isFavorite = false;
    bool isInstalled = false;
    std::string installedVersion;
    std::string installPath;
    int useCount = 0;
    std::chrono::system_clock::time_point lastUsed;
};

struct MarketplaceStats {
    int totalItems = 0;
    int totalAuthors = 0;
    int totalDownloads = 0;
    int totalRatings = 0;
    double averageRating = 0.0;
    std::vector<std::pair<std::string, int>> topCategories;
    std::vector<std::pair<std::string, int>> topAuthors;
    std::vector<std::pair<std::string, int>> topTags;
};

class MarketplaceClient {
public:
    virtual ~MarketplaceClient() = default;

    virtual std::optional<SearchResult> Search(const SearchFilters& filters) = 0;
    virtual std::optional<ItemMetadata> GetItem(const std::string& itemId) = 0;
    virtual bool DownloadItem(const std::string& itemId, const std::string& destinationPath) = 0;
    virtual bool InstallItem(const std::string& itemId, const std::string& installPath) = 0;
    virtual bool UninstallItem(const std::string& itemId) = 0;
    virtual std::vector<UserLibraryItem> GetUserLibrary() = 0;
    virtual bool ToggleFavorite(const std::string& itemId) = 0;
    virtual bool RateItem(const std::string& itemId, int rating) = 0;
    virtual bool SubmitItem(const ItemMetadata& item, const std::string& filePath) = 0;
    virtual bool UpdateItem(const std::string& itemId, const ItemMetadata& metadata) = 0;
    virtual bool DeleteItem(const std::string& itemId) = 0;
    virtual std::optional<MarketplaceStats> GetStats() = 0;
    virtual std::vector<ItemMetadata> GetRecommendations(const std::string& gameId, int limit = 10) = 0;
    virtual std::vector<ItemMetadata> GetTrending(int limit = 10) = 0;
    virtual std::vector<ItemMetadata> GetLatest(int limit = 10) = 0;
    virtual std::vector<ItemMetadata> GetFeatured(int limit = 10) = 0;
};

class LocalMarketplaceCache {
public:
    static LocalMarketplaceCache& Instance();

    void CacheItem(const ItemMetadata& item);
    std::optional<ItemMetadata> GetCachedItem(const std::string& itemId);
    std::vector<ItemMetadata> GetCachedItems(const std::string& gameId = "");
    void ClearCache();
    void ClearGameCache(const std::string& gameId);
    void SetCacheExpiry(std::chrono::hours hours);
    bool IsCacheValid(const std::string& itemId) const;

private:
    LocalMarketplaceCache() = default;
    struct CacheEntry {
        ItemMetadata item;
        std::chrono::system_clock::time_point cachedAt;
    };
    std::unordered_map<std::string, CacheEntry> cache_;
    std::chrono::hours cacheExpiry_ = std::chrono::hours(24);
};

class MarketplaceManager {
public:
    static MarketplaceManager& Instance();

    void Initialize(std::unique_ptr<MarketplaceClient> client);
    void Shutdown();

    std::optional<SearchResult> Search(const SearchFilters& filters);
    std::optional<ItemMetadata> GetItem(const std::string& itemId);
    bool DownloadAndInstall(const std::string& itemId);
    std::vector<UserLibraryItem> GetUserLibrary();
    bool ToggleFavorite(const std::string& itemId);
    bool RateItem(const std::string& itemId, int rating);
    std::optional<MarketplaceStats> GetStats();
    std::vector<ItemMetadata> GetRecommendations(const std::string& gameId, int limit = 10);
    std::vector<ItemMetadata> GetTrending(int limit = 10);
    std::vector<ItemMetadata> GetLatest(int limit = 10);
    std::vector<ItemMetadata> GetFeatured(int limit = 10);
    std::vector<ItemMetadata> GetUserLibraryItems();

    void RefreshCache();
    void ClearCache();

    bool IsInitialized() const { return client_ != nullptr; }

private:
    MarketplaceManager() = default;
    std::unique_ptr<MarketplaceClient> client_;
    LocalMarketplaceCache cache_;
    std::mutex mutex_;
};

void DrawMarketplaceUI();
void DrawItemDetails(const ItemMetadata& item);
void DrawSearchFilters(SearchFilters& filters);
void DrawItemCard(const ItemMetadata& item);
void DrawUserLibrary();

} // namespace Launcher::Marketplace