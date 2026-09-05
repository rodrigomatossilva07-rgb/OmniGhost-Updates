#include "launcher_assets.h"
#include "../platform/app_paths.h"
#include "../platform/embedded_resources.h"
#include "../window/theme.h"

#include <Windows.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

namespace LauncherAssets {
namespace {

struct LoadedTexture {
    Launcher::GameId game = Launcher::GameId::None;
    bool banner = false;
    ID3D11ShaderResourceView* view = nullptr;
    int width = 0;
    int height = 0;
};

std::vector<LoadedTexture> g_textures;
LoadedTexture g_rust_preview;
IWICImagingFactory* g_factory = nullptr;


fs::path ResolveLocalPath(const char* relative) {
    if (!relative || !*relative) return {};
    const fs::path requested(relative);
    std::error_code error;
    if (requested.is_absolute() && fs::exists(requested, error))
        return requested;

    const fs::path executable = OmniGhost::Paths::InstallDirectory();
    // Assets are local-only and resolved relative to the controlled install root.
    // The process current directory is deliberately excluded so another folder
    // cannot shadow a shipped image when OmniGhost is launched from a shortcut.
    const fs::path candidates[] = {
        executable / requested,
        executable / "resources" / requested
    };
    for (const fs::path& candidate : candidates) {
        error.clear();
        if (fs::exists(candidate, error) && fs::is_regular_file(candidate, error))
            return candidate;
    }
    return {};
}

bool LoadPng(ID3D11Device* device, const fs::path& path, LoadedTexture& output) {
    if (!device || !g_factory || path.empty()) return false;

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* texture = nullptr;
    UINT width = 0;
    UINT height = 0;
    bool loaded = false;

    if (FAILED(g_factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder)))
        goto cleanup;
    if (FAILED(decoder->GetFrame(0, &frame)))
        goto cleanup;
    if (FAILED(g_factory->CreateFormatConverter(&converter)))
        goto cleanup;
    if (FAILED(converter->Initialize(
            frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom)))
        goto cleanup;

    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
        goto cleanup;

    {
        const UINT stride = width * 4U;
        std::vector<unsigned char> pixels(static_cast<std::size_t>(stride) * height);
        if (FAILED(converter->CopyPixels(nullptr, stride,
                static_cast<UINT>(pixels.size()), pixels.data())))
            goto cleanup;

        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA source{};
        source.pSysMem = pixels.data();
        source.SysMemPitch = stride;
        if (FAILED(device->CreateTexture2D(&description, &source, &texture)))
            goto cleanup;

        D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
        view_description.Format = description.Format;
        view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view_description.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(
                texture, &view_description, &output.view)))
            goto cleanup;

        output.width = static_cast<int>(width);
        output.height = static_cast<int>(height);
        loaded = true;
    }

cleanup:
    if (texture) texture->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    return loaded;
}

bool LoadPngMemory(ID3D11Device* device, const std::vector<std::uint8_t>& bytes,
                   LoadedTexture& output) {
    if (!device || !g_factory || bytes.empty() || bytes.size() > MAXDWORD) return false;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* texture = nullptr;
    UINT width = 0, height = 0;
    bool loaded = false;
    if (FAILED(g_factory->CreateStream(&stream))) goto cleanup;
    if (FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bytes.data())),
            static_cast<DWORD>(bytes.size())))) goto cleanup;
    if (FAILED(g_factory->CreateDecoderFromStream(stream, nullptr,
            WICDecodeMetadataCacheOnLoad, &decoder))) goto cleanup;
    if (FAILED(decoder->GetFrame(0, &frame))) goto cleanup;
    if (FAILED(g_factory->CreateFormatConverter(&converter))) goto cleanup;
    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) goto cleanup;
    if (FAILED(converter->GetSize(&width, &height)) || !width || !height ||
        width > 16384 || height > 16384) goto cleanup;
    {
        const std::size_t pixelBytes = static_cast<std::size_t>(width) * height * 4u;
        if (pixelBytes > 512u * 1024u * 1024u || pixelBytes > MAXDWORD) goto cleanup;
        const UINT stride = width * 4u;
        std::vector<unsigned char> pixels(pixelBytes);
        if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()))) goto cleanup;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width; description.Height = height; description.MipLevels = 1;
        description.ArraySize = 1; description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1; description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA source{}; source.pSysMem = pixels.data(); source.SysMemPitch = stride;
        if (FAILED(device->CreateTexture2D(&description, &source, &texture))) goto cleanup;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = description.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(texture, &view, &output.view))) goto cleanup;
        output.width = static_cast<int>(width); output.height = static_cast<int>(height); loaded = true;
    }
cleanup:
    if (texture) texture->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    return loaded;
}

bool LoadAsset(ID3D11Device* device, const char* logicalName, LoadedTexture& output) {
    if (!logicalName || !*logicalName) return false;
#if defined(OMNIGHOST_DEV_EXTERNAL_RESOURCES)
    if (LoadPng(device, ResolveLocalPath(logicalName), output)) return true;
#endif
    OmniGhost::EmbeddedResourceDiagnostics diagnostics;
    const auto bytes = OmniGhost::LoadEmbeddedResource(logicalName, &diagnostics);
    if (!bytes) {
        std::clog << "[RESOURCE] id=" << logicalName << " load=FAIL reason="
                  << diagnostics.error << '\n';
        return false;
    }
    return LoadPngMemory(device, *bytes, output);
}

Texture Find(Launcher::GameId game, bool banner) {
    for (const LoadedTexture& texture : g_textures) {
        if (texture.game == game && texture.banner == banner && texture.view)
            return { reinterpret_cast<ImTextureID>(texture.view),
                     texture.width, texture.height };
    }
    return {};
}

} // namespace

void Initialize(ID3D11Device* device) {
    Shutdown();
    if (!device) return;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&g_factory))))
        return;

    std::size_t count = 0;
    const Launcher::GameDefinition* games = Launcher::Games(count);
    g_textures.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        const struct Source { const char* path; bool banner; } sources[] = {
            { games[i].logo_path, false },
            { games[i].banner_path, true }
        };
        for (const Source& source : sources) {
            LoadedTexture texture{};
            texture.game = games[i].launch_id;
            texture.banner = source.banner;
            if (LoadAsset(device, source.path, texture))
                g_textures.push_back(texture);
        }
    }

    g_rust_preview = {};
    LoadAsset(device, "resources/games/rust/esp_preview.png", g_rust_preview);
}

void Shutdown() {
    for (LoadedTexture& texture : g_textures) {
        if (texture.view) texture.view->Release();
    }
    g_textures.clear();
    if (g_rust_preview.view) {
        g_rust_preview.view->Release();
        g_rust_preview = {};
    }
    if (g_factory) {
        g_factory->Release();
        g_factory = nullptr;
    }
}

Texture Logo(Launcher::GameId game) { return Find(game, false); }
Texture Banner(Launcher::GameId game) { return Find(game, true); }
Texture RustEspPreview() {
    return { reinterpret_cast<ImTextureID>(g_rust_preview.view),
             g_rust_preview.width, g_rust_preview.height };
}

} // namespace LauncherAssets
