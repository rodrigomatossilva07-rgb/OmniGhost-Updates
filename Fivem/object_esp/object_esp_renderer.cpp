#include "object_esp_renderer.h"
#include "math/math.h"
#include "game/offsets.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../../ImGui/imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace object_esp {

static ObjectRenderer* g_renderer = nullptr;

ObjectRenderer& GetObjectRenderer() {
    if (!g_renderer) {
        g_renderer = new ObjectRenderer();
    }
    return *g_renderer;
}

ObjectRenderer::ObjectRenderer() : stats_() {}

ObjectRenderer::~ObjectRenderer() {
    Shutdown();
}

bool ObjectRenderer::Initialize() {
    // Get default font - ImGui context must exist
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) {
        std::cout << "[ObjectESP Renderer] ImGui context not available, deferring font init" << std::endl;
        return true; // Defer font initialization
    }
    
    ImGuiIO& io = ImGui::GetIO();
    font_ = io.FontDefault;
    
    if (!font_) {
        // Fallback to first available font
        if (!io.Fonts->Fonts.empty()) {
            font_ = io.Fonts->Fonts[0];
        }
    }
    
    std::cout << "[ObjectESP Renderer] Initialized" << std::endl;
    return true;
}

void ObjectRenderer::Shutdown() {
    stats_ = RenderStats{};
}

void ObjectRenderer::SetColor(ImU32 color) {
    default_color_ = color;
}

void ObjectRenderer::SetTextScale(float scale) {
    text_scale_ = std::max(0.5f, std::min(3.0f, scale));
}

void ObjectRenderer::SetBoxThickness(float thickness) {
    box_thickness_ = std::max(1.0f, std::min(5.0f, thickness));
}

void ObjectRenderer::Render(const Matrix& view_matrix, uintptr_t /*localplayer*/) {
    auto& manager = object_esp::GetObjectESPManager();
    if (!manager.GetConfig().enabled) return;
    
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    
    stats_.objects_rendered = 0;
    stats_.objects_culled = 0;
    
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    if (!draw_list) return;
    
    for (const auto& obj : manager.GetTrackedObjects()) {
        if (!obj.config.enabled) continue;
        
        Vec2 screen_pos;
        if (!WorldToScreen(obj.entity.position, screen_pos, view_matrix, display_size)) {
            stats_.objects_culled++;
            continue;
        }
        
        // Check if on screen
        if (screen_pos.x < -50 || screen_pos.x > ImGui::GetIO().DisplaySize.x + 50 ||
            screen_pos.y < -50 || screen_pos.y > ImGui::GetIO().DisplaySize.y + 50) {
            stats_.objects_culled++;
            continue;
        }
        
        RenderObject(obj, view_matrix, display_size);
        stats_.objects_rendered++;
    }
    
    auto render_end = std::chrono::high_resolution_clock::now();
    stats_.render_time_ms = std::chrono::duration<float, std::milli>(
        std::chrono::high_resolution_clock::now() - 
        std::chrono::high_resolution_clock::now()).count();
}

bool ObjectRenderer::WorldToScreen(const Vec3& world, Vec2& screen, const Matrix& view_matrix, const ImVec2& display_size) {
    // Transform world position to clip space
    DirectX::XMVECTOR world_vec = DirectX::XMVectorSet(world.x, world.y, world.z, 1.0f);
    DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&view_matrix));
    DirectX::XMVECTOR clip = DirectX::XMVector3Transform(world_vec, view);
    
    float w = DirectX::XMVectorGetW(clip);
    if (w < 0.1f) return false; // Behind camera or too close
    
    float x = DirectX::XMVectorGetX(clip) / w;
    float y = DirectX::XMVectorGetY(clip) / w;
    
    // Convert to screen coordinates
    screen.x = (x + 1.0f) * 0.5f * display_size.x;
    screen.y = (1.0f - y) * 0.5f * display_size.y;
    
    return true;
}

