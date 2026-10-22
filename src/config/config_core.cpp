#include "app_settings.h"
#include <Windows.h>
#include <shellapi.h>
#include "config_core.h"
#include "../globals.h"
#include "../window/theme.h"
#include "../platform/text_encoding.h"
#include "../platform/unique_handle.h"
#include "../platform/build_info.h"
#include "../platform/file_integrity.h"
#include "../updater/json.h"
#include "../licensing/license_service.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <objbase.h>
#include <windows.h>
#include <iomanip>
#include <map>
#include <fstream>
#include <future>
#include <sstream>
#include <filesystem>
#include <initializer_list>

namespace fs = std::filesystem;

namespace config_manager {


std::vector<SavedConfigInfo> saved_list;
char new_config_name[64] = "MinhaConfig";
std::string last_status = "Pronto";
std::string active_config_name;
std::string active_profile_name = "Default";
std::string active_config_id;
std::vector<CommunityConfigInfo> community_configs;


std::string ConfigDir();

static fs::path ConfigStatePath() { return fs::path(ConfigDir()) / "config_state.ini"; }
static fs::path CommunityBackupPath() { return fs::path(ConfigDir()) / "community-apply-backup.ogcfg"; }
bool WriteTextAtomically(const fs::path& destination, const std::string& data, std::string& error);

std::string NewConfigId() {
    GUID guid{};
    if (CoCreateGuid(&guid) != S_OK) return {};
    wchar_t buffer[64]{};
    if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) <= 0) return {};
    std::wstring value(buffer);
    if (!value.empty() && value.front() == L'{') value.erase(value.begin());
    if (!value.empty() && value.back() == L'}') value.pop_back();
    return OmniGhost::Platform::WideToUtf8(value);
}

static std::string ReadState(const std::string& key) {
    std::ifstream f(ConfigStatePath());
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos != std::string::npos && line.substr(0, pos) == key) return line.substr(pos + 1);
    }
    return {};
}

static bool WriteState(std::initializer_list<std::pair<std::string, std::string>> updates) {
    EnsureConfigDir();
    std::map<std::string, std::string> values;
    std::ifstream f(ConfigStatePath());
    std::error_code existsError;
    if (!f && fs::exists(ConfigStatePath(), existsError)) {
        last_status = "Falha ao ler estado da configuração";
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos != std::string::npos) values[line.substr(0, pos)] = line.substr(pos + 1);
    }
    if (f.bad() || existsError) {
        last_status = "Falha ao ler estado da configuração";
        return false;
    }
    f.close();
    for (const auto& [key, value] : updates) values[key] = value;
    std::ostringstream serialized;
    for (const auto& [k, v] : values) serialized << k << "=" << v << '\n';
    std::string error;
    if (!WriteTextAtomically(ConfigStatePath(), serialized.str(), error)) {
        last_status = "Falha ao guardar estado da configuração: " + error;
        return false;
    }
    return true;
}


std::string CurrentAppVersion() {
    return OmniGhost::BuildInfo::AppVersion;
}

