#pragma once
#include "object_esp.h"
#include "math/math.h"
#include "../../ImGui/imgui.h"
#include <vector>
#include <string>

namespace object_esp {

// Renderer for Object ESP
class ObjectRenderer {
public:
    ObjectRenderer();
    ~ObjectRenderer();
    
    // Initialize renderer resources
    bool Initialize();
    void Shutdown();
    
    // Main render function (call from render loop)
    void Render(const Matrix& view_matrix, uintptr_t localplayer);
    
    // Settings
    void SetColor(ImU32 color);
    void SetTextScale(float scale);
    void SetBoxThickness(float thickness);
    
    // Statistics
    struct RenderStats {
        int objects_rendered = 0;
        int objects_culled = 0;
        float render_time_ms = 0.0f;
    };
    const RenderStats& GetStats() const { return stats_; }

private:
    RenderStats stats_;
    ImU32 default_color_ = IM_COL32(255, 255, 0, 255); // Yellow
    float text_scale_ = 1.0f;
    float box_thickness_ = 2.0f;
    
    // Font
    ImFont* font_ = nullptr;
    
    // Render helpers
    bool WorldToScreen(const Vec3& world, Vec2& screen, const Matrix& view_matrix, const ImVec2& display_size);
    void DrawTextWithOutline(const Vec2& pos, const char* text, ImU32 color, float scale, bool centered = true);
    void DrawBox(const Vec2& min, const Vec2& max, ImU32 color, float thickness);
    void DrawMarker(const Vec2& pos, float size, ImU32 color);
    void DrawDistanceLine(const Vec2& from, const Vec2& to, ImU32 color, float thickness);
    
    // Render a single tracked object
    void RenderObject(const TrackedInstance& obj, const Matrix& view_matrix, const ImVec2& display_size);
    
    // Draw info text for object
    void DrawObjectInfo(const TrackedInstance& obj, const Vec2& screen_pos, float distance);
};

// Global renderer instance
ObjectRenderer& GetObjectRenderer();

} // namespace object_esp