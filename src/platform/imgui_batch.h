#pragma once

#include <ImGui/imgui.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// ImGui Batch Rendering - Reduces PushStyle/PopStyle calls
// ============================================================

struct StyleBatch {
    ImGuiStyleVar var = ImGuiStyleVar_COUNT;
    float value1 = 0.0f;
    float value2 = 0.0f;
    bool active = false;
};

struct ColorBatch {
    ImGuiCol col = ImGuiCol_COUNT;
    ImU32 color = 0;
    bool active = false;
};

struct FontBatch {
    ImFont* font = nullptr;
    bool active = false;
};

class ImGuiBatchRenderer {
public:
    static constexpr size_t MAX_BATCHES = 64;
    static constexpr size_t MAX_COLORS = 32;
    static constexpr size_t MAX_FONTS = 8;
    
    ImGuiBatchRenderer() = default;
    
    void BeginFrame() {
        styleBatchCount_ = 0;
        colorBatchCount_ = 0;
        fontBatchCount_ = 0;
        currentStyleVar_ = ImGuiStyleVar_COUNT;
        currentColor_ = ImGuiCol_COUNT;
        currentFont_ = nullptr;
        nestedStyleCount_ = 0;
        nestedColorCount_ = 0;
    }
    
    void EndFrame() {
        // Pop all remaining styles
        while (nestedStyleCount_ > 0) {
            ImGui::PopStyleVar();
            nestedStyleCount_--;
        }
        while (nestedColorCount_ > 0) {
            ImGui::PopStyleColor();
            nestedColorCount_--;
        }
        if (currentFont_) {
            ImGui::PopFont();
            currentFont_ = nullptr;
        }
    }
    
    // StyleVar batching - only push if different from current
    void PushStyleVar(ImGuiStyleVar var, float value) {
        if (currentStyleVar_ == var && currentStyleValue1_ == value) {
            nestedStyleCount_++;
            return;
        }
        FlushStyleVar();
        currentStyleVar_ = var;
        currentStyleValue1_ = value;
        currentStyleValue2_ = 0.0f;
        ImGui::PushStyleVar(var, value);
        nestedStyleCount_ = 1;
    }
    
    void PushStyleVar(ImGuiStyleVar var, float value1, float value2) {
        if (currentStyleVar_ == var && currentStyleValue1_ == value1 && currentStyleValue2_ == value2) {
            nestedStyleCount_++;
            return;
        }
        FlushStyleVar();
        currentStyleVar_ = var;
        currentStyleValue1_ = value1;
        currentStyleValue2_ = value2;
        ImGui::PushStyleVar(var, ImVec2(value1, value2));
        nestedStyleCount_ = 1;
    }
    
    void PopStyleVar(int count = 1) {
        if (nestedStyleCount_ > 0) {
            int toPop = std::min(count, nestedStyleCount_);
            for (int i = 0; i < toPop; ++i) {
                ImGui::PopStyleVar();
            }
            nestedStyleCount_ -= toPop;
        }
        if (nestedStyleCount_ == 0) {
            currentStyleVar_ = ImGuiStyleVar_COUNT;
        }
    }
    
    // Color batching
    void PushStyleColor(ImGuiCol col, ImU32 color) {
        if (currentColor_ == col && currentColorValue_ == color) {
            nestedColorCount_++;
            return;
        }
        FlushStyleColor();
        currentColor_ = col;
        currentColorValue_ = color;
        ImGui::PushStyleColor(col, color);
        nestedColorCount_ = 1;
    }
    
    void PushStyleColor(ImGuiCol col, float r, float g, float b, float a) {
        PushStyleColor(col, IM_COL32(
            static_cast<int>(r * 255),
            static_cast<int>(g * 255),
            static_cast<int>(b * 255),
            static_cast<int>(a * 255)
        ));
    }
    