bool FlushPathToDisk(const fs::path& path) {
    OmniGhost::Platform::UniqueHandle handle(CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    return handle && FlushFileBuffers(handle.get()) == TRUE;
}

bool WriteTextAtomically(const fs::path& destination, const std::string& data, std::string& error) {
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        error = "create_directories failed: " + ec.message();
        return false;
    }

    const fs::path temporary = destination.wstring() + L".tmp";
    const fs::path backup = destination.wstring() + L".bak";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "unable to create temporary config";
            return false;
        }
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream.good()) {
            error = "write/flush failed";
            stream.close();
            fs::remove(temporary, ec);
            return false;
        }
    }

    if (!FlushPathToDisk(temporary)) {
        error = "FlushFileBuffers failed win32=" + std::to_string(GetLastError());
        fs::remove(temporary, ec);
        return false;
    }

    BOOL promoted = FALSE;
    if (fs::exists(destination, ec) && !ec) {
        promoted = ReplaceFileW(destination.c_str(), temporary.c_str(), backup.c_str(),
            REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
    } else {
        ec.clear();
        promoted = MoveFileExW(temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    if (!promoted) {
        const DWORD win32 = GetLastError();
        error = "atomic replace failed win32=" + std::to_string(win32);
        return false;
    }

    return true;
}

bool ParseLine(const std::string& line, std::string& key, std::string& val) {
    auto pos = line.find('=');
    if (pos == std::string::npos) return false;
    key = line.substr(0, pos);
    val = line.substr(pos + 1);
    return true;
}


void WBool(std::ostringstream& o, const char* k, bool v) { o << k << "=" << (v ? 1 : 0) << "\n"; }


void WFloat(std::ostringstream& o, const char* k, float v) { o << k << "=" << std::setprecision(9) << v << "\n"; }
void WInt(std::ostringstream& o, const char* k, int v) { o << k << "=" << v << "\n"; }
void WU32(std::ostringstream& o, const char* k, ImU32 v) { o << k << "=" << v << "\n"; }


void WStr(std::ostringstream& o, const char* k, const std::string& v) {
    std::string safe = v;
    for (char& c : safe) {
        if (c == '\r' || c == '\n')
            c = ' ';
    }
    o << k << "=" << safe << "\n";
}


bool toB(const std::string& val) { return val == "1" || val == "true"; }
float toF(const std::string& val) {
    char* end = nullptr;
    const float result = std::strtof(val.c_str(), &end);
    if (end == val.c_str() || *end != '\0')
        return 0.0f;


    if (!std::isfinite(result))
        return 0.0f;
    return result;
}
int toI(const std::string& val) {
    char* end = nullptr;
    const long result = std::strtol(val.c_str(), &end, 10);
    return (end != val.c_str() && *end == '\0' && result >= INT_MIN && result <= INT_MAX) ? static_cast<int>(result) : 0;
}
ImU32 toU(const std::string& val) {
    char* end = nullptr;
    const unsigned long result = std::strtoul(val.c_str(), &end, 10);
    return (end != val.c_str() && *end == '\0' && result <= UINT32_MAX) ? static_cast<ImU32>(result) : 0u;
}


std::string ConfigDir() {
    return OmniGhost::Paths::Configs().string();
}

void EnsureConfigDir() {
    std::error_code ec;
    fs::create_directories(ConfigDir(), ec);
}

void MigrateLegacyConfigs() {
    EnsureConfigDir();
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(ConfigDir(), ec)) {
        if (ec || !entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ogcfg") continue;
        std::string stem = entry.path().stem().string();
        if (stem.rfind("cs2_", 0) == 0 || stem.rfind("fivem_", 0) == 0 ||
            stem.rfind("rust_", 0) == 0 || stem.rfind("warzone_", 0) == 0 ||
            stem.rfind("fortnite_", 0) == 0)
            continue;


        std::string meta;
        {
            std::ifstream f(entry.path());
            std::string line;
            while (std::getline(f, line)) {
                if (line.rfind("meta.game=", 0) == 0) {
                    meta = line.substr(10);
                    while (!meta.empty() && (meta.back()=='\r' || meta.back()=='\n' || meta.back()==' '))
                        meta.pop_back();
                    break;
                }
            }
        }
        std::string prefix = "fivem_";
        if (meta == "cs2") prefix = "cs2_";
        else if (meta == "rust") prefix = "rust_";
        else if (meta == "warzone") prefix = "warzone_";
        else if (meta == "fortnite") prefix = "fortnite_";
        else {

            std::ifstream f2(entry.path());
            std::string whole((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());
            if (whole.find("cs2.") != std::string::npos && whole.find("esp.enabled") == std::string::npos)
                prefix = "cs2_";
            else if (whole.find("rust.") != std::string::npos && whole.find("esp.enabled") == std::string::npos)
                prefix = "rust_";
            else if (whole.find("warzone.") != std::string::npos)
                prefix = "warzone_";
        }
        fs::path dest = fs::path(ConfigDir()) / (prefix + stem + ".ogcfg");
        if (!fs::exists(dest, ec)) {
            fs::rename(entry.path(), dest, ec);
            if (ec) {
                ec.clear();
                if (fs::copy_file(entry.path(), dest, fs::copy_options::none, ec) && !ec) {
                    std::error_code removeError;
                    fs::remove(entry.path(), removeError);
                }
            }
        }
    }
}


void RefreshList() {
    saved_list.clear();
    EnsureConfigDir();
    std::string dir = ConfigDir();
    if (!fs::exists(dir)) return;

    struct Entry {
        SavedConfigInfo info;
        fs::file_time_type ftime{};
    };
    std::vector<Entry> entries;

    const std::string favoriteName = ReadState(std::string("favorite.") + ActiveGameMeta());
    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ogcfg") continue;
        Entry e;
        e.info.name = entry.path().stem().string();
        if (!NameBelongsToActiveGame(e.info.name))
            continue;
        e.info.path = entry.path().string();
        e.info.favorite = e.info.name == favoriteName;
        e.ftime = fs::last_write_time(entry);

        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            e.ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
        std::tm tm{};
        localtime_s(&tm, &tt);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &tm);
        e.info.date_str = buf;
        entries.push_back(std::move(e));
    }


    std::vector<std::string> orderNames;
    {
        std::ifstream of((fs::path(dir) / "order.txt").string());
        std::string line;
        while (of && std::getline(of, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#')
                orderNames.push_back(line);
        }
    }

    auto findIdx = [&](const std::string& n) -> int {
        for (int i = 0; i < (int)orderNames.size(); ++i)
            if (orderNames[i] == n) return i;
        return 100000;
    };

    std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        const bool aAuto = a.info.name.rfind("_autosave", 0) == 0;
        const bool bAuto = b.info.name.rfind("_autosave", 0) == 0;
        if (aAuto != bAuto) return !aAuto;
        int ia = findIdx(a.info.name), ib = findIdx(b.info.name);
        if (ia != ib) return ia < ib;
        return a.ftime > b.ftime;
    });

    saved_list.reserve(entries.size());
    for (auto& e : entries)
        saved_list.push_back(std::move(e.info));
}

void SaveOrder() {
    EnsureConfigDir();
    std::ofstream of((fs::path(ConfigDir()) / "order.txt").string(), std::ios::trunc);
    if (!of) return;
    of << "# OmniGhost config order — first line = startup config\n";
    for (const auto& s : saved_list) {
        if (s.name.rfind("_autosave", 0) == 0) continue;
        of << s.name << "\n";
    }
}


const char* ActiveGamePrefix() {
    switch (g_activeGame) {
    case ActiveGame::CS2: return "cs2_";
    case ActiveGame::Warzone: return "warzone_";



    case ActiveGame::Fortnite: return "fortnite_";
    default: return "fivem_";
    }
}

const char* ActiveGameMeta() {
    switch (g_activeGame) {
    case ActiveGame::CS2: return "cs2";
    case ActiveGame::Warzone: return "warzone";


    case ActiveGame::Fortnite: return "fortnite";
    default: return "fivem";
    }
}

bool NameBelongsToActiveGame(const std::string& name) {
    const char* p = ActiveGamePrefix();
    return name.rfind(p, 0) == 0;
}


std::string SanitizeFileName(std::string name) {
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
        name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '.'))
        name.pop_back();
    for (char& c : name) {
        const unsigned char value = static_cast<unsigned char>(c);
        if (value < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
            c == '\\' || c == '|' || c == '?' || c == '*')
            c = '_';
    }
    constexpr size_t kMaximumConfigName = 96;
    if (name.size() > kMaximumConfigName)
        name.resize(kMaximumConfigName);
    while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
        name.pop_back();
    if (name.empty()) name = "config";
    return name;
}

bool IsSafeConfigIdentifier(const std::string& name) {
    return !name.empty() && name == SanitizeFileName(name) &&
           name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
           name != "." && name != "..";
}

