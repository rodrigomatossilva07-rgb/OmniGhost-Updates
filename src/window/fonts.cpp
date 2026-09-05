#include "fonts.h"
#include "../ImGui/imgui_internal.h"
#include "../platform/app_paths.h"

#include <Windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

    bool IsReadableFile(const std::string& path) {
        const std::ifstream file(path, std::ios::binary);
        return file.is_open();
    }

    std::string GetExecutableDirectory() {
        return OmniGhost::Paths::InstallDirectory().string();
    }

    std::string GetWindowsFontPath(const char* filename) {
        if (!filename || !*filename)
            return {};

        std::wstring windowsDirectory(512, L'\0');
        for (;;) {
            const UINT length = GetWindowsDirectoryW(
                windowsDirectory.data(),
                static_cast<UINT>(windowsDirectory.size()));
            if (length == 0)
                return {};
            if (length < windowsDirectory.size()) {
                windowsDirectory.resize(length);
                return (std::filesystem::path(windowsDirectory) / L"Fonts" /
                    std::filesystem::path(filename)).string();
            }
            if (length >= 32768)
                return {};
            windowsDirectory.resize(static_cast<size_t>(length) + 1, L'\0');
        }
    }

    ImFont* AddEmbeddedDefaultFont(ImFontAtlas* atlas, float size_pixels) {
        ImFontConfig config{};
        config.SizePixels = size_pixels;
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        return atlas->AddFontDefault(&config);
    }

    ImFont* LoadFontWithFallback(
        ImFontAtlas* atlas,
        const char* custom_filename,
        const char* windows_filename,
        float size_pixels) {
        const std::string executable_directory = GetExecutableDirectory();
        const std::array<std::string, 5> candidates{
            custom_filename,
            std::string("assets\\fonts\\") + custom_filename,
            executable_directory.empty()
                ? std::string{}
                : executable_directory + "\\" + custom_filename,
            executable_directory.empty()
                ? std::string{}
                : executable_directory + "\\assets\\fonts\\" + custom_filename,
            GetWindowsFontPath(windows_filename)
        };

        for (const std::string& candidate : candidates) {
            // AddFontFromFileTTF asserts when the file cannot be opened, so only
            // call it after verifying that the candidate is actually readable.
            if (candidate.empty() || !IsReadableFile(candidate))
                continue;

            if (ImFont* font = atlas->AddFontFromFileTTF(candidate.c_str(), size_pixels))
                return font;
        }

        // ProggyClean is embedded in Dear ImGui, making this last fallback
        // independent of the working directory and of fonts installed on Windows.
        return AddEmbeddedDefaultFont(atlas, size_pixels);
    }

} // namespace

namespace CyberFonts {

    namespace {
        ImFont* g_title_font = nullptr;
        ImFont* g_body_font = nullptr;
        ImFont* g_mono_font = nullptr;
    }

    void LoadFonts(float scale) {
        ImGuiIO& io = ImGui::GetIO();

        // Load the body face first and explicitly make it the default.  The
        // previous order made every native ImGui control inherit the 24 px
        // display face, which was the main source of the oversized, "debug
        // menu" typography.
        g_body_font = LoadFontWithFallback(
            io.Fonts,
            "Inter-Regular.ttf",
            "segoeui.ttf",
            19.0f * scale);
        g_title_font = LoadFontWithFallback(
            io.Fonts,
            "Inter-SemiBold.ttf",
            "segoeuib.ttf",
            27.0f * scale);
        g_mono_font = LoadFontWithFallback(
            io.Fonts,
            "JetBrainsMono-Regular.ttf",
            "consola.ttf",
            16.5f * scale);

        io.FontDefault = g_body_font;
    }

    void ReloadFonts(float scale) {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        g_title_font = nullptr;
        g_body_font = nullptr;
        g_mono_font = nullptr;
        LoadFonts(scale);
    }

    ImFont* GetTitleFont() { return g_title_font; }
    ImFont* GetBodyFont() { return g_body_font; }
    ImFont* GetMonoFont() { return g_mono_font; }

} // namespace CyberFonts
