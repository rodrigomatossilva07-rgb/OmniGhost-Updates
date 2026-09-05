#include "brand_assets.h"
#include "logo_data.h"
#include "resource.h"

#include <vector>
#include <iostream>
#include <cstdio>

namespace BrandAssets {
namespace {

    ID3D11ShaderResourceView* g_srv = nullptr;
    int g_w = 0, g_h = 0;
    HICON g_big = nullptr;
    HICON g_small = nullptr;

    bool CreateSrv(ID3D11Device* device, const unsigned char* rgba, int w, int h)
    {
        if (!device || !rgba || w <= 0 || h <= 0)
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = rgba;
        sub.SysMemPitch = static_cast<UINT>(w * 4);

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, &sub, &tex)))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;

        const HRESULT hr = device->CreateShaderResourceView(tex, &srv, &g_srv);
        tex->Release();
        if (FAILED(hr) || !g_srv)
            return false;

        g_w = w;
        g_h = h;
        return true;
    }

    HICON LoadIconFile(HINSTANCE instance, int size)
    {
        HICON icon = nullptr;
        if (instance) {
            icon = static_cast<HICON>(LoadImageW(
                instance, MAKEINTRESOURCEW(IDI_OMNIGHOST_ICON),
                IMAGE_ICON, size, size, 0));
        }
 #if defined(OMNIGHOST_DEV_EXTERNAL_RESOURCES)
        if (!icon) {
            icon = static_cast<HICON>(LoadImageW(
                nullptr, L"resources\\OmniGhost.ico",
                IMAGE_ICON, size, size, LR_LOADFROMFILE));
        }
 #endif
        if (!icon) {
            icon = static_cast<HICON>(LoadImageW(
                nullptr, L"OmniGhost.ico",
                IMAGE_ICON, size, size, LR_LOADFROMFILE));
        }
        return icon;
    }

} // namespace

    bool Initialize(ID3D11Device* device, HINSTANCE instance)
    {
        Shutdown();

        // Prefer embedded high-quality 64x64 RGBA (always available).
        if (!CreateSrv(device, BrandLogoData::kPixels,
                       BrandLogoData::kWidth, BrandLogoData::kHeight)) {
            std::cerr << "[BrandAssets] Failed to create logo texture." << std::endl;
            return false;
        }

        g_big = LoadIconFile(instance, 32);
        g_small = LoadIconFile(instance, 16);
        return true;
    }

    void Shutdown()
    {
        if (g_srv) { g_srv->Release(); g_srv = nullptr; }
        g_w = g_h = 0;
        if (g_big) { DestroyIcon(g_big); g_big = nullptr; }
        if (g_small) { DestroyIcon(g_small); g_small = nullptr; }
    }

    ImTextureID GetLogoTexture()
    {
        return g_srv ? reinterpret_cast<ImTextureID>(g_srv) : nullptr;
    }
    int GetLogoWidth() { return g_w; }
    int GetLogoHeight() { return g_h; }

    void ApplyWindowIcon(HWND hwnd, HINSTANCE instance)
    {
        if (!hwnd) return;
        if (!g_big) g_big = LoadIconFile(instance, 32);
        if (!g_small) g_small = LoadIconFile(instance, 16);
        if (g_big)
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_big));
        if (g_small)
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_small));
    }

} // namespace BrandAssets
