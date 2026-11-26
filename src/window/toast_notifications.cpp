#include "toast_notifications.h"
#include "theme.h"
#include "icons.h"
#include <algorithm>
#include <random>

namespace OmniGhost::UI::Toast {

namespace {

struct ToastInternal : Toast {
    float current_y = -1.0f;      // para stack animation
    float target_y = -1.0f;       // posição alvo no stack
    float visibility = 0.0f;      // 0..1 para enter/exit
    bool exiting = false;         // em animação de saída
    bool marked_for_removal = false;
};

ToastConfig g_config;
std::vector<ToastInternal> g_toasts;
std::mutex g_mutex;
uint64_t g_next_id = 1;
DismissCallback g_dismiss_callback = nullptr;
std::mt19937 g_rng{ static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()) };

ImU32 GetTypeColor(ToastType type, ImU32 custom) {
    switch (type) {
        case ToastType::Info:    return CyberTheme::U32(CyberTheme::Colors.Info);
        case ToastType::Success: return CyberTheme::U32(CyberTheme::Colors.Success);
        case ToastType::Warning: return CyberTheme::U32(CyberTheme::Colors.Warning);
        case ToastType::Error:   return CyberTheme::U32(CyberTheme::Colors.Error);
        case ToastType::Custom:  return custom;
    }
    return CyberTheme::U32(CyberTheme::Colors.Accent);
}

const char* GetTypeIcon(ToastType type) {
    switch (type) {
        case ToastType::Info:    return ICON_FA_INFO_CIRCLE;
        case ToastType::Success: return ICON_FA_CHECK_CIRCLE;
        case ToastType::Warning: return ICON_FA_EXCLAMATION_TRIANGLE;
        case ToastType::Error:   return ICON_FA_TIMES_CIRCLE;
        case ToastType::Custom:  return ICON_FA_BELL;
    }
    return ICON_FA_BELL;
}

float GetScreenWidth() {
    return ImGui::GetIO().DisplaySize.x;
}
float GetScreenHeight() {
    return ImGui::GetIO().DisplaySize.y;
}

} // anonymous namespace

void Initialize(const ToastConfig& config) {
    g_config = config;
    g_toasts.clear();
    g_next_id = 1;
}

void Shutdown() {
    std::lock_guard lock(g_mutex);
    g_toasts.clear();
    g_dismiss_callback = nullptr;
}

uint64_t Push(std::string_view title, std::string_view message,
              ToastType type, std::string_view icon,
              bool pinned, bool centered, ImU32 custom_color) {
    std::lock_guard lock(g_mutex);

    // Limite máximo
    if (g_toasts.size() >= g_config.max_toasts) {
        // Remove o mais antigo não-pinned
        auto it = std::find_if(g_toasts.begin(), g_toasts.end(),
                               [](const ToastInternal& t) { return !t.pinned; });
        if (it != g_toasts.end()) {
            if (g_dismiss_callback) g_dismiss_callback(it->id);
            g_toasts.erase(it);
        } else if (!g_toasts.empty()) {
            // Todos pinned - remove o mais antigo
            if (g_dismiss_callback) g_dismiss_callback(g_toasts.front().id);
            g_toasts.erase(g_toasts.begin());
        }
    }

    ToastInternal toast;
    toast.id = g_next_id++;
    toast.title = title;
    toast.message = message;
    toast.type = type;
    toast.icon = icon.empty() ? GetTypeIcon(type) : std::string(icon);
    toast.pinned = pinned;
    toast.centered = centered;
    toast.custom_color = custom_color;
    toast.created_at = std::chrono::steady_clock::now();
    toast.age = 0.0f;
    toast.visibility = 0.0f;
    toast.exiting = false;
    toast.marked_for_removal = false;
    toast.current_y = -1.0f;
    toast.target_y = -1.0f;

    g_toasts.push_back(std::move(toast));
    return toast.id;
}

uint64_t PushCustom(std::string_view title, std::string_view message,
                    ImU32 color, std::string_view icon,
                    bool pinned, bool centered) {
    return Push(title, message, ToastType::Custom, icon, pinned, centered, color);
}

bool Dismiss(uint64_t id) {
    std::lock_guard lock(g_mutex);
    auto it = std::find_if(g_toasts.begin(), g_toasts.end(),
                           [id](const ToastInternal& t) { return t.id == id; });
    if (it != g_toasts.end()) {
        if (!it->pinned && !it->exiting) {
            it->exiting = true;
            return true;
        }
    }
    return false;
}

void DismissAll() {
    std::lock_guard lock(g_mutex);
    for (auto& toast : g_toasts) {
        if (!toast.exiting && !toast.marked_for_removal) {
            toast.exiting = true;
        }
    }
}

void Unpin(uint64_t id) {
    std::lock_guard lock(g_mutex);
    auto it = std::find_if(g_toasts.begin(), g_toasts.end(),
                           [id](const ToastInternal& t) { return t.id == id; });
    if (it != g_toasts.end()) {
        it->pinned = false;
    }
}