void ObjectRenderer::DrawTextWithOutline(const Vec2& pos, const char* text, ImU32 color, float scale, bool centered) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    if (!draw_list || !font_) return;
    
    ImVec2 text_size = font_->CalcTextSizeA(text_scale_ * scale, FLT_MAX, 0.0f, text);
    ImVec2 draw_pos(pos.x, pos.y);
    
    if (centered) {
        draw_pos.x -= text_size.x * 0.5f;
    }
    
    // Draw outline
    const float outline_thickness = 1.0f * text_scale_ * scale;
    ImU32 outline_color = IM_COL32(0, 0, 0, 200);
    
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;
            draw_list->AddText(font_, text_scale_ * scale, 
                ImVec2(draw_pos.x + dx * outline_thickness, draw_pos.y + dy * outline_thickness),
                outline_color, text);
        }
    }
    
    // Draw main text
    draw_list->AddText(font_, text_scale_ * scale, ImVec2(draw_pos.x, draw_pos.y), color, text);
}

void ObjectRenderer::DrawBox(const Vec2& min, const Vec2& max, ImU32 color, float thickness) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    if (!draw_list) return;
    
    draw_list->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), color, 0.0f, 0, thickness);
}

void ObjectRenderer::DrawMarker(const Vec2& pos, float size, ImU32 color) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    if (!draw_list) return;
    
    draw_list->AddCircleFilled(ImVec2(pos.x, pos.y), size, color, 12);
    draw_list->AddCircle(ImVec2(pos.x, pos.y), size + 1.0f, IM_COL32(0, 0, 0, 180), 12, 2.0f);
}

void ObjectRenderer::DrawDistanceLine(const Vec2& from, const Vec2& to, ImU32 color, float thickness) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    if (!draw_list) return;
    
    draw_list->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), color, thickness);
}

void ObjectRenderer::RenderObject(const TrackedInstance& obj, const Matrix& view_matrix, const ImVec2& display_size) {
    Vec2 screen_pos;
    if (!WorldToScreen(obj.entity.position, screen_pos, view_matrix, display_size)) {
        return;
    }
    
    const auto& entry_config = obj.config;
    float distance = obj.entity.distance;
    ImU32 color = entry_config.color != 0 ? entry_config.color : default_color_;
    
    // Draw marker if enabled
    if (entry_config.show_marker) {
        DrawMarker(screen_pos, 6.0f * text_scale_, color);
    }
    
    // Draw info text
    if (entry_config.show_name || entry_config.show_distance || entry_config.show_category) {
        std::string info;
        
        if (entry_config.show_name) {
            // Use custom display name if set, otherwise fall back to model name
            std::string display_name = entry_config.display_name;
            if (display_name.empty() || display_name == entry_config.model) {
                // Check config for custom display name override
                auto& manager = object_esp::GetObjectESPManager();
                auto it = manager.GetMutableConfig().custom_display_names.find(entry_config.model);
                if (it != manager.GetMutableConfig().custom_display_names.end()) {
                    display_name = it->second;
                }
            }
            info = display_name;
        }
        
        if (entry_config.show_distance) {
            if (!info.empty()) info += " ";
            info += std::to_string(static_cast<int>(distance)) + "m";
        }
        
        if (entry_config.show_category) {
            if (!info.empty()) info += " ";
            info += "[" + std::string(ObjectCategoryToString(entry_config.category)) + "]";
        }
        
        if (!info.empty()) {
            DrawTextWithOutline(Vec2(screen_pos.x, screen_pos.y - 20.0f * text_scale_), 
                info.c_str(), color, text_scale_, true);
        }
    }
    
    // Draw box if enabled
    if (entry_config.show_box) {
        // Estimate box size based on distance
        float box_size = std::max(20.0f, 100.0f / std::max(1.0f, distance * 0.1f)) * text_scale_;
        Vec2 min(screen_pos.x - box_size * 0.5f, screen_pos.y - box_size * 0.5f);
        Vec2 max(screen_pos.x + box_size * 0.5f, screen_pos.y + box_size * 0.5f);
        DrawBox(min, max, color, box_thickness_);
    }
}

} // namespace object_esp
