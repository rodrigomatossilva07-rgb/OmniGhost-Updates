#include "app_settings.h"
#include <Windows.h>
#include <shellapi.h>
#include "config_core.h"
#include "../globals.h"
#include "../window/theme.h"
#include "../platform/text_encoding.h"
#include "../platform/unique_handle.h"
#include "../platform/build_info.h"
#include <algorithm>
#include <limits>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <objbase.h>
#include <windows.h>
#include <iomanip>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace config_manager {

// Global state
std::vector<SavedConfigInfo> saved_list;
char new_config_name[64] = "MinhaConfig";
std::string last_status = "Pronto";
std::string active_config_name;
std::string active_profile_name = "Default";
std::string active_config_id;

// ---- Core helpers ----
std::string ConfigDir();

static fs::path ConfigStatePath() { return fs::path(ConfigDir()) / "config_state.ini"; }
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

static bool WriteState(const std::string& key, const std::string& value) {
    EnsureConfigDir();
    std::map<std::string, std::string> values;
    std::ifstream f(ConfigStatePath());
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos != std::string::npos) values[line.substr(0, pos)] = line.substr(pos + 1);
    }
    values[key] = value;
    std::ostringstream serialized;
    for (const auto& [k, v] : values) serialized << k << "=" << v << '\n';
    std::string error;
    if (!WriteTextAtomically(ConfigStatePath(), serialized.str(), error)) {
        last_status = "Falha ao guardar estado da configuração: " + error;
        return false;
    }
    return true;
}

// ---- Helpers ----
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

    FlushPathToDisk(temporary);

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

// Serialization helpers
void WBool(std::ostringstream& o, const char* k, bool v) { o << k << "=" << (v ? 1 : 0) << "\n"; }
void WFloat(std::ostringstream& o, const char* k, float v) { o << k << "=" << v << "\n"; }
void WInt(std::ostringstream& o, const char* k, int v) { o << k << "=" << v << "\n"; }
void WU32(std::ostringstream& o, const char* k, ImU32 v) { o << k << "=" << v << "\n"; }
void WStr(std::ostringstream& o, const char* k, const std::string& v) { o << k << "=" << v << "\n"; }

// Deserialization helpers
bool toB(const std::string& val) { return val == "1" || val == "true"; }
float toF(const std::string& val) { return (float)atof(val.c_str()); }
int toI(const std::string& val) { return atoi(val.c_str()); }
ImU32 toU(const std::string& val) { return (ImU32)strtoul(val.c_str(), nullptr, 10); }

// ---- Directory management ----
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
            stem.rfind("rust_", 0) == 0 || stem.rfind("warzone_", 0) == 0)
            continue;
        
        // Read meta.game if present
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
        else {
            // Heuristic
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

    // Load manual order from Configs/order.txt if present
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

// ---- Game prefix/meta ----
const char* ActiveGamePrefix() {
    switch (g_activeGame) {
    case ActiveGame::CS2: return "cs2_";
    case ActiveGame::Warzone: return "warzone_";
    default: return "fivem_";
    }
}

const char* ActiveGameMeta() {
    switch (g_activeGame) {
    case ActiveGame::CS2: return "cs2";
    case ActiveGame::Warzone: return "warzone";
    default: return "fivem";
    }
}

bool NameBelongsToActiveGame(const std::string& name) {
    const char* p = ActiveGamePrefix();
    return name.rfind(p, 0) == 0;
}

// ---- File operations ----
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
        safe = prefix + safe;
    }
    EnsureConfigDir();
    const fs::path path = fs::path(ConfigDir()) / (safe + ".ogcfg");
    std::string error;
    if (!WriteTextAtomically(path, SerializeAll(), error)) {
        last_status = "Erro ao guardar atomicamente: " + error;
        return false;
    }
    last_status = "Guardado: Configs/" + safe + ".ogcfg";
    active_config_name = safe;
    WriteState(std::string("active.") + ActiveGameMeta(), active_config_name);
    WriteState(std::string("active_id.") + ActiveGameMeta(), active_config_id);
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
        WriteState(std::string("active.") + ActiveGameMeta(), active_config_name);
        WriteState(std::string("active_id.") + ActiveGameMeta(), active_config_id);
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
    if (!ec && IsFavoriteConfig(name))
        WriteState(std::string("favorite.") + ActiveGameMeta(), "");
    last_status = ec ? "Erro ao apagar" : ("Apagado: " + name);
    RefreshList();
    return !ec;
}

// ---- Clipboard ----
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

// ---- Auto-save ----
void TickAutoSave() {
    if (!app_settings::config.auto_save || active_config_name.empty())
        return;

    static std::string lastSnap;
    static auto lastChange = std::chrono::steady_clock::now();
    static bool dirty = false;

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

    EnsureConfigDir();
    const fs::path path = fs::path(ConfigDir()) / (active_config_name + ".ogcfg");
    std::string error;
    if (WriteTextAtomically(path, lastSnap, error)) {
        dirty = false;
        last_status = "Auto: " + active_config_name;
    } else {
        last_status = "Auto-save falhou: " + error;
    }
}

void FlushActiveConfig() {
    if (active_config_name.empty()) return;
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

// ---- Folder operations ----
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
    WriteState(key, removing ? std::string{} : name);
    last_status = removing ? "Favorito removido: " + name : "Favorito: " + name;
    RefreshList();
    return !removing;
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
            WriteState(favorite_key, "");
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

} // namespace config_manager