void Update(float delta_time) {
    std::lock_guard lock(g_mutex);

    const float lifetime = g_config.enter_duration + g_config.hold_duration + g_config.exit_duration;

    for (auto& toast : g_toasts) {
        if (toast.marked_for_removal) continue;

        toast.age += delta_time;

        // Enter phase
        if (!toast.exiting && toast.age < g_config.enter_duration) {
            float t = toast.age / g_config.enter_duration;
            // Ease out quad
            toast.visibility = 1.0f - (1.0f - t) * (1.0f - t);
        }
        // Hold phase
        else if (!toast.exiting && toast.age < g_config.enter_duration + g_config.hold_duration) {
            toast.visibility = 1.0f;
        }
        // Exit phase (auto-expire ou dismissed)
        else if (toast.exiting || toast.age >= lifetime) {
            if (!toast.exiting) {
                toast.exiting = true;
            }
            float exit_elapsed = toast.age - (g_config.enter_duration + g_config.hold_duration);
            if (exit_elapsed < 0) exit_elapsed = 0;
            float t = exit_elapsed / g_config.exit_duration;
            t = std::clamp(t, 0.0f, 1.0f);
            // Ease in quad
            toast.visibility = 1.0f - t * t;

            if (t >= 1.0f) {
                toast.marked_for_removal = true;
            }
        }

        // Pinned toasts não expiram (só saem se dismissed)
        if (toast.pinned && !toast.exiting) {
            toast.visibility = 1.0f;
            toast.age = 0.0f; // reset age para não trigger auto-exit
        }
    }

    // Remove marcados
    if (g_dismiss_callback) {
        for (auto& toast : g_toasts) {
            if (toast.marked_for_removal && toast.id != 0) {
                g_dismiss_callback(toast.id);
                toast.id = 0; // evita double-call
            }
        }
    }
    g_toasts.erase(
        std::remove_if(g_toasts.begin(), g_toasts.end(),
                       [](const ToastInternal& t) { return t.marked_for_removal; }),
        g_toasts.end());
}

