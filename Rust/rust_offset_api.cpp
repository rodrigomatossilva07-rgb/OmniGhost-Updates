#include "rust_offset_api.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

namespace Rust {
namespace OffsetApi {
namespace {

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

struct HttpResult {
    int status = 0;
    std::string body;
    std::string etag;
};

HttpResult HttpGet(const std::wstring& host, const std::wstring& path, const std::wstring& ifNoneMatch) {
    HttpResult out;
    HINTERNET sess = WinHttpOpen(L"OmniGhost-RustOffsetApi/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) return out;
    HINTERNET conn = WinHttpConnect(sess, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) { WinHttpCloseHandle(sess); return out; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) {
        WinHttpCloseHandle(conn); WinHttpCloseHandle(sess); return out;
    }
    std::wstring hdrs;
    if (!ifNoneMatch.empty())
        hdrs = L"If-None-Match: " + ifNoneMatch + L"\r\n";
    WinHttpSendRequest(req,
        hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(),
        hdrs.empty() ? 0 : (DWORD)-1,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(req, nullptr);

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    out.status = (int)status;

    wchar_t etagBuf[512]{};
    DWORD etagLen = sizeof(etagBuf);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"ETag", etagBuf, &etagLen, WINHTTP_NO_HEADER_INDEX))
        out.etag = ToUtf8(etagBuf);

    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail) {
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read) || !read) break;
        out.body.append(chunk.data(), read);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);
    return out;
}

// Minimal extraction of "name": "0x..." or name = 0x... from JSON/markdown body
void GrabHex(const std::string& text, const char* key, std::string& dst) {
    // JSON style "key": "0xABC"
    std::string k1 = std::string("\"") + key + "\"";
    size_t p = text.find(k1);
    if (p != std::string::npos) {
        p = text.find("0x", p);
        if (p != std::string::npos) {
            size_t e = p + 2;
            while (e < text.size() && std::isxdigit((unsigned char)text[e])) ++e;
            dst = text.substr(p, e - p);
            return;
        }
    }
    // C++ style key = 0xABC
    std::string k2 = std::string(key) + " ";
    p = 0;
    while ((p = text.find(key, p)) != std::string::npos) {
        size_t eq = text.find('=', p);
        size_t semi = text.find(';', p);
        if (eq != std::string::npos && semi != std::string::npos && eq < semi && eq - p < 64) {
            size_t hx = text.find("0x", eq);
            if (hx != std::string::npos && hx < semi) {
                size_t e = hx + 2;
                while (e < text.size() && std::isxdigit((unsigned char)text[e])) ++e;
                dst = text.substr(hx, e - hx);
                return;
            }
        }
        p += strlen(key);
    }
}

