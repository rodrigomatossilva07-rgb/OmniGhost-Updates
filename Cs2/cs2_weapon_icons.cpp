#include "cs2_weapon_icons.h"
#include "../src/platform/app_paths.h"
#include "../src/platform/embedded_resources.h"

#include <Windows.h>
#include <wincodec.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <future>
#include <chrono>

#pragma comment(lib, "windowscodecs.lib")

namespace CS2_WeaponIcons {
namespace {

struct IconTex {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
};

struct IconPixels {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    [[nodiscard]] bool valid() const noexcept { return !rgba.empty() && w > 0 && h > 0; }
};

std::unordered_map<int, IconTex> g_icons;
std::unordered_map<int, IconPixels> g_cpu_icons;
std::unordered_map<int, std::future<IconPixels>> g_pending_icons;
bool g_attempted = false;
ID3D11Device* g_device = nullptr;

const char* StemFromDef(int def) {
    switch (def) {
    case 1: return "weapon_deagle";
    case 2: return "weapon_elite";
    case 3: return "weapon_fiveseven";
    case 4: return "weapon_glock";
    case 7: return "weapon_ak47";
    case 8: return "weapon_aug";
    case 9: return "weapon_awp";
    case 10: return "weapon_famas";
    case 11: return "weapon_g3sg1";
    case 13: return "weapon_galilar";
    case 14: return "weapon_m249";
    case 16: return "weapon_m4a1";
    case 17: return "weapon_mac10";
    case 19: return "weapon_p90";
    case 23: return "weapon_mp5sd";
    case 24: return "weapon_ump45";
    case 25: return "weapon_xm1014";
    case 26: return "weapon_bizon";
    case 27: return "weapon_mag7";
    case 28: return "weapon_negev";
    case 29: return "weapon_sawedoff";
    case 30: return "weapon_tec9";
    case 31: return "weapon_taser";
    case 32: return "weapon_hkp2000";
    case 33: return "weapon_mp7";
    case 34: return "weapon_mp9";
    case 35: return "weapon_nova";
    case 36: return "weapon_p250";
    case 38: return "weapon_scar20";
    case 39: return "weapon_sg556";
    case 40: return "weapon_ssg08";
    case 41: case 42: case 59: return "weapon_knife";
    case 43: return "weapon_flashbang";
    case 44: return "weapon_hegrenade";
    case 45: return "weapon_smokegrenade";
    case 46: return "weapon_molotov";
    case 47: return "weapon_decoy";
    case 48: return "weapon_incgrenade";
    case 49: return "weapon_c4";
    case 60: return "weapon_m4a1_silencer";
    case 61: return "weapon_usp_silencer";
    case 63: return "weapon_cz75a";
    case 64: return "weapon_revolver";
    // Individual knives (item definition indexes)
    case 500: return "weapon_bayonet";
    case 503: return "weapon_knife_css";           // Classic
    case 505: return "weapon_knife_flip";
    case 506: return "weapon_knife_gut";
    case 507: return "weapon_knife_karambit";
    case 508: return "weapon_knife_m9_bayonet";
    case 509: return "weapon_knife_tactical";      // Huntsman
    case 512: return "weapon_knife_falchion";
    case 514: return "weapon_knife_bowie";         // Bowie / survival_bowie
    case 515: return "weapon_knife_butterfly";
    case 516: return "weapon_knife_push";          // Shadow Daggers
    case 517: return "weapon_knife_cord";          // Paracord
    case 518: return "weapon_knife_canis";         // Survival
    case 519: return "weapon_knife_ursus";
    case 520: return "weapon_knife_gypsy_jackknife"; // Navaja
    case 521: return "weapon_knife_outdoor";       // Nomad
    case 522: return "weapon_knife_stiletto";
    case 523: return "weapon_knife_widowmaker";    // Talon
    case 525: return "weapon_knife_skeleton";
    case 526: return "weapon_knife_kukri";
    default:
        if (def >= 500 && def <= 530) return "weapon_knife";
        return nullptr;
    }
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

bool LoadPngRgba(const std::wstring& path, std::vector<uint8_t>& out, int& w, int& h) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) || !factory)
        return false;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) { factory->Release(); return false; }

    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) {
        decoder->Release(); factory->Release(); return false;
    }

    IWICFormatConverter* conv = nullptr;
    if (FAILED(factory->CreateFormatConverter(&conv)) || !conv) {
        frame->Release(); decoder->Release(); factory->Release(); return false;
    }

    if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom))) {
        conv->Release(); frame->Release(); decoder->Release(); factory->Release();
        return false;
    }

    UINT uw = 0, uh = 0;
    conv->GetSize(&uw, &uh);
    w = (int)uw; h = (int)uh;
    if (w <= 0 || h <= 0) {
        conv->Release(); frame->Release(); decoder->Release(); factory->Release();
        return false;
    }

    out.resize((size_t)w * (size_t)h * 4);
    hr = conv->CopyPixels(nullptr, (UINT)(w * 4), (UINT)out.size(), out.data());
    conv->Release(); frame->Release(); decoder->Release(); factory->Release();
    return SUCCEEDED(hr);
}