    void PopStyleColor(int count = 1) {
        if (nestedColorCount_ > 0) {
            int toPop = std::min(count, nestedColorCount_);
            for (int i = 0; i < toPop; ++i) {
                ImGui::PopStyleColor();
            }
            nestedColorCount_ -= toPop;
        }
        if (nestedColorCount_ == 0) {
            currentColor_ = ImGuiCol_COUNT;
        }
    }
    
    // Font batching
    void PushFont(ImFont* font) {
        if (currentFont_ == font) {
            fontNesting_++;
            return;
        }
        FlushFont();
        currentFont_ = font;
        fontNesting_ = 1;
        ImGui::PushFont(font);
    }
    
    void PopFont() {
        if (fontNesting_ > 0) {
            fontNesting_--;
            if (fontNesting_ == 0 && currentFont_) {
                ImGui::PopFont();
                currentFont_ = nullptr;
            }
        }
    }
    
    // Scoped helpers
    class ScopedStyleVar {
    public:
        ScopedStyleVar(ImGuiBatchRenderer& renderer, ImGuiStyleVar var, float value)
            : renderer_(renderer) {
            renderer_.PushStyleVar(var, value);
        }
        ScopedStyleVar(ImGuiBatchRenderer& renderer, ImGuiStyleVar var, float v1, float v2)
            : renderer_(renderer) {
            renderer_.PushStyleVar(var, v1, v2);
        }
        ~ScopedStyleVar() { renderer_.PopStyleVar(); }
    private:
        ImGuiBatchRenderer& renderer_;
    };
    
    class ScopedStyleColor {
    public:
        ScopedStyleColor(ImGuiBatchRenderer& renderer, ImGuiCol col, ImU32 color)
            : renderer_(renderer) {
            renderer_.PushStyleColor(col, color);
        }
        ScopedStyleColor(ImGuiBatchRenderer& renderer, ImGuiCol col, float r, float g, float b, float a)
            : renderer_(renderer) {
            renderer_.PushStyleColor(col, r, g, b, a);
        }
        ~ScopedStyleColor() { renderer_.PopStyleColor(); }
    private:
        ImGuiBatchRenderer& renderer_;
    };
    
    class ScopedFont {
    public:
        ScopedFont(ImGuiBatchRenderer& renderer, ImFont* font) : renderer_(renderer) {
            renderer_.PushFont(font);
        }
        ~ScopedFont() { renderer_.PopFont(); }
    private:
        ImGuiBatchRenderer& renderer_;
    };
    
    // Helpers for common patterns
    void PushFrameRounding(float rounding) { PushStyleVar(ImGuiStyleVar_FrameRounding, rounding); }
    void PushFramePadding(float x, float y) { PushStyleVar(ImGuiStyleVar_FramePadding, x, y); }
    void PushItemSpacing(float x, float y) { PushStyleVar(ImGuiStyleVar_ItemSpacing, x, y); }
    void PushWindowPadding(float x, float y) { PushStyleVar(ImGuiStyleVar_WindowPadding, x, y); }
    void PushAlpha(float alpha) { PushStyleVar(ImGuiStyleVar_Alpha, alpha); }
    
    void PushTextColor(ImU32 color) { PushStyleColor(ImGuiCol_Text, color); }
    void PushButtonColor(ImU32 color) { PushStyleColor(ImGuiCol_Button, color); }
    void PushButtonHoveredColor(ImU32 color) { PushStyleColor(ImGuiCol_ButtonHovered, color); }
    void PushButtonActiveColor(ImU32 color) { PushStyleColor(ImGuiCol_ButtonActive, color); }
    void PushBorderColor(ImU32 color) { PushStyleColor(ImGuiCol_Border, color); }
    void PushFrameBgColor(ImU32 color) { PushStyleColor(ImGuiCol_FrameBg, color); }
    void PushWindowBgColor(ImU32 color) { PushStyleColor(ImGuiCol_WindowBg, color); }
    
