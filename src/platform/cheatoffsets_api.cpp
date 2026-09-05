#include "cheatoffsets_api.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include "app_paths.h"
#include "text_encoding.h"
#include "internet_handle.h"
#include <string>
#include <fstream>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace OmniGhost {
namespace CheatOffsets {
namespace {

std::string DataPath(const char* filename) {
    return (OmniGhost::Paths::InstallDirectory() / L"data" / OmniGhost::Platform::Utf8ToWide(filename)).string();
}

struct HttpResult {
    int status = 0;
    std::string body;
    std::string etag;
};

HttpResult HttpGet(const std::wstring& path, const std::wstring& ifNone) {
    HttpResult out;
    auto sess = OmniGhost::Platform::MakeWinHttpHandle(WinHttpOpen(L"OmniGhost-CheatOffsets/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!sess) return out;
    auto conn = OmniGhost::Platform::MakeWinHttpHandle(WinHttpConnect(sess.get(), L"www.cheatoffsets.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!conn) return out;
    auto req = OmniGhost::Platform::MakeWinHttpHandle(WinHttpOpenRequest(conn.get(), L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!req) return out;

    std::wstring hdrs = L"Accept: application/json\r\nUser-Agent: OmniGhost-CheatOffsets/2.0\r\n";
    if (!ifNone.empty())
        hdrs += L"If-None-Match: " + ifNone + L"\r\n";
    WinHttpSendRequest(req.get(),
        hdrs.c_str(), (DWORD)-1,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(req.get(), nullptr);

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    out.status = (int)status;

    wchar_t etagBuf[512]{};
    DWORD etagLen = sizeof(etagBuf);
    if (WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_CUSTOM, L"ETag", etagBuf, &etagLen, WINHTTP_NO_HEADER_INDEX))
        out.etag = OmniGhost::Platform::WideToUtf8(etagBuf);

    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req.get(), &avail) && avail) {
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req.get(), chunk.data(), avail, &read) || !read) break;
        out.body.append(chunk.data(), read);
    }
    return out;
}

std::string EtagFile(const std::string& jsonPath) {
    return jsonPath + ".etag";
}

bool WriteFile(const std::string& path, const std::string& body) {
    auto slash = path.find_last_of("\\/");
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        CreateDirectoryA(dir.c_str(), nullptr);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << body;
    return true;
}

} // namespace

bool FetchGame(const char* slug, const char* out_json_path, std::string* status) {
    if (!slug || !slug[0]) return false;
    const std::string path = out_json_path && out_json_path[0]
        ? std::string(out_json_path)
        : DataPath((std::string(slug) + "_offsets.json").c_str());

    const std::wstring base = L"/api/games/" + OmniGhost::Platform::Utf8ToWide(slug);
    // Prefer compact offsets payload; fall back to full /current document
    auto r = HttpGet(base + L"/current/offsets", L"");
    if (r.status != 200 || r.body.empty())
        r = HttpGet(base + L"/current", L"");
    if (r.status != 200 || r.body.empty()) {
        if (status) *status = std::string(slug) + ": HTTP " + std::to_string(r.status);
        return false;
    }
    if (!WriteFile(path, r.body)) {
        if (status) *status = std::string(slug) + ": write fail " + path;
        return false;
    }
    if (!r.etag.empty())
        WriteFile(EtagFile(path), r.etag);
    if (status)
        *status = std::string(slug) + ": OK " + std::to_string(r.body.size()) + " bytes → " + path;
    return true;
}

bool FetchGameIfChanged(const char* slug, const char* out_json_path, std::string* status) {
    if (!slug || !slug[0]) return false;
    const std::string path = out_json_path && out_json_path[0]
        ? std::string(out_json_path)
        : DataPath((std::string(slug) + "_offsets.json").c_str());

    std::wstring etag;
    {
        std::ifstream e(EtagFile(path));
        std::string s;
        if (e && std::getline(e, s) && !s.empty())
            etag = OmniGhost::Platform::Utf8ToWide(s);
    }
    const std::wstring base = L"/api/games/" + OmniGhost::Platform::Utf8ToWide(slug);
    auto r = HttpGet(base + L"/current/offsets", etag);
    if (r.status != 200 && r.status != 304)
        r = HttpGet(base + L"/current", etag);
    if (r.status == 304) {
        if (status) *status = std::string(slug) + ": already current (304)";
        return true;
    }
    if (r.status != 200 || r.body.empty()) {
        if (status) *status = std::string(slug) + ": HTTP " + std::to_string(r.status);
        return false;
    }
    if (!WriteFile(path, r.body)) {
        if (status) *status = std::string(slug) + ": write fail";
        return false;
    }
    if (!r.etag.empty())
        WriteFile(EtagFile(path), r.etag);
    if (status)
        *status = std::string(slug) + ": updated " + std::to_string(r.body.size()) + " bytes";
    return true;
}

bool FetchRust(std::string* s) {
    // Primary path used by the Rust module and OffsetAuto
    return FetchGameIfChanged("rust", DataPath("rust_offsets.json").c_str(), s);
}
bool FetchCS2(std::string* s) { return FetchGameIfChanged("cs2", nullptr, s); }
bool FetchApex(std::string* s) { return FetchGameIfChanged("apex", nullptr, s); }
bool FetchFiveM(std::string* s) { return FetchGameIfChanged("fivem", nullptr, s); }
bool FetchWarzone(std::string* s) {
    return FetchGameIfChanged("warzone", DataPath("warzone_offsets_api.json").c_str(), s);
}

int FetchAll(std::string* status) {
    int ok = 0;
    std::string all;
    auto one = [&](bool (*fn)(std::string*), const char* name) {
        std::string st;
        if (fn(&st)) ++ok;
        if (!st.empty()) {
            if (!all.empty()) all += " | ";
            all += st;
        }
        (void)name;
    };
    one(FetchRust, "rust");
    one(FetchCS2, "cs2");
    one(FetchApex, "apex");
    one(FetchFiveM, "fivem");
    one(FetchWarzone, "warzone");
    if (status) *status = all;
    return ok;
}

} // namespace CheatOffsets
} // namespace OmniGhost