std::string BuildOmniJson(const std::string& apiBody) {
    // Prefer full markdown / offsets_flat embedded in API response
    if (apiBody.empty() || apiBody.size() > 8u * 1024u * 1024u ||
        (apiBody.find("offsets_flat") == std::string::npos &&
         apiBody.find("markdown") == std::string::npos &&
         apiBody.find("base_networkable") == std::string::npos &&
         apiBody.find("il2cpphandle") == std::string::npos))
        return {};
    std::string src = apiBody;
    for (char& c : src) if (c == '\r') c = '\n';

    auto g = [&](const char* k) -> std::string {
        std::string v; GrabHex(src, k, v); return v;
    };
    auto or_def = [](const std::string& v, const char* d) -> std::string {
        return v.empty() ? std::string(d) : v;
    };

    // TypeInfo / globals (klass_rvas + rust_globals)
    const std::string bn = or_def(
        g("base_networkable_static").empty() ? g("base_networkable") : g("base_networkable_static"),
        "0x0");
    const std::string mc = or_def(g("main_camera_c").empty() ? g("MainCamera") : g("main_camera_c"), "0x0");
    const std::string bp = or_def(
        g("BasePlayer").empty() ? g("BasePlayer_TypeInfo") : g("BasePlayer"),
        "0x0");
    const std::string gh = or_def(g("il2cpphandle").empty() ? g("gchandle_get_target") : g("il2cpphandle"), "0x0");
    const std::string lp = g("local_player_static");
    const std::string tid = g("type_info_definition_table");

    const std::string vm = or_def(g("view_matrix"), "0x2FC");
    const std::string camPos = or_def(g("position"), "0x444");
    const std::string camObj = or_def(g("camera_object"), "0x30");
    const std::string sf = or_def(g("static_fields"), "0xB8");
    const std::string wrap = or_def(g("wrapper_class_ptr"), "0x8");
    const std::string parent = or_def(g("parent_static_fields"), "0x10");
    const std::string entities = or_def(g("entities"), "0x10");
    const std::string hv = or_def(g("hv_offset"), "0x18");
    const std::string bufArr = or_def(g("buffer_list_array"), "0x10");
    const std::string bufSz = or_def(g("buffer_list_size"), "0x18");

    const std::string pm = or_def(g("player_model"), "0x340");
    const std::string flags = or_def(g("player_flags"), "0x6D0");
    const std::string hp = or_def(g("_health"), "0x2B4");
    const std::string maxhp = or_def(g("_maxHealth"), "0x2B8");
    const std::string life = or_def(g("lifestate"), "0x2A8");
    const std::string team = or_def(g("current_team"), "0x550");
    const std::string move = or_def(g("base_movement"), "0x520");
    const std::string inv = or_def(g("player_inventory"), "0x4B0");
    const std::string eyes = or_def(g("player_eyes"), "0x7A0");
    const std::string modelPos = or_def(g("modelPosition").empty() ? g("position") : g("modelPosition"), "0x2F8");
    const std::string isNpc = or_def(g("isNpc"), "0x374");
    const std::string newVel = or_def(g("newVelocity"), "0x31C");
    const std::string displayName = or_def(g("displayName").empty() ? g("display_name") : g("displayName"), "0x390");
    const std::string held = or_def(g("heldEntity").empty() ? g("cl_active_item") : g("heldEntity"), "0x310");
    const std::string clItem = or_def(g("cl_active_item"), "0x580");
    const std::string bounds = g("bounds");
    const std::string model = g("model");
    const std::string entFlags = g("flags");

    // Reject empty criticals
    if (bn == "0x0" && mc == "0x0")
        return {};

    std::ostringstream o;
    o << "{\n";
    o << "  \"schema_version\": 4,\n";
    o << "  \"source\": \"https://www.cheatoffsets.com/g/rust\",\n";
    o << "  \"updated_at\": \"api-live\",\n";
    o << "  \"note\": \"Auto-fetched from cheatoffsets.com API — full field set\",\n";
    // Top-level aliases for Rust::LoadOffsetsFromJson grab()
    o << "  \"BaseNetworkable_TypeInfo\": \"" << bn << "\",\n";
    o << "  \"MainCamera_TypeInfo\": \"" << mc << "\",\n";
    o << "  \"BasePlayer_TypeInfo\": \"" << bp << "\",\n";
    o << "  \"base_networkable\": \"" << bn << "\",\n";
    o << "  \"main_camera_c\": \"" << mc << "\",\n";
    o << "  \"il2cpphandle\": \"" << gh << "\",\n";
    if (!lp.empty()) o << "  \"local_player_static\": \"" << lp << "\",\n";
    if (!tid.empty()) o << "  \"type_info_definition_table\": \"" << tid << "\",\n";
    o << "  \"staticFields\": \"" << sf << "\",\n";
    o << "  \"mainCamera\": \"" << camObj << "\",\n";
    o << "  \"cameraGameObject\": \"0x10\",\n";
    o << "  \"viewMatrix\": \"" << vm << "\",\n";
    o << "  \"playerModel\": \"" << pm << "\",\n";
    o << "  \"playerFlags\": \"" << flags << "\",\n";
    o << "  \"_health\": \"" << hp << "\",\n";
    o << "  \"_maxHealth\": \"" << maxhp << "\",\n";
    o << "  \"lifestate\": \"" << life << "\",\n";
    o << "  \"currentTeam\": \"" << team << "\",\n";
    o << "  \"movement\": \"" << move << "\",\n";
    o << "  \"inventory\": \"" << inv << "\",\n";
    o << "  \"playerEyes\": \"" << eyes << "\",\n";
    o << "  \"modelPosition\": \"" << modelPos << "\",\n";
    o << "  \"isNpc\": \"" << isNpc << "\",\n";
    o << "  \"newVelocity\": \"" << newVel << "\",\n";
    o << "  \"displayName\": \"" << displayName << "\",\n";
    o << "  \"heldEntity\": \"" << held << "\",\n";
    o << "  \"clActiveItem\": \"" << clItem << "\",\n";
    o << "  \"bufferList\": \"" << bufArr << "\",\n";
    o << "  \"buffer\": \"" << bufArr << "\",\n";
    o << "  \"bufferSize\": \"" << bufSz << "\",\n";
    o << "  \"globals\": {\n";
    o << "    \"il2cpphandle\": \"" << gh << "\",\n";
    o << "    \"base_networkable_static\": \"" << bn << "\",\n";
    o << "    \"main_camera_c\": \"" << mc << "\"";
    if (!lp.empty()) o << ",\n    \"local_player_static\": \"" << lp << "\"";
    if (!tid.empty()) o << ",\n    \"type_info_definition_table\": \"" << tid << "\"";
    o << "\n  },\n";
    o << "  \"structs\": {\n";
    o << "    \"base_networkable\": {\n";
    o << "      \"base_networkable\": \"" << bn << "\",\n";
    o << "      \"static_fields\": \"" << sf << "\",\n";
    o << "      \"wrapper_class_ptr\": \"" << wrap << "\",\n";
    o << "      \"parent_static_fields\": \"" << parent << "\",\n";
    o << "      \"entities\": \"" << entities << "\",\n";
    o << "      \"hv_offset\": \"" << hv << "\",\n";
    o << "      \"buffer_list_array\": \"" << bufArr << "\",\n";
    o << "      \"buffer_list_size\": \"" << bufSz << "\"\n";
    o << "    },\n";
    o << "    \"camera\": {\n";
    o << "      \"main_camera_c\": \"" << mc << "\",\n";
    o << "      \"camera_static\": \"" << sf << "\",\n";
    o << "      \"camera_object\": \"" << camObj << "\",\n";
    o << "      \"entity\": \"0x10\",\n";
    o << "      \"view_matrix\": \"" << vm << "\",\n";
    o << "      \"position\": \"" << camPos << "\"\n";
    o << "    }\n";
    o << "  },\n";
    o << "  \"decrypt\": {\n";
    o << "    \"gchandle_base_rva\": \"" << gh << "\",\n";
    o << "    \"client_entities\": \"" << wrap << "\",\n";
    o << "    \"entity_list\": \"" << parent << "\",\n";
    o << "    \"static_fields\": \"" << sf << "\"\n";
    o << "  }\n";
    o << "}\n";
    return o.str();
}