bool SaveToFile(const std::string& name) {
    std::string safe = SanitizeFileName(name);
    if (safe.empty()) { last_status = "Nome vazio"; return false; }
    const std::string prefix = ActiveGamePrefix();
    if (safe.rfind(prefix, 0) != 0) {
        if (safe.rfind("cs2_", 0) == 0) safe = safe.substr(4);
        else if (safe.rfind("fivem_", 0) == 0) safe = safe.substr(6);
        else if (safe.rfind("rust_", 0) == 0) safe = safe.substr(5);
        else if (safe.rfind("fortnite_", 0) == 0) safe = safe.substr(9);
        safe = prefix + safe;
    }
    EnsureConfigDir();
    const fs::path path = fs::path(ConfigDir()) / (safe + ".ogcfg");
    std::string error;
    if (!WriteTextAtomically(path, SerializeAll(), error)) {
        last_status = "Erro ao guardar atomicamente: " + error;
        return false;
    }
    active_config_name = safe;
    if (!WriteState({{std::string("active.") + ActiveGameMeta(), active_config_name},
                    {std::string("active_id.") + ActiveGameMeta(), active_config_id}})) return false;
    last_status = "Guardado: Configs/" + safe + ".ogcfg";
    RefreshList();
    return true;
}

bool LoadFromFile(const std::string& name) {
    if (!IsSafeConfigIdentifier(name)) {
        last_status = "Nome de config invalido";
        return false;
    }
    const fs::path path = fs::path(ConfigDir()) / (name + ".ogcfg");
    std::error_code sizeEc;
    const auto size = fs::file_size(path, sizeEc);
    if (sizeEc) { last_status = "Ficheiro nao encontrado"; return false; }
    if (size > kMaxConfigBytes) { last_status = "Config demasiado grande"; return false; }

    std::ifstream f(path, std::ios::binary);
    if (!f) { last_status = "Ficheiro nao encontrado"; return false; }
    std::ostringstream ss;
    ss << f.rdbuf();
    bool ok = DeserializeAll(ss.str());
    if (ok) {
        active_config_name = name;
        if (!WriteState({{std::string("active.") + ActiveGameMeta(), active_config_name},
                        {std::string("active_id.") + ActiveGameMeta(), active_config_id}})) {

            last_status = "Config carregada, mas " + last_status;
            return true;
        }
        last_status = "Carregado: " + name + " (ativa)";
    } else {
        std::error_code copyEc;
        fs::copy_file(path, path.wstring() + L".bad", fs::copy_options::overwrite_existing, copyEc);
        if (last_status.empty() || last_status == "Pronto")
            last_status = "Erro ao carregar";
    }
    return ok;
}

bool DeleteConfigFile(const std::string& name) {
    if (!IsSafeConfigIdentifier(name)) {
        last_status = "Nome de config invalido";
        return false;
    }
    const fs::path path = fs::path(ConfigDir()) / (name + ".ogcfg");
    std::error_code ec;
    fs::remove(path, ec);
    if (!ec && IsFavoriteConfig(name) &&
        !WriteState({{std::string("favorite.") + ActiveGameMeta(), ""}})) {
        RefreshList();
        return false;
    }
    last_status = ec ? "Erro ao apagar" : ("Apagado: " + name);
    RefreshList();
    return !ec;
}

bool DuplicateConfigFile(const std::string& name, const std::string& new_name) {
    if (!IsSafeConfigIdentifier(name) || !NameBelongsToActiveGame(name)) {
        last_status = "Config de origem inválida";
        return false;
    }
    std::string safe = SanitizeFileName(new_name);
    const std::string prefix = ActiveGamePrefix();
    if (safe.rfind(prefix, 0) != 0) safe = prefix + safe;
    if (!IsSafeConfigIdentifier(safe)) { last_status = "Nome de destino inválido"; return false; }
    const fs::path source = fs::path(ConfigDir()) / (name + ".ogcfg");
    const fs::path destination = fs::path(ConfigDir()) / (safe + ".ogcfg");
    std::error_code ec;
    if (!fs::exists(source, ec) || ec || fs::exists(destination, ec)) {
        last_status = fs::exists(destination, ec) ? "Já existe uma config com esse nome" : "Config de origem não encontrada";
        return false;
    }
    std::ifstream input(source, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.good() && !input.eof()) { last_status = "Falha ao ler config de origem"; return false; }
    std::string error;
    if (!WriteTextAtomically(destination, contents.str(), error)) {
        last_status = "Falha ao duplicar config: " + error;
        return false;
    }
    last_status = "Config duplicada: " + safe;
    RefreshList();
    return true;
}

bool RenameConfigFile(const std::string& name, const std::string& new_name) {
    if (!DuplicateConfigFile(name, new_name)) return false;
    const std::string oldActive = active_config_name;
    std::string safe = SanitizeFileName(new_name);
    const std::string prefix = ActiveGamePrefix();
    if (safe.rfind(prefix, 0) != 0) safe = prefix + safe;
    if (!DeleteConfigFile(name)) {
        std::error_code cleanup;
        fs::remove(fs::path(ConfigDir()) / (safe + ".ogcfg"), cleanup);
        RefreshList();
        return false;
    }
    if (oldActive == name) {
        active_config_name = safe;
        WriteState({{std::string("active.") + ActiveGameMeta(), safe}});
    }
    last_status = "Config renomeada: " + safe;
    RefreshList();
    return true;
}


static std::string EscapeJsonString(std::string_view input) {
    std::string output;
    output.reserve(input.size() + 32);
    static constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : input) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (c < 0x20) {
                output += "\\u00";
                output.push_back(hex[(c >> 4) & 0x0f]);
                output.push_back(hex[c & 0x0f]);
            } else output.push_back(static_cast<char>(c));
        }
    }
    return output;
}

static std::string UrlEncode(std::string_view input) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    for (const unsigned char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') output.push_back(static_cast<char>(c));
        else {
            output.push_back('%');
            output.push_back(hex[(c >> 4) & 0x0f]);
            output.push_back(hex[c & 0x0f]);
        }
    }
    return output;
}

static bool CloudResponseOk(const OmniGhost::Auth::LauncherApiResponse& response,
                            const char* operation) {
    if (response.statusCode >= 200 && response.statusCode < 300 && response.error.empty())
        return true;
    last_status = std::string(operation) + " falhou";
    if (response.statusCode == 401) last_status += ": inicia sessão com a conta OmniGhost";
    else if (response.statusCode == 409) last_status += ": conflito de versão; descarrega antes de substituir";
    else if (!response.error.empty()) last_status += ": " + response.error;
    else last_status += " (HTTP " + std::to_string(response.statusCode) + ")";
    return false;
}