void Render() {
    if (g_toasts.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    const float sw = GetScreenWidth();
    const float sh = GetScreenHeight();
    const float rx = sw > 0 ? sw / 1920.0f : 1.0f;
    const float ry = sh > 0 ? sh / 1080.0f : 1.0f;
    const float scale = std::min(rx, ry);

    const float w = g_config.width * scale;
    const float h = g_config.height * scale;
    const float gap = g_config.gap * scale;
    const float margin = g_config.margin * scale;

    std::lock_guard lock(g_mutex);

    // Calcula target_y para cada toast (stack)
    for (size_t i = 0; i < g_toasts.size(); ++i) {
        auto& toast = g_toasts[i];
        if (toast.centered) continue; // centered não faz stack

        float target = margin + static_cast<float>(i) * (h + gap);
        toast.target_y = target;

        if (toast.current_y < 0) {
            toast.current_y = target; // primeira vez
        } else {
            // Suaviza movimento do stack
            float ease = 1.0f - std::exp(-io.DeltaTime * g_config.stack_ease_rate);
            toast.current_y += (target - toast.current_y) * ease;
        }
    }

    // Render cada toast
    for (auto& toast : g_toasts) {
        if (toast.marked_for_removal || toast.visibility <= 0.001f) continue;

        float x, y;

        if (toast.centered) {
            // Centralizado no ecrã (style "grande")
            x = (sw - w) * 0.5f;
            float rest_y = 48.0f * scale;
            float drop_d = rest_y + h;

            if (toast.age < g_config.enter_duration) {
                float p = std::clamp(toast.age / g_config.enter_duration, 0.0f, 1.0f);
                float ease = 1.0f - (1.0f - p) * (1.0f - p);
                y = rest_y - drop_d * (1.0f - ease);
            } else if (toast.exiting) {
                float exit_elapsed = toast.age - (g_config.enter_duration + g_config.hold_duration);
                if (exit_elapsed < 0) exit_elapsed = 0;
                float t = std::clamp(exit_elapsed / g_config.exit_duration, 0.0f, 1.0f);
                y = rest_y - drop_d * t * t;
            } else {
                y = rest_y;
            }
        } else {
            // Stack à direita
            x = sw - margin - w;
            y = toast.current_y;

            // Slide in horizontal
            if (toast.age < g_config.enter_duration) {
                float t = std::clamp(toast.age / g_config.enter_duration, 0.0f, 1.0f);
                float ease = 1.0f - (1.0f - t) * (1.0f - t);
                x += (1.0f - ease) * 50.0f * scale;
            } else if (toast.exiting) {
                float exit_elapsed = toast.age - (g_config.enter_duration + g_config.hold_duration);
                if (exit_elapsed < 0) exit_elapsed = 0;
                float t = std::clamp(exit_elapsed / g_config.exit_duration, 0.0f, 1.0f);
                float ease = t * t;
                x += ease * 50.0f * scale;
            }
        }

        auto alpha = [&](float base) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(base * toast.visibility, 0.0f, 255.0f));
        };

        ImU32 color = GetTypeColor(toast.type, toast.custom_color);
        ImU32 color_bg = IM_COL32(10, 9, 18, alpha(255));
        ImU32 color_bg2 = IM_COL32(28, 25, 40, alpha(220));
        ImU32 color_border = IM_COL32(
            (color >> 16) & 0xFF,
            (color >> 8) & 0xFF,
            color & 0xFF,
            alpha(150)
        );
        ImU32 color_accent = IM_COL32(
            (color >> 16) & 0xFF,
            (color >> 8) & 0xFF,
            color & 0xFF,
            alpha(255)
        );
        ImU32 color_text = IM_COL32(255, 255, 255, alpha(255));
        ImU32 color_text_dim = IM_COL32(210, 205, 225, alpha(235));

        // Background com padrão de dots pulsante (estilo crisis-fortnite)
        const float corner_radius = 12.0f * scale;

        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), color_bg, corner_radius);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), color_bg2, corner_radius);

        // Padrão de dots animado
        const float dot_spacing = 20.0f * scale;
        const float outer_dot_radius = 2.5f * scale;
        const ImVec2 pattern_center{ w * 0.5f, h * 0.5f };
        const float max_dist = std::sqrt(pattern_center.x * pattern_center.x + pattern_center.y * pattern_center.y);
        const float anim_time = static_cast<float>(ImGui::GetTime());

        dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);

        for (float dy = 0; dy < h; dy += dot_spacing) {
            for (float dx = 0; dx < w; dx += dot_spacing) {
                float ddx = dx - pattern_center.x;
                float ddy = dy - pattern_center.y;
                float dist = std::sqrt(ddx * ddx + ddy * ddy);
                float fade = std::max(0.0f, 1.0f - dist / max_dist);
                float pulse = 0.5f + 0.5f * std::sin(anim_time * 2.0f + (dx + dy) * 0.05f);

                float opacity = 0.85f * fade * pulse;
                if (opacity <= 0.05f) continue;

                uint8_t dot_alpha = alpha(opacity * 255.0f);
                ImVec2 dot_pos{ x + dx + 1.0f * scale, y + dy + 1.0f * scale };

                dl->AddCircleFilled(dot_pos, outer_dot_radius,
                    IM_COL32((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, dot_alpha));
            }
        }
        dl->PopClipRect();

        // Borda
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color_border, corner_radius, 0, 2.5f * scale);

        // Icon badge (esquerda)
        const float badge_size = 50.0f * scale;
        const float badge_radius = 11.0f * scale;
        dl->AddRectFilled(ImVec2(x + 11.0f * scale, y + 8.0f * scale),
                          ImVec2(x + 11.0f * scale + badge_size, y + 8.0f * scale + 40.0f * scale),
                          IM_COL32((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, alpha(70)), badge_radius);
        dl->AddRect(ImVec2(x + 11.0f * scale, y + 8.0f * scale),
                    ImVec2(x + 11.0f * scale + badge_size, y + 8.0f * scale + 40.0f * scale),
                    IM_COL32((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, alpha(200)), badge_radius, 0, 1.0f * scale);

        // Icon text
        ImGui::PushFont(CyberFonts::GetFontAwesome(18.0f * scale));
        ImVec2 icon_size = ImGui::CalcTextSize(toast.icon.c_str());
        ImGui::PopFont();
        dl->AddText(ImVec2(
            x + 11.0f * scale + (badge_size - icon_size.x) * 0.5f,
            y + 8.0f * scale + (40.0f * scale - icon_size.y) * 0.5f),
            color_accent, toast.icon.c_str());

        // Título
        dl->AddText(ImVec2(x + 73.0f * scale, y + 8.0f * scale),
            color_text, toast.title.c_str());

        // Mensagem
        dl->AddText(ImVec2(x + 73.0f * scale, y + 25.0f * scale),
            color_text_dim, toast.message.c_str());
    }
}

void SetConfig(const ToastConfig& config) {
    g_config = config;
}

const ToastConfig& GetConfig() {
    return g_config;
}

// Convenience helpers
uint64_t Info(std::string_view title, std::string_view msg, bool pinned) {
    return Push(title, msg, ToastType::Info, ICON_FA_INFO_CIRCLE, pinned, false);
}
uint64_t Success(std::string_view title, std::string_view msg, bool pinned) {
    return Push(title, msg, ToastType::Success, ICON_FA_CHECK_CIRCLE, pinned, false);
}
uint64_t Warning(std::string_view title, std::string_view msg, bool pinned) {
    return Push(title, msg, ToastType::Warning, ICON_FA_EXCLAMATION_TRIANGLE, pinned, false);
}
uint64_t Error(std::string_view title, std::string_view msg, bool pinned) {
    return Push(title, msg, ToastType::Error, ICON_FA_TIMES_CIRCLE, pinned, false);
}
uint64_t Centered(std::string_view title, std::string_view msg,
                  ToastType type, std::string_view icon) {
    return Push(title, msg, type, icon.empty() ? GetTypeIcon(type) : std::string(icon),
                false, true);
}

void SetDismissCallback(DismissCallback cb) {
    g_dismiss_callback = std::move(cb);
}

} // namespace OmniGhost::UI::Toast