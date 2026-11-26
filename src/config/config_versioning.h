#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <mutex>

namespace OmniGhost::Config::Versioning {

// Versão atual do schema de configuração
inline constexpr uint32_t CURRENT_SCHEMA_VERSION = 12;

// Estrutura de migração: de versão X para X+1
struct Migration {
    uint32_t from_version;
    uint32_t to_version;
    std::function<void(nlohmann::json&)> migrate_fn;
    std::string description;
};

// Regista uma migração (chamar no startup, antes de carregar config)
void RegisterMigration(const Migration& migration);

// Executa todas as migrações necessárias para trazer o JSON à versão atual
// Retorna true se migrou com sucesso, false se versão desconhecida/erro
[[nodiscard]] bool MigrateToCurrent(nlohmann::json& config_json);

// Verifica se config precisa de migração
[[nodiscard]] bool NeedsMigration(const nlohmann::json& config_json);

// Obtém versão do schema do JSON (0 se não tem campo version)
[[nodiscard]] uint32_t GetSchemaVersion(const nlohmann::json& config_json);

// Define versão no JSON
void SetSchemaVersion(nlohmann::json& config_json, uint32_t version);

// Callback opcional para log de migrações
using MigrationLogCallback = std::function<void(uint32_t from, uint32_t to, const std::string& desc)>;
void SetMigrationLogCallback(MigrationLogCallback cb);

// Helpers comuns de migração
namespace Helpers {
    // Renomeia chave
    void RenameKey(nlohmann::json& j, std::string_view old_key, std::string_view new_key);
    // Move valor de path antigo para novo (ex: "aim.fov" -> "aimbot.field_of_view")
    void MoveValue(nlohmann::json& j, std::string_view from_path, std::string_view to_path);
    // Define valor default se chave não existe
    void SetDefaultIfMissing(nlohmann::json& j, std::string_view key, const nlohmann::json& default_value);
    // Remove chave obsoleta
    void RemoveKey(nlohmann::json& j, std::string_view key);
    // Converte array de objetos para novo formato
    void TransformArray(nlohmann::json& j, std::string_view key,
                        std::function<nlohmann::json(const nlohmann::json&)> transformer);
}

} // namespace OmniGhost::Config::Versioning