bool UploadCloudConfig(const std::string& name) {
    std::string safe = SanitizeFileName(name.empty() ? active_config_name : name);
    if (safe.empty()) { last_status = "Nome cloud inválido"; return false; }
    uint64_t expectedRevision = 0;
    const std::string lookupPath = "/api/launcher/configs?game=" + UrlEncode(ActiveGameMeta()) +
        "&name=" + UrlEncode(safe);
    const auto current = OmniGhost::Licensing::WebsiteRequest("GET", lookupPath);
    if (current.statusCode == 200 && current.error.empty()) {
        OmniGhost::Update::Json::Value root;
        OmniGhost::Update::Json::Limits limits{12, 300u * 1024u, 2000};
        if (!OmniGhost::Update::Json::Parse(current.body, root, limits).ok) {
            last_status = "Estado cloud inválido";
            return false;
        }
        const auto* object = OmniGhost::Update::Json::AsObject(root);
        if (!object) { last_status = "Estado cloud inválido"; return false; }
        const auto revisionIt = object->find("revision");
        if (revisionIt == object->end()) { last_status = "Revisão cloud ausente"; return false; }
        uint64_t revision = 0;
        if (!OmniGhost::Update::Json::AsUInt64(revisionIt->second, revision) || revision < 1) {
            last_status = "Revisão cloud inválida";
            return false;
        }
        expectedRevision = revision;
    } else if (current.statusCode != 404) {
        if (!CloudResponseOk(current, "Consulta cloud")) return false;
    }
    std::ostringstream body;
    body << "{\"name\":\"" << EscapeJsonString(safe) << "\",\"game\":\""
         << EscapeJsonString(ActiveGameMeta()) << "\",\"schemaVersion\":"
         << kConfigSchemaVersion << ",\"expectedRevision\":" << expectedRevision
         << ",\"payload\":\"" << EscapeJsonString(SerializeAll()) << "\"}";
    if (body.str().size() > 256u * 1024u) { last_status = "Config demasiado grande para a cloud"; return false; }
    const auto response = OmniGhost::Licensing::WebsiteRequest("POST", "/api/launcher/configs", body.str());
    if (!CloudResponseOk(response, "Upload cloud")) return false;
    last_status = "Config guardada na cloud: " + safe;
    return true;
}

bool DownloadCloudConfig(const std::string& name) {
    const std::string safe = SanitizeFileName(name.empty() ? active_config_name : name);
    const std::string path = "/api/launcher/configs?game=" + UrlEncode(ActiveGameMeta()) +
        "&name=" + UrlEncode(safe);
    const auto response = OmniGhost::Licensing::WebsiteRequest("GET", path);
    if (!CloudResponseOk(response, "Download cloud")) return false;
    OmniGhost::Update::Json::Value root;
    OmniGhost::Update::Json::Limits limits{20, 300u * 1024u, 5000};
    if (!OmniGhost::Update::Json::Parse(response.body, root, limits).ok) {
        last_status = "Resposta cloud inválida";
        return false;
    }
    const auto* object = OmniGhost::Update::Json::AsObject(root);
    if (!object) { last_status = "Resposta cloud inválida"; return false; }
    const auto payloadIt = object->find("payload");
    if (payloadIt == object->end()) { last_status = "Payload cloud ausente"; return false; }
    const std::string* payload = OmniGhost::Update::Json::AsString(payloadIt->second);
    if (!payload || payload->size() > kMaxConfigBytes || !DeserializeAll(*payload)) {
        last_status = "Config cloud rejeitada pela validação";
        return false;
    }
    last_status = "Config carregada da cloud: " + safe;
    return true;
}

bool DeleteCloudConfig(const std::string& name) {
    const std::string safe = SanitizeFileName(name.empty() ? active_config_name : name);
    const std::string path = "/api/launcher/configs?game=" + UrlEncode(ActiveGameMeta()) +
        "&name=" + UrlEncode(safe);
    const auto response = OmniGhost::Licensing::WebsiteRequest("DELETE", path);
    if (!CloudResponseOk(response, "Remoção cloud")) return false;
    last_status = "Config removida da cloud: " + safe;
    return true;
}

static const OmniGhost::Update::Json::Object* ParseCloudObject(
    const OmniGhost::Auth::LauncherApiResponse& response,
    OmniGhost::Update::Json::Value& root, const char* operation) {
    if (!CloudResponseOk(response, operation)) return nullptr;
    OmniGhost::Update::Json::Limits limits{24, 400u * 1024u, 10000};
    const auto parsed = OmniGhost::Update::Json::Parse(response.body, root, limits);
    if (!parsed.ok) {
        last_status = std::string(operation) + ": resposta inválida";
        return nullptr;
    }
    const auto* object = OmniGhost::Update::Json::AsObject(root);
    if (!object) last_status = std::string(operation) + ": objeto ausente";
    return object;
}

static std::string JsonString(const OmniGhost::Update::Json::Object& object, std::string_view key) {
    const auto* value = OmniGhost::Update::Json::Field(object, key);
    const auto* text = value ? OmniGhost::Update::Json::AsString(*value) : nullptr;
    return text ? *text : std::string{};
}

static bool JsonBool(const OmniGhost::Update::Json::Object& object, std::string_view key) {
    const auto* value = OmniGhost::Update::Json::Field(object, key);
    const auto* boolean = value ? OmniGhost::Update::Json::AsBool(*value) : nullptr;
    return boolean && *boolean;
}

static int JsonInt(const OmniGhost::Update::Json::Object& object, std::string_view key) {
    const auto* value = OmniGhost::Update::Json::Field(object, key);
    std::int64_t number = 0;
    if (!value || !OmniGhost::Update::Json::AsInt64(*value, number)) {
        std::uint64_t unsignedNumber = 0;
        if (!value || !OmniGhost::Update::Json::AsUInt64(*value, unsignedNumber)) return 0;
        number = static_cast<std::int64_t>((std::min<std::uint64_t>)(unsignedNumber, INT_MAX));
    }
    return static_cast<int>(std::clamp<std::int64_t>(number, INT_MIN, INT_MAX));
}

