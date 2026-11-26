#include "config_versioning.h"
#include <algorithm>
#include <iostream>

namespace OmniGhost::Config::Versioning {

namespace {

std::vector<Migration> g_migrations;
std::mutex g_mutex;
MigrationLogCallback g_log_callback = nullptr;

void LogMigration(uint32_t from, uint32_t to, const std::string& desc) {
    if (g_log_callback) g_log_callback(from, to, desc);
    // Debug output
    std::cout << "[Config Migration] v" << from << " -> v" << to << ": " << desc << std::endl;
}

} // anonymous namespace

void RegisterMigration(const Migration& migration) {
    std::lock_guard lock(g_mutex);
    // Insere ordenado por from_version
    auto it = std::lower_bound(g_migrations.begin(), g_migrations.end(), migration.from_version,
                               [](const Migration& m, uint32_t v) { return m.from_version < v; });
    g_migrations.insert(it, migration);
}

bool MigrateToCurrent(nlohmann::json& config_json) {
    std::lock_guard lock(g_mutex);

    uint32_t current = GetSchemaVersion(config_json);
    if (current == CURRENT_SCHEMA_VERSION) return true;
    if (current > CURRENT_SCHEMA_VERSION) {
        // Config do futuro - não sabemos migrar para baixo
        std::cerr << "[Config Migration] Warning: config version " << current
                  << " > current schema " << CURRENT_SCHEMA_VERSION << std::endl;
        return false;
    }

    // Aplica migrações sequenciais
    for (uint32_t v = current; v < CURRENT_SCHEMA_VERSION; ++v) {
        auto it = std::find_if(g_migrations.begin(), g_migrations.end(),
                               [v](const Migration& m) { return m.from_version == v; });
        if (it == g_migrations.end()) {
            std::cerr << "[Config Migration] Error: no migration from v" << v << " to v" << (v + 1) << std::endl;
            return false;
        }

        try {
            it->migrate_fn(config_json);
            LogMigration(v, v + 1, it->description);
        } catch (const std::exception& e) {
            std::cerr << "[Config Migration] Exception migrating v" << v << " -> v" << (v + 1)
                      << ": " << e.what() << std::endl;
            return false;
        }
    }

    SetSchemaVersion(config_json, CURRENT_SCHEMA_VERSION);
    return true;
}

bool NeedsMigration(const nlohmann::json& config_json) {
    return GetSchemaVersion(config_json) != CURRENT_SCHEMA_VERSION;
}

uint32_t GetSchemaVersion(const nlohmann::json& config_json) {
    if (config_json.contains("schema_version") && config_json["schema_version"].is_number_unsigned()) {
        return config_json["schema_version"].get<uint32_t>();
    }
    // Legado: tenta ler "version" ou "config_version"
    if (config_json.contains("version") && config_json["version"].is_number_unsigned()) {
        return config_json["version"].get<uint32_t>();
    }
    if (config_json.contains("config_version") && config_json["config_version"].is_number_unsigned()) {
        return config_json["config_version"].get<uint32_t>();
    }
    return 0; // sem versão = v0 (legacy)
}

void SetSchemaVersion(nlohmann::json& config_json, uint32_t version) {
    config_json["schema_version"] = version;
    // Remove campos legados
    config_json.erase("version");
    config_json.erase("config_version");
}

void SetMigrationLogCallback(MigrationLogCallback cb) {
    g_log_callback = std::move(cb);
}

// ===== Helpers =====

namespace Helpers {

void RenameKey(nlohmann::json& j, std::string_view old_key, std::string_view new_key) {
    if (j.contains(old_key)) {
        j[new_key] = std::move(j[old_key]);
        j.erase(old_key);
    }
}

void MoveValue(nlohmann::json& j, std::string_view from_path, std::string_view to_path) {
    // Parse path como "parent.child.value"
    auto get_ptr = [&](nlohmann::json& root, std::string_view path, bool create_missing) -> nlohmann::json* {
        nlohmann::json* cur = &root;
        size_t start = 0;
        while (start < path.size()) {
            size_t dot = path.find('.', start);
            std::string_view key = path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
            if (cur->is_object() && cur->contains(key)) {
                cur = &(*cur)[key];
            } else if (create_missing) {
                (*cur)[key] = nlohmann::json::object();
                cur = &(*cur)[key];
            } else {
                return nullptr;
            }
            if (dot == std::string_view::npos) break;
            start = dot + 1;
        }
        return cur;
    };

    // Extrai valor final do from_path
    nlohmann::json* from_parent = nullptr;
    std::string from_key;
    {
        size_t last_dot = from_path.rfind('.');
        if (last_dot == std::string_view::npos) {
            from_parent = &j;
            from_key = std::string(from_path);
        } else {
            std::string_view parent_path = from_path.substr(0, last_dot);
            from_key = std::string(from_path.substr(last_dot + 1));
            from_parent = get_ptr(j, parent_path, false);
        }
    }

    if (!from_parent || !from_parent->contains(from_key)) return;

    nlohmann::json value = std::move((*from_parent)[from_key]);
    from_parent->erase(from_key);

    // Insere no to_path
    nlohmann::json* to_parent = nullptr;
    std::string to_key;
    {
        size_t last_dot = to_path.rfind('.');
        if (last_dot == std::string_view::npos) {
            to_parent = &j;
            to_key = std::string(to_path);
        } else {
            std::string_view parent_path = to_path.substr(0, last_dot);
            to_key = std::string(to_path.substr(last_dot + 1));
            to_parent = get_ptr(j, parent_path, true);
        }
    }

    if (to_parent) {
        (*to_parent)[to_key] = std::move(value);
    }
}

void SetDefaultIfMissing(nlohmann::json& j, std::string_view key, const nlohmann::json& default_value) {
    if (!j.contains(key)) {
        j[key] = default_value;
    }
}

void RemoveKey(nlohmann::json& j, std::string_view key) {
    j.erase(key);
}

void TransformArray(nlohmann::json& j, std::string_view key,
                    std::function<nlohmann::json(const nlohmann::json&)> transformer) {
    if (j.contains(key) && j[key].is_array()) {
        nlohmann::json& arr = j[key];
        for (auto& item : arr) {
            item = transformer(item);
        }
    }
}

} // namespace Helpers

} // namespace OmniGhost::Config::Versioning