bool LoadPngRgbaMemory(const std::vector<std::uint8_t>& bytes,
                       std::vector<uint8_t>& out, int& w, int& h) {
    if (bytes.empty() || bytes.size() > MAXDWORD) return false;
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) || !factory) return false;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    if (FAILED(factory->CreateStream(&stream))) goto cleanup;
    if (FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bytes.data())),
            static_cast<DWORD>(bytes.size())))) goto cleanup;
    if (FAILED(factory->CreateDecoderFromStream(stream, nullptr,
            WICDecodeMetadataCacheOnLoad, &decoder))) goto cleanup;
    if (FAILED(decoder->GetFrame(0, &frame))) goto cleanup;
    if (FAILED(factory->CreateFormatConverter(&conv))) goto cleanup;
    if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) goto cleanup;
    {
        UINT uw = 0, uh = 0;
        if (FAILED(conv->GetSize(&uw, &uh)) || !uw || !uh || uw > 4096 || uh > 4096) goto cleanup;
        const std::size_t size = static_cast<std::size_t>(uw) * uh * 4u;
        if (size > MAXDWORD) goto cleanup;
        out.resize(size); w = static_cast<int>(uw); h = static_cast<int>(uh);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, uw * 4u,
            static_cast<UINT>(out.size()), out.data()));
    }
cleanup:
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    factory->Release();
    if (!ok) out.clear();
    return ok;
}

bool CreateSrv(ID3D11Device* device, const uint8_t* rgba, int w, int h, IconTex& out) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = rgba;
    sub.SysMemPitch = (UINT)(w * 4);

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &sub, &tex)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* view = nullptr;
    HRESULT hr = device->CreateShaderResourceView(tex, &srv, &view);
    tex->Release();
    if (FAILED(hr) || !view) return false;

    out.srv = view;
    out.w = w;
    out.h = h;
    return true;
}

std::filesystem::path WeaponIconCacheDir() {
    return OmniGhost::Paths::Cache() / L"cs2" / L"weapons";
}

std::vector<std::filesystem::path> IconSearchDirs() {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs;
    try {
        // Prefer persistent LocalAppData cache so icons are not re-decoded
        // from embedded resources on every process start.
        dirs.push_back(WeaponIconCacheDir());
        const fs::path install = OmniGhost::Paths::InstallDirectory();
        const fs::path exe = OmniGhost::Paths::Executable().parent_path();
        dirs.push_back(install / "data" / "weapons");
        dirs.push_back(install / "Cs2" / "data" / "weapons");
        dirs.push_back(exe / "data" / "weapons");
        dirs.push_back(exe / "Cs2" / "data" / "weapons");
        dirs.push_back(exe.parent_path() / "Cs2" / "data" / "weapons");
        dirs.push_back(fs::current_path() / "Cs2" / "data" / "weapons");
        dirs.push_back(fs::current_path() / "data" / "weapons");
    } catch (const std::exception& exception) {
        std::cerr << "[CS2][WeaponIcons] Falha ao enumerar pastas: "
                  << exception.what() << std::endl;
    } catch (...) {
        std::cerr << "[CS2][WeaponIcons] Falha não identificada ao enumerar pastas." << std::endl;
    }
    return dirs;
}