static double JsonDouble(const OmniGhost::Update::Json::Object& object, std::string_view key) {
    const auto* value = OmniGhost::Update::Json::Field(object, key);
    double number = 0.0;
    return value && OmniGhost::Update::Json::AsDouble(*value, number) ? number : 0.0;
}

const std::vector<CommunityConfigInfo>& CommunityConfigs() { return community_configs; }

bool RefreshCommunityConfigs(CommunityScope scope, const std::string& search, const std::string& sort) {
    return RefreshCommunityConfigsForGame(ActiveGameMeta(), scope, search, sort);
}

bool RefreshCommunityConfigsForGame(const std::string& game, CommunityScope scope,
                                    const std::string& search, const std::string& sort) {
    if (game != "fivem" && game != "cs2" && game != "warzone" && game != "fortnite") {
        last_status = "Jogo inválido no Config Hub"; return false;
    }
    const char* scopeName = scope == CommunityScope::Marketplace ? "marketplace"
        : scope == CommunityScope::Mine ? "mine"
        : scope == CommunityScope::Favorites ? "favorites"
        : scope == CommunityScope::Downloads ? "downloads" : "community";
    std::string path = "/api/launcher/community-configs?game=" + UrlEncode(game) +
        "&scope=" + scopeName + "&sort=" + UrlEncode(sort);
    if (!search.empty()) {
        const bool shareCode = search.size() == 36 && search[8] == '-' && search[13] == '-' && search[18] == '-' && search[23] == '-';
        path += shareCode ? "&code=" + UrlEncode(search) : "&search=" + UrlEncode(search.substr(0, 80));
    }
    const auto response = OmniGhost::Licensing::WebsiteRequest("GET", path);
    OmniGhost::Update::Json::Value root;
    const auto* object = ParseCloudObject(response, root, "Config Hub");
    if (!object) return false;
    const auto* value = OmniGhost::Update::Json::Field(*object, "configs");
    const auto* rows = value ? OmniGhost::Update::Json::AsArray(*value) : nullptr;
    if (!rows) { last_status = "Config Hub: lista ausente"; return false; }
    std::vector<CommunityConfigInfo> next;
    next.reserve(rows->size());
    for (const auto& entry : *rows) {
        const auto* row = OmniGhost::Update::Json::AsObject(entry);
        if (!row) continue;
        CommunityConfigInfo info;
        info.id = JsonString(*row, "id");
        info.game = JsonString(*row, "game");
        info.title = JsonString(*row, "title");
        info.description = JsonString(*row, "description");
        info.author = JsonString(*row, "author");
        info.visibility = JsonString(*row, "visibility");
        info.status = JsonString(*row, "status");
        info.updated_at = JsonString(*row, "updatedAt");
        info.version = JsonInt(*row, "version");
        info.schema_version = JsonInt(*row, "schemaVersion");
        info.min_launcher_version = JsonString(*row, "minLauncherVersion");
        info.compatible = info.schema_version > 0 && info.schema_version <= kConfigSchemaVersion;
        info.downloads = JsonInt(*row, "downloads");
        info.favorites = JsonInt(*row, "favorites");
        info.rating_count = JsonInt(*row, "ratingCount");
        info.rating = static_cast<float>(JsonDouble(*row, "rating"));
        info.my_rating = JsonInt(*row, "myRating");
        info.official = JsonBool(*row, "official");
        info.verified = JsonBool(*row, "verified");
        info.favorite = JsonBool(*row, "favorite");
        info.owned = JsonBool(*row, "owned");
        if (const auto* tagsValue = OmniGhost::Update::Json::Field(*row, "tags")) {
            if (const auto* tags = OmniGhost::Update::Json::AsArray(*tagsValue)) {
                for (const auto& tag : *tags) if (const auto* text = OmniGhost::Update::Json::AsString(tag))
                    info.tags.push_back(*text);
            }
        }
        if (!info.id.empty() && !info.title.empty()) next.push_back(std::move(info));
    }
    community_configs = std::move(next);
    last_status = "Config Hub atualizado: " + std::to_string(community_configs.size()) + " configs";
    return true;
}

static std::vector<std::string> SplitTags(const std::string& input) {
    std::vector<std::string> tags;
    std::istringstream stream(input);
    std::string tag;
    while (std::getline(stream, tag, ',') && tags.size() < 6) {
        const auto first = tag.find_first_not_of(" \t\r\n");
        const auto last = tag.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        tag = tag.substr(first, last - first + 1);
        if (tag.size() > 24) tag.resize(24);
        if (!tag.empty() && std::find(tags.begin(), tags.end(), tag) == tags.end()) tags.push_back(tag);
    }
    return tags;
}

bool PublishCommunityConfig(const std::string& title, const std::string& description,
                            const std::string& visibility, const std::string& tags,
                            const std::string& changelog, const std::string& config_id) {
    const std::string payload = SerializeAll();
    if (payload.empty() || payload.size() > 262144u) { last_status = "Config demasiado grande para publicar"; return false; }
    if (title.size() < 3 || title.size() > 80 || description.size() > 600 || changelog.size() > 500) {
        last_status = "Título, descrição ou changelog inválido"; return false;
    }
    if (visibility != "private" && visibility != "unlisted" && visibility != "public") {
        last_status = "Visibilidade inválida"; return false;
    }
    std::ostringstream body;
    body << "{\"action\":\"publish\",\"title\":\"" << EscapeJsonString(title)
         << "\",\"description\":\"" << EscapeJsonString(description)
         << "\",\"game\":\"" << EscapeJsonString(ActiveGameMeta())
         << "\",\"visibility\":\"" << visibility << "\",\"tags\":[";
    const auto parsedTags = SplitTags(tags);
    for (std::size_t i = 0; i < parsedTags.size(); ++i) {
        if (i) body << ',';
        body << '"' << EscapeJsonString(parsedTags[i]) << '"';
    }
    body << "],\"schemaVersion\":" << kConfigSchemaVersion
         << ",\"minLauncherVersion\":\"" << EscapeJsonString(CurrentAppVersion())
         << "\",\"payload\":\"" << EscapeJsonString(payload)
         << "\",\"changelog\":\"" << EscapeJsonString(changelog) << '"';
    if (!config_id.empty()) body << ",\"configId\":\"" << EscapeJsonString(config_id) << '"';
    body << '}';
    const auto response = OmniGhost::Licensing::WebsiteRequest("POST", "/api/launcher/community-configs", body.str());
    if (!CloudResponseOk(response, "Publicação")) return false;
    last_status = visibility == "private" ? "Config privada guardada" : "Config enviada para revisão";
    return true;
}