    // Getters
    [[nodiscard]] int GetNestedStyleCount() const noexcept { return nestedStyleCount_; }
    [[nodiscard]] int GetNestedColorCount() const noexcept { return nestedColorCount_; }
    [[nodiscard]] bool HasActiveFont() const noexcept { return currentFont_ != nullptr; }
    
    // Stats
    struct Stats {
        int stylePushes = 0;
        int stylePops = 0;
        int colorPushes = 0;
        int colorPops = 0;
        int fontPushes = 0;
        int fontPops = 0;
        int redundantAvoided = 0;
    };
    
    [[nodiscard]] Stats GetStats() const noexcept {
        return stats_;
    }
    
    void ResetStats() noexcept {
        stats_ = {};
    }
    
    // Global accessor
    static ImGuiBatchRenderer& Instance() noexcept {
        static ImGuiBatchRenderer instance;
        return instance;
    }

private:
    void FlushStyleVar() {
        if (currentStyleVar_ != ImGuiStyleVar_COUNT && nestedStyleCount_ > 0) {
            for (int i = 0; i < nestedStyleCount_; ++i) {
                ImGui::PopStyleVar();
            }
            stats_.stylePops += nestedStyleCount_;
            nestedStyleCount_ = 0;
        }
    }
    
    void FlushStyleColor() {
        if (currentColor_ != ImGuiCol_COUNT && nestedColorCount_ > 0) {
            for (int i = 0; i < nestedColorCount_; ++i) {
                ImGui::PopStyleColor();
            }
            stats_.colorPops += nestedColorCount_;
            nestedColorCount_ = 0;
        }
    }
    
    void FlushFont() {
        if (currentFont_ && fontNesting_ > 0) {
            ImGui::PopFont();
            stats_.fontPops += fontNesting_;
            fontNesting_ = 0;
        }
    }
    
    // Current state tracking
    int styleBatchCount_ = 0;
    int colorBatchCount_ = 0;
    int fontBatchCount_ = 0;
    ImGuiStyleVar currentStyleVar_ = ImGuiStyleVar_COUNT;
    float currentStyleValue1_ = 0.0f;
    float currentStyleValue2_ = 0.0f;
    int nestedStyleCount_ = 0;
    
    ImGuiCol currentColor_ = ImGuiCol_COUNT;
    ImU32 currentColorValue_ = 0;
    int nestedColorCount_ = 0;
    
    ImFont* currentFont_ = nullptr;
    int fontNesting_ = 0;
    
    // Statistics
    Stats stats_;
};

// Convenience macros
#define BATCH_STYLE_VAR(var, ...) \
    OmniGhost::Platform::ImGuiBatchRenderer::Instance().PushStyleVar(var, __VA_ARGS__)

#define BATCH_STYLE_COLOR(col, ...) \
    OmniGhost::Platform::ImGuiBatchRenderer::Instance().PushStyleColor(col, __VA_ARGS__)

#define BATCH_FONT(font) \
    OmniGhost::Platform::ImGuiBatchRenderer::Instance().PushFont(font)

#define SCOPED_STYLE_VAR(var, ...) \
    OmniGhost::Platform::ImGuiBatchRenderer::ScopedStyleVar _scoped_style_##__LINE__( \
        OmniGhost::Platform::ImGuiBatchRenderer::Instance(), var, __VA_ARGS__)

#define SCOPED_STYLE_COLOR(col, ...) \
    OmniGhost::Platform::ImGuiBatchRenderer::ScopedStyleColor _scoped_color_##__LINE__( \
        OmniGhost::Platform::ImGuiBatchRenderer::Instance(), col, __VA_ARGS__)

#define SCOPED_FONT(font) \
    OmniGhost::Platform::ImGuiBatchRenderer::ScopedFont _scoped_font_##__LINE__( \
        OmniGhost::Platform::ImGuiBatchRenderer::Instance(), font)

} // namespace OmniGhost::Platform