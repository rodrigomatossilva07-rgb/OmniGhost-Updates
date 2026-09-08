#include "marketplace.h"
#include "../config/app_settings.h"
#include "../platform/app_paths.h"
#include "../window/localization.h"
#include <Windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Launcher::Marketplace {

namespace {

std::string g_marketplaceApiUrl = "https://api.omnighost.dev/marketplace/v1";
std::string g_cdnBaseUrl = "https://cdn.omnighost.dev/marketplace";
std::string g_userAgent = "OmniGhost-Launcher/3.5.4";

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string BuildUrl(const std::string& endpoint, const std::string& query = "") {
    std::string url = g_marketplaceApiUrl + endpoint;
    if (!query.empty()) {
        url += "?" + query;
    }
    return url;
}

std::string BuildQueryString(const SearchFilters& filters) {
    std::string query;
    auto addParam = [&](const std::string& key, const std::string& value) {
        if (!value.empty()) {
            if (!query.empty()) query += "&";
            query += key + "=" + value;
        }
    };
    auto addParamInt = [&](const std::string& key, int value) {
        if (value > 0) {
            if (!query.empty()) query += "&";
            query += key + "=" + std::to_string(value);
        }
    };
    auto addParamBool = [&](const std::string& key, bool value) {
        if (value) {
            if (!query.empty()) query += "&";
            query += key + "=true";
        }
    };

    addParam("q", filters.query);
    if (filters.type != ItemType::Config) addParam("type", std::to_string(static_cast<int>(filters.type)));
    if (filters.category != ItemCategory::All) addParam("category", std::to_string(static_cast<int>(filters.category)));
    addParam("game", filters.gameId);
    addParam("author", filters.authorId);
    addParamInt("min_rating", filters.minRating);
    addParamInt("max_rating", filters.maxRating);
    addParamBool("verified", filters.onlyVerified);
    addParamBool("official", filters.onlyOfficial);
    addParam("sort", filters.sortBy);
    addParam("order", filters.ascending ? "asc" : "desc");
    addParamInt("page", filters.page);
    addParamInt("per_page", filters.pageSize);
    return query;
}

std::optional<std::string> HttpGet(const std::string& url, const std::string& token = "") {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, g_userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, ("User-Agent: " + g_userAgent).c_str());
    if (!token.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[Marketplace] HTTP GET failed: " << curl_easy_strerror(res) << std::endl;
        return std::nullopt;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        std::cerr << "[Marketplace] HTTP error: " << httpCode << std::endl;
        return std::nullopt;
    }

    return response;
}

std::optional<std::string> HttpPost(const std::string& url, const std::string& jsonData, const std::string& token = "") {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, jsonData.size());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, g_userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("User-Agent: " + g_userAgent).c_str());
    if (!token.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[Marketplace] HTTP POST failed: " << curl_easy_strerror(res) << std::endl;
        return std::nullopt;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        std::cerr << "[Marketplace] HTTP error: " << httpCode << std::endl;
        return std::nullopt;
    }

    return response;
}

ItemMetadata ParseItemFromJson(const json& j) {
    ItemMetadata item;
    item.id = j.value("id", "");
    item.name = j.value("name", "");
    item.description = j.value("description", "");
    item.author = j.value("author", "");
    item.authorId = j.value("author_id", "");
    item.type = static_cast<ItemType>(j.value("type", 0));
    item.category = static_cast<ItemCategory>(j.value("category", 0));
    item.tags = j.value("tags", std::vector<std::string>{});
    item.version = j.value("version", "1.0.0");
    item.gameId = j.value("game_id", "");
    item.previewImageUrl = j.value("preview_image_url", "");
    item.downloadUrl = j.value("download_url", "");
    item.sha256 = j.value("sha256", "");
    item.fileSize = j.value("file_size", 0);
    item.status = static_cast<ItemStatus>(j.value("status", 0));
    item.rating = j.value("rating", 0);
    item.downloadCount = j.value("download_count", 0);

    if (j.contains("created_at")) {
        auto createdStr = j["created_at"].get<std::string>();
        std::tm tm = {};
        std::istringstream ss(createdStr);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        item.createdAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
    if (j.contains("updated_at")) {
        auto updatedStr = j["updated_at"].get<std::string>();
        std::tm tm = {};
        std::istringstream ss(updatedStr);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        item.updatedAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    item.changelog = j.value("changelog", "");
    item.isOfficial = j.value("is_official", false);
    item.isVerified = j.value("is_verified", false);
    item.dependencies = j.value("dependencies", std::vector<std::string>{});
    return item;
}

SearchResult ParseSearchResult(const json& j) {
    SearchResult result;
    result.totalCount = j.value("total_count", 0);
    result.currentPage = j.value("current_page", 1);
    result.totalPages = j.value("total_pages", 1);
    result.hasMore = j.value("has_more", false);

    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& itemJson : j["items"]) {
            result.items.push_back(ParseItemFromJson(itemJson));
        }
    }
    result.hasMore = result.currentPage < result.totalPages;
    return result;
}