static bool CommunityAction(const std::string& action, const std::string& config_id,
                            std::string_view extra, const char* label) {
    if (config_id.empty()) { last_status = "Config da comunidade inválida"; return false; }
    const std::string body = "{\"action\":\"" + action + "\",\"configId\":\"" +
        EscapeJsonString(config_id) + "\"" + std::string(extra) + "}";
    const auto response = OmniGhost::Licensing::WebsiteRequest("POST", "/api/launcher/community-configs", body);
    if (!CloudResponseOk(response, label)) return false;
    last_status = std::string(label) + " concluído";
    return true;
}

bool ApplyCommunityConfig(const std::string& config_id, bool save_copy) {
    const std::string body = "{\"action\":\"download\",\"configId\":\"" + EscapeJsonString(config_id) + "\"}";
    const auto response = OmniGhost::Licensing::WebsiteRequest("POST", "/api/launcher/community-configs", body);
    OmniGhost::Update::Json::Value root;
    const auto* object = ParseCloudObject(response, root, "Download");
    if (!object) return false;
    const std::string payload = JsonString(*object, "payload");
    const std::string expectedHash = JsonString(*object, "payloadHash");
    const std::string title = JsonString(*object, "title");
    const int schemaVersion = JsonInt(*object, "schemaVersion");
    if (payload.empty() || payload.size() > kMaxConfigBytes || expectedHash.size() != 64 ||
        schemaVersion < 1 || schemaVersion > kConfigSchemaVersion) {
        last_status = "Config da comunidade incompatível"; return false;
    }
    std::string actualHash, hashError;
    if (!OmniGhost::Platform::Sha256Text(payload, actualHash, hashError) || actualHash != expectedHash) {
        last_status = "Hash da config da comunidade inválido"; return false;
    }
    std::string backupError;
    if (!WriteTextAtomically(CommunityBackupPath(), SerializeAll(), backupError)) {
        last_status = "Não foi possível criar backup antes de aplicar"; return false;
    }
    if (!DeserializeAll(payload)) { last_status = "Config descarregada rejeitada pela validação"; return false; }
    if (save_copy) {
        const std::string name = std::string(ActiveGamePrefix()) + "community-" + SanitizeFileName(title);
        if (!SaveToFile(name)) return false;
    }
    last_status = save_copy ? "Config guardada na tua cloud local" : "Config aplicada; podes reverter";
    return true;
}

bool DownloadCommunityToLibrary(const std::string& config_id) {
    const std::string body = "{\"action\":\"download\",\"configId\":\"" + EscapeJsonString(config_id) + "\"}";
    const auto response = OmniGhost::Licensing::WebsiteRequest("POST", "/api/launcher/community-configs", body);
    OmniGhost::Update::Json::Value root;
    const auto* object = ParseCloudObject(response, root, "Guardar no perfil");
    if (!object) return false;
    const std::string payload = JsonString(*object, "payload");
    const std::string expectedHash = JsonString(*object, "payloadHash");
    const std::string title = JsonString(*object, "title");
    const std::string game = JsonString(*object, "game");
    const std::string prefix = game == "fivem" ? "fivem-" : game == "cs2" ? "cs2-" :
        game == "warzone" ? "warzone-" : game == "fortnite" ? "fortnite-" : "";
    std::string actualHash, hashError;
    if (prefix.empty() || payload.empty() || payload.size() > kMaxConfigBytes || expectedHash.size() != 64 ||
        !OmniGhost::Platform::Sha256Text(payload, actualHash, hashError) || actualHash != expectedHash ||
        payload.find("meta.game=" + game) == std::string::npos) {
        last_status = "Config incompatível ou com integridade inválida"; return false;
    }
    EnsureConfigDir();
    const std::string name = prefix + "community-" + SanitizeFileName(title);
    std::string error;
    if (!WriteTextAtomically(fs::path(ConfigDir()) / (name + ".ogcfg"), payload, error)) {
        last_status = "Falha ao guardar na biblioteca: " + error; return false;
    }
    last_status = "Guardada em Configs: " + name;
    RefreshList();
    return true;
}

static std::map<std::string, std::string> ConfigValues(std::string_view payload) {
    std::map<std::string, std::string> values;
    std::istringstream stream{std::string(payload)};
    std::string line, key, value;
    while (std::getline(stream, line)) if (ParseLine(line, key, value)) values[key] = value;
    return values;
}

bool PreviewCommunityConfig(const std::string& config_id) {
    const std::string body = "{\"action\":\"preview\",\"configId\":\"" + EscapeJsonString(config_id) + "\"}";
    const auto response = OmniGhost::Licensing::WebsiteRequest("POST", "/api/launcher/community-configs", body);
    OmniGhost::Update::Json::Value root;
    const auto* object = ParseCloudObject(response, root, "Pré-visualização");
    if (!object) return false;
    const std::string payload = JsonString(*object, "payload");
    const std::string expectedHash = JsonString(*object, "payloadHash");
    std::string actualHash, hashError;
    if (payload.empty() || expectedHash.size() != 64 ||
        !OmniGhost::Platform::Sha256Text(payload, actualHash, hashError) || actualHash != expectedHash) {
        last_status = "Pré-visualização rejeitada: integridade inválida"; return false;
    }
    const auto current = ConfigValues(SerializeAll());
    const auto incoming = ConfigValues(payload);
    int changed = 0, added = 0, removed = 0;
    for (const auto& [key, value] : incoming) {
        const auto found = current.find(key);
        if (found == current.end()) ++added;
        else if (found->second != value) ++changed;
    }
    for (const auto& [key, value] : current) if (!incoming.contains(key)) ++removed;
    last_status = "Diferenças: " + std::to_string(changed) + " alteradas, " +
        std::to_string(added) + " novas, " + std::to_string(removed) + " removidas";
    return true;
}