IconPixels DecodeOne(int def) {
    IconPixels result{};
    const char* stem = StemFromDef(def);
    if (!stem) return result;

    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (const auto& dir : IconSearchDirs()) {
        const auto path = dir / (std::string(stem) + ".png");
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        if (LoadPngRgba(path.wstring(), result.rgba, result.w, result.h)) break;
    }

    if (!result.valid()) {
        const std::string logical = std::string("cs2/weapons/") + stem + ".png";
        OmniGhost::EmbeddedResourceDiagnostics diagnostics;
        const auto bytes = OmniGhost::LoadEmbeddedResource(logical, &diagnostics);
        if (bytes) {
            try {
                const auto cachePath = WeaponIconCacheDir() / (std::string(stem) + ".png");
                std::filesystem::create_directories(cachePath.parent_path());
                std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
                if (out) out.write(reinterpret_cast<const char*>(bytes->data()),
                                   static_cast<std::streamsize>(bytes->size()));
            } catch (...) {
                OutputDebugStringA("[CS2][WeaponIcons] cache write failed\n");
            }
            LoadPngRgbaMemory(*bytes, result.rgba, result.w, result.h);
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return result;
}

void RequestOne(int def) {
    if (def <= 0 || !StemFromDef(def) || g_icons.count(def) || g_cpu_icons.count(def) ||
        g_pending_icons.count(def)) return;
    // Cap background decode concurrency. A newly requested definition that
    // misses the cap is retried by Get() on a following frame.
    std::size_t activeWorkers = 0;
    for (auto& [_, future] : g_pending_icons) {
        if (future.valid() && future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            ++activeWorkers;
    }
    if (activeWorkers >= 1) return; // single worker — avoid thread explosion
    if (g_pending_icons.size() >= 8) return;
    g_pending_icons.emplace(def, std::async(std::launch::async, [def] { return DecodeOne(def); }));
}

void PumpUploads(ID3D11Device* device, int max_uploads) {
    if (!device || max_uploads <= 0) return;

    // Collect completed CPU decodes without waiting. WIC/filesystem work stays off render.
    for (auto it = g_pending_icons.begin(); it != g_pending_icons.end();) {
        if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        IconPixels pixels = it->second.get();
        const int def = it->first;
        it = g_pending_icons.erase(it);
        if (pixels.valid()) g_cpu_icons[def] = std::move(pixels);
    }

    int uploaded = 0;
    for (auto& [def, pixels] : g_cpu_icons) {
        if (uploaded >= max_uploads) break;
        if (g_icons.count(def) || !pixels.valid()) continue;
        IconTex tex{};
        if (CreateSrv(device, pixels.rgba.data(), pixels.w, pixels.h, tex)) {
            g_icons[def] = tex;
            ++uploaded;
        }
    }
}

} // namespace

const char* FileStem(int weapon_def) { return StemFromDef(weapon_def); }

void EnsureLoaded(ID3D11Device* device) {
    if (!device) return;
    if (g_device && g_device != device) {
        for (auto& [definition, icon] : g_icons) {
            (void)definition;
            if (icon.srv) icon.srv->Release();
        }
        g_icons.clear();
    }
    g_device = device;
    g_attempted = true;
    // One D3D upload per render frame caps the worst-case frametime cost.
    PumpUploads(device, 1);
}

ID3D11ShaderResourceView* Get(int weapon_def) {
    if (weapon_def <= 0) return nullptr;
    auto it = g_icons.find(weapon_def);
    if (it != g_icons.end()) return it->second.srv;

    RequestOne(weapon_def);
    if (weapon_def >= 500 && weapon_def <= 530)
        RequestOne(41); // generic knife fallback, also decoded asynchronously

    if (weapon_def >= 500 && weapon_def <= 530) {
        it = g_icons.find(41);
        if (it != g_icons.end()) return it->second.srv;
    }
    return nullptr;
}

void GetSize(int weapon_def, int& out_w, int& out_h) {
    out_w = out_h = 0;
    auto it = g_icons.find(weapon_def);
    if (it == g_icons.end() && weapon_def >= 500 && weapon_def <= 530)
        it = g_icons.find(41);
    if (it != g_icons.end()) { out_w = it->second.w; out_h = it->second.h; return; }
    auto cpu = g_cpu_icons.find(weapon_def);
    if (cpu == g_cpu_icons.end() && weapon_def >= 500 && weapon_def <= 530)
        cpu = g_cpu_icons.find(41);
    if (cpu != g_cpu_icons.end()) { out_w = cpu->second.w; out_h = cpu->second.h; }
}

void Shutdown() {
    for (auto& kv : g_icons) {
        if (kv.second.srv) kv.second.srv->Release();
    }
    g_icons.clear();
    // Pending futures are allowed to finish during orderly shutdown; no render frame is blocked.
    g_pending_icons.clear();
    g_cpu_icons.clear();
    g_attempted = false;
    g_device = nullptr;
}

} // namespace CS2_WeaponIcons