MarketplaceStats ParseStatsFromJson(const json& j) {
    MarketplaceStats stats;
    stats.totalItems = j.value("total_items", 0);
    stats.totalAuthors = j.value("total_authors", 0);
    stats.totalDownloads = j.value("total_downloads", 0);
    stats.totalRatings = j.value("total_ratings", 0);
    stats.averageRating = j.value("average_rating", 0.0);

    if (j.contains("top_categories") && j["top_categories"].is_array()) {
        for (const auto& cat : j["top_categories"]) {
            stats.topCategories.emplace_back(cat.value("name", ""), cat.value("count", 0));
        }
    }
    if (j.contains("top_authors") && j["top_authors"].is_array()) {
        for (const auto& auth : j["top_authors"]) {
            stats.topAuthors.emplace_back(auth.value("name", ""), auth.value("count", 0));
        }
    }
    if (j.contains("top_tags") && j["top_tags"].is_array()) {
        for (const auto& tag : j["top_tags"]) {
            stats.topTags.emplace_back(tag.value("name", ""), tag.value("count", 0));
        }
    }
    return stats;
}

UserLibraryItem ParseLibraryItemFromJson(const json& j) {
    UserLibraryItem item;
    item.itemId = j.value("item_id", "");
    item.isFavorite = j.value("is_favorite", false);
    item.isInstalled = j.value("is_installed", false);
    item.installedVersion = j.value("installed_version", "");
    item.installPath = j.value("install_path", "");
    item.useCount = j.value("use_count", 0);

    if (j.contains("acquired_at")) {
        auto acquiredStr = j["acquired_at"].get<std::string>();
        std::tm tm = {};
        std::istringstream ss(acquiredStr);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        item.acquiredAt = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
    if (j.contains("last_used")) {
        auto lastUsedStr = j["last_used"].get<std::string>();
        std::tm tm = {};
        std::istringstream ss(lastUsedStr);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        item.lastUsed = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
    return item;
}

} // namespace

class HttpMarketplaceClient : public MarketplaceClient {
public:
    explicit HttpMarketplaceClient(const std::string& apiUrl = "") {
        if (!apiUrl.empty()) g_marketplaceApiUrl = apiUrl;
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~HttpMarketplaceClient() {
        curl_global_cleanup();
    }

    std::string GetAuthToken() const {
        return app_settings::config.marketplace_token;
    }

    std::optional<SearchResult> Search(const SearchFilters& filters) override {
        std::string query = BuildQueryString(filters);
        auto response = HttpGet(BuildUrl("/items/search", query), GetAuthToken());
        if (!response) return std::nullopt;

        try {
            json j = json::parse(*response);
            return ParseSearchResult(j);
        } catch (const std::exception& ex) {
            std::cerr << "[Marketplace] Failed to parse search results: " << ex.what() << std::endl;
            return std::nullopt;
        }
    }

    std::optional<ItemMetadata> GetItem(const std::string& itemId) override {
        auto response = HttpGet(BuildUrl("/items/" + itemId), GetAuthToken());
        if (!response) return std::nullopt;

        try {
            json j = json::parse(*response);
            return ParseItemFromJson(j);
        } catch (const std::exception& ex) {
            std::cerr << "[Marketplace] Failed to parse item: " << ex.what() << std::endl;
            return std::nullopt;
        }
    }

    bool DownloadItem(const std::string& itemId, const std::string& destinationPath) override {
        auto item = GetItem(itemId);
        if (!item || item->downloadUrl.empty()) return false;

        CURL* curl = curl_easy_init();
        if (!curl) return false;

        FILE* fp = fopen(destinationPath.c_str(), "wb");
        if (!fp) {
            curl_easy_cleanup(curl);
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, item->downloadUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, g_userAgent.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[Marketplace] Download failed: " << curl_easy_strerror(res) << std::endl;
            std::filesystem::remove(destinationPath);
            return false;
        }

        if (!item->sha256.empty()) {
            // Verify SHA256
            // TODO: Implement SHA256 verification
        }

        return true;
    }

    bool InstallItem(const std::string& itemId, const std::string& installPath) override {
        fs::path tempDir = fs::temp_directory_path() / ("omnighost_marketplace_" + itemId);
        fs::create_directories(tempDir);

        fs::path downloadPath = tempDir / "download";
        if (!DownloadItem(itemId, downloadPath.string())) {
            fs::remove_all(tempDir);
            return false;
        }

        fs::create_directories(installPath);
        fs::copy(downloadPath, fs::path(installPath) / "config.json", fs::copy_options::overwrite_existing);

        fs::remove_all(tempDir);
        return true;
    }

    bool UninstallItem(const std::string& itemId) override {
        auto library = GetUserLibrary();
        for (const auto& item : library) {
            if (item.itemId == itemId && item.isInstalled) {
                try {
                    fs::remove_all(item.installPath);
                    return true;
                } catch (...) {
                    return false;
                }
            }
        }
        return false;
    }

    std::vector<UserLibraryItem> GetUserLibrary() override {
        auto response = HttpGet(BuildUrl("/user/library"), GetAuthToken());
        if (!response) return {};

        try {
            json j = json::parse(*response);
            std::vector<UserLibraryItem> library;
            if (j.contains("items") && j["items"].is_array()) {
                for (const auto& itemJson : j["items"]) {
                    library.push_back(ParseLibraryItemFromJson(itemJson));
                }
            }
            return library;
        } catch (const std::exception& ex) {
            std::cerr << "[Marketplace] Failed to parse library: " << ex.what() << std::endl;
            return {};
        }
    }

    bool ToggleFavorite(const std::string& itemId) override {
        auto response = HttpPost(BuildUrl("/items/" + itemId + "/favorite"), "{}", GetAuthToken());
        return response.has_value();
    }

    bool RateItem(const std::string& itemId, int rating) override {
        json j;
        j["rating"] = std::clamp(rating, 1, 5);
        auto response = HttpPost(BuildUrl("/items/" + itemId + "/rate"), j.dump(), GetAuthToken());
        return response.has_value();
    }

    bool SubmitItem(const ItemMetadata& item, const std::string& filePath) override {
        // Upload file first, then submit metadata
        // This is a simplified implementation
        json j;
        j["name"] = item.name;
        j["description"] = item.description;
        j["type"] = static_cast<int>(item.type);
        j["category"] = static_cast<int>(item.category);
        j["tags"] = item.tags;
        j["game_id"] = item.gameId;
        j["version"] = item.version;
        j["dependencies"] = item.dependencies;

        auto response = HttpPost(BuildUrl("/items"), j.dump(), GetAuthToken());
        return response.has_value();
    }

    bool UpdateItem(const std::string& itemId, const ItemMetadata& metadata) override {
        json j;
        j["name"] = metadata.name;
        j["description"] = metadata.description;
        j["tags"] = metadata.tags;
        j["version"] = metadata.version;
        j["changelog"] = metadata.changelog;
        j["dependencies"] = metadata.dependencies;

        auto response = HttpPost(BuildUrl("/items/" + itemId), j.dump(), GetAuthToken());
        return response.has_value();
    }

    bool DeleteItem(const std::string& itemId) override {
        // DELETE request
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, (g_marketplaceApiUrl + "/items/" + itemId).c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, g_userAgent.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, ("User-Agent: " + g_userAgent).c_str());
        headers = curl_slist_append(headers, ("Authorization: Bearer " + GetAuthToken()).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return res == CURLE_OK;
    }

    std::optional<MarketplaceStats> GetStats() override {
        auto response = HttpGet(BuildUrl("/stats"), GetAuthToken());
        if (!response) return std::nullopt;

        try {
            json j = json::parse(*response);
            return ParseStatsFromJson(j);
        } catch (const std::exception& ex) {
            std::cerr << "[Marketplace] Failed to parse stats: " << ex.what() << std::endl;
            return std::nullopt;
        }
    }

    std::vector<ItemMetadata> GetRecommendations(const std::string& gameId, int limit) override {
        auto response = HttpGet(BuildUrl("/recommendations?game=" + gameId + "&limit=" + std::to_string(limit)), GetAuthToken());
        if (!response) return {};

        try {
            json j = json::parse(*response);
            std::vector<ItemMetadata> items;
            if (j.contains("items") && j["items"].is_array()) {
                for (const auto& itemJson : j["items"]) {
                    items.push_back(ParseItemFromJson(itemJson));
                }
            }
            return items;
        } catch (...) {
            return {};
        }
    }

    std::vector<ItemMetadata> GetTrending(int limit) override {
        auto response = HttpGet(BuildUrl("/trending?limit=" + std::to_string(limit)), GetAuthToken());
        if (!response) return {};

        try {
            json j = json::parse(*response);
            std::vector<ItemMetadata> items;
            if (j.contains("items") && j["items"].is_array()) {
                for (const auto& itemJson : j["items"]) {
                    items.push_back(ParseItemFromJson(itemJson));
                }
            }
            return items;
        } catch (...) {
            return {};
        }
    }

    std::vector<ItemMetadata> GetLatest(int limit) override {
        auto response = HttpGet(BuildUrl("/latest?limit=" + std::to_string(limit)), GetAuthToken());
        if (!response) return {};

        try {
            json j = json::parse(*response);
            std::vector<ItemMetadata> items;
            if (j.contains("items") && j["items"].is_array()) {
                for (const auto& itemJson : j["items"]) {
                    items.push_back(ParseItemFromJson(itemJson));
                }
            }
            return items;
        } catch (...) {
            return {};
        }
    }

    std::vector<ItemMetadata> GetFeatured(int limit) override {
        auto response = HttpGet(BuildUrl("/featured?limit=" + std::to_string(limit)), GetAuthToken());
        if (!response) return {};

        try {
            json j = json::parse(*response);
            std::vector<ItemMetadata> items;
            if (j.contains("items") && j["items"].is_array()) {
                for (const auto& itemJson : j["items"]) {
                    items.push_back(ParseItemFromJson(itemJson));
                }
            }
            return items;
        } catch (...) {
            return {};
        }
    }
};

std::string LocalMarketplaceCache::CacheKey(const std::string& itemId) {
    return "marketplace_item_" + itemId;
}

void LocalMarketplaceCache::CacheItem(const ItemMetadata& item) {
    CacheEntry entry;
    entry.item = item;
    entry.cachedAt = std::chrono::system_clock::now();
    cache_[item.id] = entry;
}

std::optional<ItemMetadata> LocalMarketplaceCache::GetCachedItem(const std::string& itemId) {
    auto it = cache_.find(itemId);
    if (it != cache_.end() && IsCacheValid(itemId)) {
        return it->second.item;
    }
    return std::nullopt;
}

std::vector<ItemMetadata> LocalMarketplaceCache::GetCachedItems(const std::string& gameId) {
    std::vector<ItemMetadata> items;
    for (const auto& [id, entry] : cache_) {
        if (IsCacheValid(id)) {
            if (gameId.empty() || entry.item.gameId == gameId) {
                items.push_back(entry.item);
            }
        }
    }
    return items;
}

void LocalMarketplaceCache::ClearCache() {
    cache_.clear();
}

void LocalMarketplaceCache::ClearGameCache(const std::string& gameId) {
    std::vector<std::string> toRemove;
    for (const auto& [id, entry] : cache_) {
        if (entry.item.gameId == gameId) {
            toRemove.push_back(id);
        }
    }
    for (const auto& id : toRemove) {
        cache_.erase(id);
    }
}

void LocalMarketplaceCache::SetCacheExpiry(std::chrono::hours hours) {
    cacheExpiry_ = hours;
}

bool LocalMarketplaceCache::IsCacheValid(const std::string& itemId) const {
    auto it = cache_.find(itemId);
    if (it == cache_.end()) return false;
    auto now = std::chrono::system_clock::now();
    return (now - it->second.cachedAt) < cacheExpiry_;
}

LocalMarketplaceCache& LocalMarketplaceCache::Instance() {
    static LocalMarketplaceCache instance;
    return instance;
}

MarketplaceManager& MarketplaceManager::Instance() {
    static MarketplaceManager instance;
    return instance;
}

void MarketplaceManager::Initialize(std::unique_ptr<MarketplaceClient> client) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_ = std::move(client);
}

void MarketplaceManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    client_.reset();
}