bool ToggleCommunityFavorite(const std::string& config_id, bool enabled) {
    return CommunityAction("favorite", config_id, enabled ? ",\"enabled\":true" : ",\"enabled\":false", "Favorito");
}
bool RateCommunityConfig(const std::string& config_id, int rating) {
    rating = std::clamp(rating, 1, 5);
    return CommunityAction("rate", config_id, ",\"rating\":" + std::to_string(rating), "Avaliação");
}
bool ReportCommunityConfig(const std::string& config_id, const std::string& reason) {
    if (reason.size() < 3 || reason.size() > 300) { last_status = "Motivo inválido"; return false; }
    return CommunityAction("report", config_id, ",\"reason\":\"" + EscapeJsonString(reason) + "\"", "Denúncia");
}
bool WithdrawCommunityConfig(const std::string& config_id) {
    return CommunityAction("withdraw", config_id, {}, "Retirada");
}
bool RevertCommunityConfig() {
    const fs::path path = CommunityBackupPath();
    std::ifstream input(path, std::ios::binary);
    if (!input) { last_status = "Não existe backup de aplicação"; return false; }
    std::ostringstream contents; contents << input.rdbuf();
    if (contents.str().size() > kMaxConfigBytes || !DeserializeAll(contents.str())) {
        last_status = "Backup inválido"; return false;
    }
    std::error_code error; fs::remove(path, error);
    last_status = "Config anterior restaurada";
    return true;
}

bool ExportJson(const std::string& file_name) {
    const std::string safe = SanitizeFileName(file_name.empty() ? "OmniGhost-export.json" : file_name);
    EnsureConfigDir();
    const fs::path path = fs::path(ConfigDir()) / safe;
    std::ostringstream json;
    json << "{\n"
         << "  \"schemaVersion\": 1,\n"
         << "  \"game\": \"" << EscapeJsonString(ActiveGameMeta()) << "\",\n"
         << "  \"format\": \"omnighost-config-v5\",\n"
         << "  \"payload\": \"" << EscapeJsonString(SerializeAll()) << "\"\n"
         << "}\n";
    std::string error;
    if (!WriteTextAtomically(path, json.str(), error)) {
        last_status = "Falha ao exportar JSON: " + error;
        return false;
    }
    last_status = "JSON exportado: " + path.filename().string();
    return true;
}

bool ImportJson(const std::string& file_name) {
    const std::string safe = SanitizeFileName(file_name.empty() ? "OmniGhost-import.json" : file_name);
    const fs::path path = fs::path(ConfigDir()) / safe;
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size == 0 || size > kMaxConfigBytes) {
        last_status = ec ? "JSON nao encontrado na pasta Configs" : "JSON invalido ou demasiado grande";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream content;
    content << stream.rdbuf();
    OmniGhost::Update::Json::Value root;
    OmniGhost::Update::Json::Limits limits{8, static_cast<std::size_t>(kMaxConfigBytes), 64};
    const auto parsed = OmniGhost::Update::Json::Parse(content.str(), root, limits);
    const auto* object = parsed.ok ? OmniGhost::Update::Json::AsObject(root) : nullptr;
    if (!object) {
        last_status = "JSON invalido: " + parsed.error;
        return false;
    }
    std::int64_t version = 0;
    const auto* versionField = OmniGhost::Update::Json::Field(*object, "schemaVersion");
    const auto* gameField = OmniGhost::Update::Json::Field(*object, "game");
    const auto* formatField = OmniGhost::Update::Json::Field(*object, "format");
    const auto* payloadField = OmniGhost::Update::Json::Field(*object, "payload");
    const auto* game = gameField ? OmniGhost::Update::Json::AsString(*gameField) : nullptr;
    const auto* format = formatField ? OmniGhost::Update::Json::AsString(*formatField) : nullptr;
    const auto* payload = payloadField ? OmniGhost::Update::Json::AsString(*payloadField) : nullptr;
    if (!versionField || !OmniGhost::Update::Json::AsInt64(*versionField, version) || version != 1 ||
        !game || *game != ActiveGameMeta() || !format || *format != "omnighost-config-v5" ||
        !payload || payload->empty() || payload->size() > kMaxConfigBytes) {
        last_status = "JSON incompativel, incompleto ou pertencente a outro jogo";
        return false;
    }
    if (!DeserializeAll(*payload)) {
        if (last_status.empty()) last_status = "Payload de configuracao invalido";
        return false;
    }
    last_status = "JSON importado e validado";
    return true;
}

bool CopyToClipboard() {
    const std::wstring data = OmniGhost::Platform::Utf8ToWide(SerializeAll());
    if (data.empty()) { last_status = "Conversao UTF-8 falhou"; return false; }
    if (!OpenClipboard(nullptr)) { last_status = "Clipboard falhou"; return false; }
    const auto closeClipboard = OmniGhost::Platform::MakeScopeExit([] { CloseClipboard(); });
    (void)closeClipboard;
    if (!EmptyClipboard()) { last_status = "Clipboard falhou"; return false; }

    const SIZE_T bytes = (data.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) { last_status = "Clipboard sem memoria"; return false; }

    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        last_status = "Clipboard falhou";
        return false;
    }
    memcpy(destination, data.c_str(), bytes);
    GlobalUnlock(memory);

    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        last_status = "Clipboard falhou";
        return false;
    }
    last_status = "Copiado para clipboard";
    return true;
}

