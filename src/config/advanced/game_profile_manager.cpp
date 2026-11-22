// Game Profile Manager Implementation
// Game-specific profiles with per-game settings

#include "config_manager_advanced.h"
#include <fstream>
#include <algorithm>

namespace omnighost::config {

// =============================================================================
// GameProfileManager Implementation
// =============================================================================

class GameProfileManager {
public:
    explicit GameProfileManager(const std::string& profiles_dir) : profiles_dir_(profiles_dir) {
        std::filesystem::create_directories(profiles_dir_);
    }

    std::optional<GameProfile> create_profile(const std::string& name, SupportedGame game) {
        GameProfile profile;
        profile.id = generate_id();
        profile.name = name;
        profile.game = game;
        profile.metadata.created_at = std::chrono::system_clock::now();
        profile.metadata.modified_at = profile.metadata.created_at;
        profile.metadata.game = game;

        // Set game-specific defaults
        apply_game_defaults(profile);

        // Save to file
        if (!save_profile(profile)) {
            return std::nullopt;
        }

        return profile;
    }

    bool delete_profile(const std::string& profile_id) {
        std::filesystem::path profile_path = profiles_dir_ / (profile_id + ".json");
        if (std::filesystem::exists(profile_path)) {
            return std::filesystem::remove(profile_path);
        }
        return false;
    }

    std::optional<GameProfile> get_profile(const std::string& profile_id) {
        std::filesystem::path profile_path = profiles_dir_ / (profile_id + ".json");
        if (!std::filesystem::exists(profile_path)) return std::nullopt;

        std::ifstream ifs(profile_path);
        if (!ifs) return std::nullopt;

        nlohmann::json json;
        ifs >> json;

        GameProfile profile;
        from_json(json, profile);
        return profile;
    }

    std::vector<GameProfile> list_profiles(SupportedGame game = SupportedGame::None) {
        std::vector<GameProfile> profiles;

        if (!std::filesystem::exists(profiles_dir_)) return profiles;

        for (const auto& entry : std::filesystem::directory_iterator(profiles_dir_)) {
            if (entry.path().extension() == ".json") {
                std::ifstream ifs(entry.path());
                if (ifs) {
                    nlohmann::json json;
                    ifs >> json;

                    GameProfile profile;
                    from_json(json, profile);

                    if (game == SupportedGame::None || profile.game == game) {
                        profiles.push_back(profile);
                    }
                }
            }
        }

        // Sort by name
        std::sort(profiles.begin(), profiles.end(),
            [](const GameProfile& a, const GameProfile& b) {
                return a.name < b.name;
            });

        return profiles;
    }

    bool apply_profile(const std::string& profile_id) {
        auto profile = get_profile(profile_id);
        if (!profile) return false;

        // Apply to current config
        // This would merge profile.config_data into current config
        return true;
    }

    std::optional<GameProfile> get_active_profile(SupportedGame game) {
        // Return most recently used profile for game
        auto profiles = list_profiles(game);
        if (profiles.empty()) return std::nullopt;
        return profiles.front(); // Most recent
    }

    bool update_profile(const std::string& profile_id, const nlohmann::json& data) {
        auto profile = get_profile(profile_id);
        if (!profile) return false;

        profile->config_data = data;
        profile->metadata.modified_at = std::chrono::system_clock::now();
        profile->metadata.version++;

        return save_profile(*profile);
    }

    std::vector<GameProfile> search_profiles(const std::string& query, SupportedGame game = SupportedGame::None) {
        auto all = list_profiles(game);
        std::vector<GameProfile> results;

        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

        for (const auto& profile : all) {
            std::string lower_name = profile.name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

            if (lower_name.find(lower_query) != std::string::npos) {
                results.push_back(profile);
            }
        }

        return results;
    }

private:
    std::string profiles_dir_;

    std::string generate_id() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return "profile_" + std::to_string(timestamp) + "_" + std::to_string(rand());
    }