std::optional<SearchResult> MarketplaceManager::Search(const SearchFilters& filters) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return std::nullopt;

    auto cached = LocalMarketplaceCache::Instance().GetCachedItems(filters.gameId);
    if (!cached.empty() && filters.query.empty() && filters.page == 1) {
        SearchResult result;
        result.items = cached;
        result.totalCount = static_cast<int>(cached.size());
        result.currentPage = 1;
        result.totalPages = 1;
        result.hasMore = false;
        return result;
    }

    auto result = client_->Search(filters);
    if (result) {
        for (const auto& item : result->items) {
            LocalMarketplaceCache::Instance().CacheItem(item);
        }
    }
    return result;
}

std::optional<ItemMetadata> MarketplaceManager::GetItem(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return std::nullopt;

    auto cached = LocalMarketplaceCache::Instance().GetCachedItem(itemId);
    if (cached) return cached;

    auto item = client_->GetItem(itemId);
    if (item) {
        LocalMarketplaceCache::Instance().CacheItem(*item);
    }
    return item;
}

bool MarketplaceManager::DownloadAndInstall(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return false;

    auto item = client_->GetItem(itemId);
    if (!item) return false;

    fs::path installDir = OmniGhost::Paths::ConfigDir() / "marketplace" / itemId;
    fs::create_directories(installDir);

    if (!client_->InstallItem(itemId, installDir.string())) {
        return false;
    }

    LocalMarketplaceCache::Instance().CacheItem(*client_->GetItem(itemId).value());
    return true;
}