bool PasteFromClipboard() {
    if (!OpenClipboard(nullptr)) { last_status = "Clipboard falhou"; return false; }
    const auto closeClipboard = OmniGhost::Platform::MakeScopeExit([] { CloseClipboard(); });
    (void)closeClipboard;

    std::string data;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
        if (text) {
            data = OmniGhost::Platform::WideToUtf8(text);
            GlobalUnlock(handle);
        }
    } else if (HANDLE ansiHandle = GetClipboardData(CF_TEXT)) {
        const char* text = static_cast<const char*>(GlobalLock(ansiHandle));
        if (text) {
            data = text;
            GlobalUnlock(ansiHandle);
        }
    }
    if (data.empty()) { last_status = "Clipboard vazio"; return false; }
    if (data.size() > kMaxConfigBytes) { last_status = "Clipboard demasiado grande"; return false; }

    const bool ok = DeserializeAll(data);
    last_status = ok ? "Colado do clipboard" : "Dados invalidos";
    return ok;
}


void TickAutoSave() {
    if (!app_settings::config.auto_save || active_config_name.empty())
        return;

    static std::string lastSnap;
    static auto lastChange = std::chrono::steady_clock::now();
    static auto lastCompare = std::chrono::steady_clock::time_point{};
    static bool dirty = false;
    static std::future<std::string> pendingWrite;
    static bool writeInFlight = false;

    if (writeInFlight) {
        if (pendingWrite.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            return;
        writeInFlight = false;
        try {
            const std::string writeError = pendingWrite.get();
            if (writeError.empty())
                last_status = "Auto: " + active_config_name;
            else
                last_status = "Auto-save falhou: " + writeError;
        } catch (...) {
            last_status = "Auto-save falhou: exceção no escritor";
        }
        return;
    }

    const auto nowTp = std::chrono::steady_clock::now();
    if (lastCompare.time_since_epoch().count() != 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(nowTp - lastCompare).count() < 500)
        return;
    lastCompare = nowTp;

    std::string now = SerializeAll();
    if (lastSnap.empty()) {
        lastSnap = std::move(now);
        return;
    }
    if (now != lastSnap) {
        lastSnap = std::move(now);
        dirty = true;
        lastChange = std::chrono::steady_clock::now();
    }
    if (!dirty) return;

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - lastChange).count();
    if (elapsed < 700) return;



    const std::string safeName = SanitizeFileName(active_config_name);
    if (safeName != active_config_name) {
        active_config_name = safeName;

        WriteState({{std::string("active.") + ActiveGameMeta(), active_config_name}});
    }

    EnsureConfigDir();
    const std::string pathString = (fs::path(ConfigDir()) / (active_config_name + ".ogcfg")).string();
    const std::string payload = lastSnap;
    dirty = false;
    writeInFlight = true;
    pendingWrite = std::async(std::launch::async, [pathString, payload]() -> std::string {
        std::string error;
        if (!WriteTextAtomically(fs::path(pathString), payload, error))
            return error;
        return {};
    });
}

void FlushActiveConfig() {
    if (active_config_name.empty()) return;

    const std::string safeName = SanitizeFileName(active_config_name);
    if (safeName != active_config_name) {
        active_config_name = safeName;
        WriteState({{std::string("active.") + ActiveGameMeta(), active_config_name}});
    }

    EnsureConfigDir();
    const fs::path path = fs::path(ConfigDir()) / (active_config_name + ".ogcfg");
    std::string error;
    if (WriteTextAtomically(path, SerializeAll(), error))
        last_status = "Flush: " + active_config_name;
    else
        last_status = "Flush falhou: " + error;
}

const char* ActiveConfigName() {
    return active_config_name.empty() ? "(nenhuma)" : active_config_name.c_str();
}


void OpenConfigFolder() {
    const fs::path directory = OmniGhost::Paths::Configs();
    std::error_code error;
    fs::create_directories(directory, error);
    if (!error)
        ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string GetConfigFolder() {
    EnsureConfigDir();
    return ConfigDir();
}

void MoveConfig(int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(saved_list.size()) ||
        to >= static_cast<int>(saved_list.size()) || from == to)
        return;
    std::swap(saved_list[static_cast<size_t>(from)], saved_list[static_cast<size_t>(to)]);
    SaveOrder();
    last_status = "Ordem das configs atualizada";
}

bool IsFavoriteConfig(const std::string& name) {
    return !name.empty() &&
        ReadState(std::string("favorite.") + ActiveGameMeta()) == name;
}

bool ToggleFavoriteConfig(const std::string& name) {
    if (!NameBelongsToActiveGame(name) || !IsSafeConfigIdentifier(name)) {
        last_status = "Config favorita invalida";
        return false;
    }

    const std::string key = std::string("favorite.") + ActiveGameMeta();
    const bool removing = ReadState(key) == name;
    if (!WriteState({{key, removing ? std::string{} : name}})) return false;
    last_status = removing ? "Favorito removido: " + name : "Favorito: " + name;
    RefreshList();
    return true;
}

void Initialize() {
    EnsureConfigDir();
    MigrateLegacyConfigs();
    ActivateCurrentGameConfig();
}

void ActivateCurrentGameConfig() {
    EnsureConfigDir();
    const std::string favorite_key = std::string("favorite.") + ActiveGameMeta();
    const std::string favorite_name = ReadState(favorite_key);
    const std::string remembered_name = ReadState(std::string("active.") + ActiveGameMeta());
    active_config_id = ReadState(std::string("active_id.") + ActiveGameMeta());
    RefreshList();

    const std::string default_name = std::string(ActiveGamePrefix()) + "default";
    bool loaded = false;
    if (!favorite_name.empty() && NameBelongsToActiveGame(favorite_name)) {
        loaded = LoadFromFile(favorite_name);
        if (!loaded)
            (void)WriteState({{favorite_key, ""}});
    }
    if (!loaded && !remembered_name.empty() && remembered_name != favorite_name &&
        NameBelongsToActiveGame(remembered_name)) {
        loaded = LoadFromFile(remembered_name);
    }
    if (!loaded)
        loaded = LoadFromFile(default_name);

    if (!loaded) {
        ResetActiveGameToDefaults();
        active_config_id = NewConfigId();
        (void)SaveToFile(default_name);
    }

    if (active_config_name == default_name)
        active_profile_name = "Default";
    else if (active_config_name == std::string(ActiveGamePrefix()) + "custom")
        active_profile_name = "Custom";
    else
        active_profile_name = "Personalizado";
}

}