    void apply_game_defaults(GameProfile& profile) {
        switch (profile.game) {
            case SupportedGame::CS2:
                profile.aim.fov = 90.0f;
                profile.aim.smooth = 1.5f;
                profile.aim.max_distance = 200.0f;
                profile.aim.target_bones = {0, 1, 2}; // Head, neck, chest
                profile.aim.visibility_check = true;
                profile.aim.team_check = true;
                profile.esp.enabled = true;
                profile.esp.box = true;
                profile.esp.health = true;
                profile.esp.name = true;
                profile.esp.weapon = true;
                profile.esp.distance = true;
                break;

            case SupportedGame::FiveM:
                profile.aim.fov = 120.0f;
                profile.aim.smooth = 2.0f;
                profile.aim.max_distance = 300.0f;
                profile.aim.target_bones = {0, 1};
                profile.aim.visibility_check = true;
                profile.aim.team_check = false; // FiveM often no team check
                profile.esp.enabled = true;
                profile.esp.box = true;
                profile.esp.health = true;
                profile.esp.name = true;
                profile.esp.distance = true;
                profile.misc.bhop = true;
                break;

            case SupportedGame::Warzone:
                profile.aim.fov = 80.0f;
                profile.aim.smooth = 2.5f;
                profile.aim.max_distance = 400.0f;
                profile.aim.target_bones = {0, 1, 2, 3}; // Head to pelvis
                profile.aim.visibility_check = true;
                profile.aim.team_check = true;
                profile.aim.prediction = 0.1f;
                profile.esp.enabled = true;
                profile.esp.box = true;
                profile.esp.health = true;
                profile.esp.distance = true;
                profile.esp.max_distance = 500.0f;
                break;

            case SupportedGame::Apex:
                profile.aim.fov = 70.0f;
                profile.aim.smooth = 3.0f;
                profile.aim.max_distance = 300.0f;
                profile.aim.target_bones = {0, 1, 2};
                profile.aim.visibility_check = true;
                profile.aim.team_check = true;
                profile.esp.enabled = true;
                profile.esp.box = true;
                profile.esp.health = true;
                profile.esp.name = true;
                break;

            case SupportedGame::Rust:
                profile.aim.fov = 100.0f;
                profile.aim.smooth = 2.0f;
                profile.aim.max_distance = 350.0f;
                profile.aim.target_bones = {0, 1};
                profile.aim.visibility_check = true;
                profile.aim.team_check = false;
                profile.esp.enabled = true;
                profile.esp.box = true;
                profile.esp.health = true;
                profile.esp.name = true;
                profile.esp.distance = true;
                profile.misc.bhop = true;
                break;

            case SupportedGame::Fortnite:
                profile.aim.fov = 90.0f;
                profile.aim.smooth = 2.0f;
                profile.aim.max_distance = 250.0f;
                profile.aim.target_bones = {0, 1};
                profile.aim.visibility_check = true;
                profile.aim.team_check = true;
                profile.esp.enabled = true;
                profile.esp.box = true;
                profile.esp.health = true;
                profile.esp.name = true;
                break;

            case SupportedGame::Valorant:
                profile.aim.fov = 60.0f;
                profile.aim.smooth = 4.0f;
                profile.aim.max_distance = 200.0f;
                profile.aim.target_bones = {0}; // Head only
                profile.aim.visibility_check = true;
                profile.aim.team_check = true;
                profile.esp.enabled = true;
                profile.esp.box = true;
                profile.esp.health = true;
                profile.esp.name = true;
                break;

            default:
                profile.aim.fov = 90.0f;
                profile.aim.smooth = 2.0f;
                profile.aim.max_distance = 200.0f;
                profile.aim.target_bones = {0};
                profile.aim.visibility_check = true;
                profile.aim.team_check = true;
                break;
        }
    }