std::vector<UserLibraryItem> MarketplaceManager::GetUserLibrary() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return {};

    auto library = client_->GetUserLibrary();
    return library;
}

bool MarketplaceManager::ToggleFavorite(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return false;

    bool result = client_->ToggleFavorite(itemId);
    if (result) {
        LocalMarketplaceCache::Instance().ClearGameCache(""); // Invalidate cache
    }
    return result;
}

bool MarketplaceManager::RateItem(const std::string& itemId, int rating) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return false;

    bool result = client_->RateItem(itemId, rating);
    if (result) {
        LocalMarketplaceCache::Instance().ClearGameCache(""); // Invalidate cache
    }
    return result;
}

std::optional<MarketplaceStats> MarketplaceManager::GetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return std::nullopt;
    return client_->GetStats();
}

std::vector<ItemMetadata> MarketplaceManager::GetRecommendations(const std::string& gameId, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return {};
    return client_->GetRecommendations(gameId, limit);
}

std::vector<ItemMetadata> MarketplaceManager::GetTrending(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return {};
    return client_->GetTrending(limit);
}

std::vector<ItemMetadata> MarketplaceManager::GetLatest(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return {};
    return client_->GetLatest(limit);
}

std::vector<ItemMetadata> MarketplaceManager::GetFeatured(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return {};
    return client_->GetFeatured(limit);
}

std::vector<ItemMetadata> MarketplaceManager::GetUserLibraryItems() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return {};

    auto library = client_->GetUserLibrary();
    std::vector<ItemMetadata> items;
    for (const auto& libItem : library) {
        if (auto item = client_->GetItem(libItem.itemId)) {
            items.push_back(*item);
        }
    }
    return items;
}

void MarketplaceManager::RefreshCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    LocalMarketplaceCache::Instance().ClearCache();
}

void MarketplaceManager::ClearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    LocalMarketplaceCache::Instance().ClearCache();
}

void DrawMarketplaceUI() {
    // TODO: Implement ImGui UI for marketplace
}

void DrawItemDetails(const ItemMetadata& item) {
    // TODO: Implement ImGui UI for item details
}

void DrawSearchFilters(SearchFilters& filters) {
    // TODO: Implement ImGui UI for search filters
}

void DrawItemCard(const ItemMetadata& item) {
    // TODO: Implement ImGui UI for item card
}

void DrawUserLibrary() {
    // TODO: Implement ImGui UI for user library
}

} // namespace Launcher::Marketplace