std::string EtagPath(const char* jsonPath) {
    std::string p = jsonPath ? jsonPath : "data/rust_offsets.json";
    auto slash = p.find_last_of("/\\");
    std::string dir = (slash == std::string::npos) ? "." : p.substr(0, slash);
    return dir + "/rust_offsets.etag";
}

bool SaveResponse(const char* path, const HttpResult& response, std::string* status_out) {
    const std::string json = BuildOmniJson(response.body);
    if (json.empty()) {
        if (status_out) *status_out = "Resposta Rust sem offsets reconhecíveis";
        return false;
    }
    const std::string filePath = path && *path ? path : "data/rust_offsets.json";
    const auto slash = filePath.find_last_of("/\\");
    if (slash != std::string::npos)
        CreateDirectoryA(filePath.substr(0, slash).c_str(), nullptr);
    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (status_out) *status_out = "Não foi possível guardar os offsets Rust";
        return false;
    }
    file << json;
    file.close();
    if (!response.etag.empty()) {
        std::ofstream etag(EtagPath(filePath.c_str()), std::ios::trunc);
        etag << response.etag;
    }
    if (status_out) *status_out = "Offsets Rust recebidos e preparados";
    return true;
}

} // namespace

bool FetchAndSave(const char* out_json_path, std::string* status_out) {
    const char* path = out_json_path && out_json_path[0] ? out_json_path : "data/rust_offsets.json";
    auto r = HttpGet(L"www.cheatoffsets.com", L"/api/games/rust/current", L"");
    if (r.status != 200 || r.body.empty()) {
        if (status_out)
            *status_out = "Fetch falhou HTTP " + std::to_string(r.status);
        return false;
    }
    return SaveResponse(path, r, status_out);
}

bool FetchIfChanged(const char* out_json_path, std::string* status_out) {
    const char* path = out_json_path && out_json_path[0] ? out_json_path : "data/rust_offsets.json";
    std::wstring etag;
    {
        std::ifstream e(EtagPath(path));
        std::string s;
        if (e && std::getline(e, s) && !s.empty())
            etag = ToWide(s);
    }
    auto r = HttpGet(L"www.cheatoffsets.com", L"/api/games/rust/current", etag);
    if (r.status == 304) {
        if (status_out) *status_out = "Offsets ja atuais (304)";
        return true;
    }
    if (r.status != 200 || r.body.empty()) {
        if (status_out)
            *status_out = "Fetch falhou HTTP " + std::to_string(r.status);
        return false;
    }
    // Reuse the response already downloaded. The old implementation performed
    // a second request here, which wasted time and could save a different dump.
    return SaveResponse(path, r, status_out);
}

} // namespace OffsetApi
} // namespace Rust