    bool save_profile(const GameProfile& profile) {
        std::filesystem::create_directories(profiles_dir_);
        std::filesystem::path profile_path = profiles_dir_ / (profile.id + ".json");

        nlohmann::json json;
        to_json(json, profile);

        std::ofstream ofs(profiles_dir_ / (profile.id + ".json"));
        if (!ofs) return false;

        ofs << json.dump(2);
        return true;
    }

    void to_json(nlohmann::json& j, const GameProfile& profile) {
        j = {
            {"id", profile.id},
            {"name", profile.name},
            {"game", static_cast<int>(profile.game)},
            {"config_data", profile.config_data},
            {"metadata", {
                {"id", profile.metadata.id},
                {"name", profile.metadata.name},
                {"game", static_cast<int>(profile.metadata.game)},
                {"schema", {
                    {"version", profile.metadata.schema.version},
                    {"min_compatible_version", profile.metadata.schema.min_compatible_version},
                    {"schema_json", profile.metadata.schema.schema_json},
                    {"created_at", to_iso_string(profile.metadata.schema.created_at)},
                    {"description", profile.metadata.schema.description}
                }},
                {"created_at", to_iso_string(profile.metadata.created_at)},
                {"modified_at", to_iso_string(profile.metadata.modified_at)},
                {"last_synced", to_iso_string(profile.metadata.last_synced)},
                {"size_bytes", profile.metadata.size_bytes},
                {"checksum_sha256", profile.metadata.checksum_sha256},
                {"author", profile.metadata.author},
                {"tags", profile.metadata.tags},
                {"is_favorite", profile.metadata.is_favorite},
                {"is_cloud_synced", profile.metadata.is_cloud_synced},
                {"is_backup", profile.metadata.is_backup},
                {"version", profile.metadata.version}
            }},
            {"aim", {
                {"enabled", profile.aim.enabled},
                {"fov", profile.aim.fov},
                {"smooth", profile.aim.smooth},
                {"max_distance", profile.aim.max_distance},
                {"target_bones", profile.aim.target_bones},
                {"visibility_check", profile.aim.visibility_check},
                {"team_check", profile.aim.team_check},
                {"prediction", profile.aim.prediction}
            }},
            {"esp", {
                {"enabled", profile.esp.enabled},
                {"box", profile.esp.box},
                {"skeleton", profile.esp.skeleton},
                {"health", profile.esp.health},
                {"name", profile.esp.name},
                {"weapon", profile.esp.weapon},
                {"distance", profile.esp.distance},
                {"max_distance", profile.esp.max_distance},
                {"enemy_color", {
                    {"r", profile.esp.enemy_color.r},
                    {"g", profile.esp.enemy_color.g},
                    {"b", profile.esp.enemy_color.b},
                    {"a", profile.esp.enemy_color.a}
                }},
                {"friendly_color", {
                    {"r", profile.esp.friendly_color.r},
                    {"g", profile.esp.friendly_color.g},
                    {"b", profile.esp.friendly_color.b},
                    {"a", profile.esp.friendly_color.a}
                }}
            }},
            {"misc", {
                {"bhop", profile.misc.bhop},
                {"triggerbot", profile.misc.triggerbot},
                {"radar", profile.misc.radar},
                {"no_flash", profile.misc.no_flash},
                {"no_smoke", profile.misc.no_smoke},
                {"third_person", profile.misc.third_person},
                {"fov_override", profile.misc.fov_override}
            }}
        };
    }

    void from_json(const nlohmann::json& j, GameProfile& profile) {
        profile.id = j.value("id", "");
        profile.name = j.value("name", "");
        profile.game = static_cast<SupportedGame>(j.value("game", 0));
        profile.config_data = j.value("config_data", nlohmann::json{});

        auto meta = j.value("metadata", nlohmann::json{});
        profile.metadata.id = meta.value("id", "");
        profile.metadata.name = meta.value("name", "");
        profile.metadata.game = static_cast<SupportedGame>(meta.value("game", 0));

        auto schema = meta.value("schema", nlohmann::json{});
        profile.metadata.schema.version = schema.value("version", 1);
        profile.metadata.schema.min_compatible_version = schema.value("min_compatible_version", 1);
        profile.metadata.schema.schema_json = schema.value("schema_json", "");
        profile.metadata.schema.created_at = from_iso_string(schema.value("created_at", ""));
        profile.metadata.schema.description = schema.value("description", "");

        profile.metadata.created_at = from_iso_string(meta.value("created_at", ""));
        profile.metadata.modified_at = from_iso_string(meta.value("modified_at", ""));
        profile.metadata.last_synced = from_iso_string(meta.value("last_synced", ""));
        profile.metadata.size_bytes = meta.value("size_bytes", 0);
        profile.metadata.checksum_sha256 = meta.value("checksum_sha256", "");
        profile.metadata.author = meta.value("author", "");
        profile.metadata.tags = meta.value("tags", std::vector<std::string>{});
        profile.metadata.is_favorite = meta.value("is_favorite", false);
        profile.metadata.is_cloud_synced = meta.value("is_cloud_synced", false);
        profile.metadata.is_backup = meta.value("is_backup", false);
        profile.metadata.version = meta.value("version", 1);

        auto aim = j.value("aim", nlohmann::json{});
        profile.aim.enabled = aim.value("enabled", false);
        profile.aim.fov = aim.value("fov", 90.0f);
        profile.aim.smooth = aim.value("smooth", 1.0f);
        profile.aim.max_distance = aim.value("max_distance", 200.0f);
        profile.aim.target_bones = aim.value("target_bones", std::vector<int>{0});
        profile.aim.visibility_check = aim.value("visibility_check", true);
        profile.aim.team_check = aim.value("team_check", true);
        profile.aim.prediction = aim.value("prediction", 0.0f);

        auto esp = j.value("esp", nlohmann::json{});
        profile.esp.enabled = esp.value("enabled", false);
        profile.esp.box = esp.value("box", true);
        profile.esp.skeleton = esp.value("skeleton", false);
        profile.esp.health = esp.value("health", true);
        profile.esp.name = esp.value("name", true);
        profile.esp.weapon = esp.value("weapon", false);
        profile.esp.distance = esp.value("distance", false);
        profile.esp.max_distance = esp.value("max_distance", 300.0f);

        auto ec = esp.value("enemy_color", nlohmann::json{});
        profile.esp.enemy_color.r = ec.value("r", 1.0f);
        profile.esp.enemy_color.g = ec.value("g", 0.0f);
        profile.esp.enemy_color.b = ec.value("b", 0.0f);
        profile.esp.enemy_color.a = ec.value("a", 1.0f);

        auto fc = esp.value("friendly_color", nlohmann::json{});
        profile.esp.friendly_color.r = fc.value("r", 0.0f);
        profile.esp.friendly_color.g = fc.value("g", 1.0f);
        profile.esp.friendly_color.b = fc.value("b", 0.0f);
        profile.esp.friendly_color.a = fc.value("a", 1.0f);

        auto misc = j.value("misc", nlohmann::json{});
        profile.misc.bhop = misc.value("bhop", false);
        profile.misc.triggerbot = misc.value("triggerbot", false);
        profile.misc.radar = misc.value("radar", false);
        profile.misc.no_flash = misc.value("no_flash", false);
        profile.misc.no_smoke = misc.value("no_smoke", false);
        profile.misc.third_person = misc.value("third_person", false);
        profile.misc.fov_override = misc.value("fov_override", 0.0f);
    }

    std::string to_iso_string(std::chrono::system_clock::time_point tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }

    std::chrono::system_clock::time_point from_iso_string(const std::string& str) {
        std::tm tm = {};
        std::stringstream ss(str);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
};

} // namespace omnighost::config