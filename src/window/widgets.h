#pragma once
#include "../ImGui/imgui.h"
#include "menu_tab.h"
#include <cstddef>
#include <vector>
#include <algorithm>
#include <cfloat>
#include <functional>
#include <string>
#include <chrono>

namespace CyberWidgets {

    enum class TextTone { Primary, Secondary, Accent, Success, Warning, Error, Info };
    enum class ButtonStyle { Primary, Secondary, Ghost, Destructive };
    enum class ModalResult { None, Confirmed, Cancelled };
    enum class HealthStatus { Ok, Warning, Error };

    bool ToggleSwitch(const char* label, bool* v);
    bool Button(const char* label, ButtonStyle style = ButtonStyle::Secondary,
                const ImVec2& size = ImVec2(0, 0), bool enabled = true);
    bool LoadingButton(const char* label, const char* pending_label, bool pending,
                       ButtonStyle style = ButtonStyle::Primary,
                       const ImVec2& size = ImVec2(0, 0), bool enabled = true);
    bool CyberButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool GoldButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool GhostButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool SidebarButton(const char* label, bool active,
                       void(*icon_fn)(ImDrawList*, ImVec2, float, ImU32) = nullptr,
                       float height = 0.0f);
    void SectionTitle(const char* title);
    void Separator();
    bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f");
    bool Combo(const char* label, int* current_item, const char* const items[], int items_count);
    bool InputField(const char* id, char* buffer, std::size_t buffer_size,
                    const char* hint = nullptr, ImGuiInputTextFlags flags = 0,
                    float width = -1.0f, bool default_focus = false);
    bool PasswordField(const char* id, char* buffer, std::size_t buffer_size,
                       bool* reveal, const char* hint = nullptr,
                       ImGuiInputTextFlags flags = 0, float width = -1.0f,
                       bool default_focus = false);
    bool TextInput(const char* label, char* buffer, std::size_t buffer_size,
                   const char* hint = nullptr);
    bool InputInt(const char* label, int* value);
    void TextLine(const char* text, TextTone tone = TextTone::Primary);
    void TextLineF(TextTone tone, const char* format, ...);
    void KeyValueRow(const char* label, const char* value);
    void HealthRow(const char* label, const char* value, HealthStatus status);
    void StatusBadge(const char* label, bool online);
    void Badge(const char* label, TextTone tone = TextTone::Secondary);
    void HelpMarker(const char* description);
    void InlineMessage(const char* message, TextTone tone = TextTone::Info,
                       const char* error_id = nullptr);
    bool EmptyState(const char* title, const char* description,
                    const char* action_label = nullptr);
    void SkeletonLine(float width = -1.0f, float height = 14.0f);
    void BeginSurfaceList(const char* id, float height);
    void EndSurfaceList();

    void BeginCard(const char* title, float width = 0.f);
    void EndCard();
    void CardGap(float y = 10.f);
    void BeginCardRow(int columns = 2);
    void NextCardColumn();
    void EndCardRow();
    float CardRowHalfWidth();
    float CardContentWidth();

    void DrawHeader(const ImVec2& wp, const ImVec2& ws, float fps, bool dma_ok,
                    const char* build, int ping_ms = -1, int players = -1);
    void DrawFooter(const ImVec2& wp, const ImVec2& ws, float fps, bool dma_ok,
                    const char* build, int ping_ms = -1, int players = -1);
    void DrawSidebar(const ImVec2& wp, const ImVec2& ws, MenuTab* current_tab);

    bool SearchBar(const char* hint = "Search features...");
    bool PassSearch(const char* label);

    void Spinner(const char* id, float radius = 7.0f, float thickness = 2.0f,
                 TextTone tone = TextTone::Accent);
    void DrawSpinner(ImDrawList* draw, ImVec2 center, float radius = 7.0f,
                     float thickness = 2.0f, TextTone tone = TextTone::Accent);

    enum class ToastType { Info, Success, Error, Warning };
    using ToastActionCallback = void(*)();
    void Notify(const char* msg, ToastType type = ToastType::Success);
    void NotifyAction(const char* msg, ToastType type, const char* action_label,
                      ToastActionCallback callback);
    void DrawToasts();
    void CopyToClipboard(const char* text, const char* feedback = "Copiado");

    void OpenModal(const char* id);
    bool BeginModal(const char* id, const char* title, float width = 440.0f);
    void EndModal();
    ModalResult ConfirmModal(const char* id, const char* title, const char* message,
                             const char* confirm_label, const char* cancel_label = "Cancelar",
                             ButtonStyle confirm_style = ButtonStyle::Destructive,
                             float width = 440.0f);

    enum class ThemeId { Dark = 0, Cyber, Purple, Gold, Matrix, Red, Blue, White };
    void SetTheme(ThemeId id);
    bool ThemeCombo(const char* label);

    void ColorEditU32(const char* label, ImU32* col);

    // Visual state components for consistent UI feedback
    enum class VisualState {
        Normal,
        Loading,
        Unavailable,
        Error,
        Empty,
        Retry
    };

    // Draw a visual state indicator with consistent styling
    bool DrawVisualState(
        VisualState state,
        const char* message = nullptr,
        const char* actionLabel = nullptr,
        bool (*actionCallback)() = nullptr,
        float width = -1.0f);

    // Convenience functions for common states
    bool DrawLoadingState(const char* message = "A carregar...", float width = -1.0f);
    bool DrawUnavailableState(const char* message = "Indisponível", const char* actionLabel = "Tentar novamente", bool (*actionCallback)() = nullptr, float width = -1.0f);
    bool DrawErrorState(const char* message = "Ocorreu um erro", const char* actionLabel = "Tentar novamente", bool (*actionCallback)() = nullptr, float width = -1.0f);
    bool DrawEmptyState(const char* title = "Nenhum item", const char* description = nullptr, const char* actionLabel = nullptr, bool (*actionCallback)() = nullptr, float width = -1.0f);
    bool DrawRetryState(const char* message = "Falha na operação", const char* actionLabel = "Tentar novamente", bool (*actionCallback)() = nullptr, float width = -1.0f);

    // Virtualized list - only renders visible items for performance with large lists
    template<typename T>
    void VirtualizedList(
        const char* id,
        const std::vector<T>& items,
        float itemHeight,
        float containerHeight,
        std::function<void(const T&, int, bool)> renderItem,
        [[maybe_unused]] float rowHeight,
        [[maybe_unused]] float listHeight,
        [[maybe_unused]] std::function<void(const T&, int, bool)> renderItem2,
        [[maybe_unused]] bool allowSelection,
        int* selectedIndex)
    {
        if (items.empty() || containerHeight <= 0.0f || itemHeight <= 0.0f) return;
        
        ImGui::PushID(id);
        ImGui::BeginChild(id, ImVec2(0, containerHeight), true);
        
        const float scrollY = ImGui::GetScrollY();
        
        // Calculate visible range
        int startIdx = static_cast<int>(scrollY / itemHeight);
        int endIdx = static_cast<int>((scrollY + containerHeight) / itemHeight) + 1;
        startIdx = std::max(0, startIdx);
        endIdx = std::min(static_cast<int>(items.size()), endIdx);
        
        // Dummy for items before visible range
        if (startIdx > 0) {
            ImGui::Dummy(ImVec2(0, startIdx * itemHeight));
        }
        
        // Render visible items
        for (int i = startIdx; i < endIdx; ++i) {
            bool isSelected = selectedIndex && *selectedIndex == i;
            renderItem(items[i], i, isSelected);
        }
        
        // Dummy for items after visible range
        const float endY = (items.size() - endIdx) * itemHeight;
        if (endY > 0) {
            ImGui::Dummy(ImVec2(0, endY));
        }
        
        ImGui::EndChild();
        ImGui::PopID();
    }

} // namespace CyberWidgets

// Progress dialog for long operations with cancel support
class ProgressDialog {
public:
    struct Config {
        const char* title = "A processar...";
        const char* message = nullptr;
        bool cancellable = true;
        const char* cancelLabel = "Cancelar";
        float minDuration = 0.5f;  // Minimum time to show dialog
        bool showProgressBar = true;
        bool showTimeElapsed = true;
        float width = 400.0f;
    };

    ProgressDialog() = default;
    ~ProgressDialog() = default;

    ProgressDialog(const ProgressDialog&) = delete;
    ProgressDialog& operator=(const ProgressDialog&) = delete;

    // Start the progress dialog
    void Open(const Config& config = Config()) noexcept;

    // Update progress (0.0 to 1.0)
    void SetProgress(float progress, const char* message = nullptr) noexcept;

    // Get current progress (0.0 to 1.0)
    [[nodiscard]] float GetProgress() const noexcept { return progress_; }

    // Get current message
    [[nodiscard]] const std::string& GetMessage() const noexcept { return message_; }

    // Check if user requested cancel
    [[nodiscard]] bool IsCancelled() const noexcept { return cancelled_; }

    // Request cancel
    void Cancel() noexcept { cancelled_ = true; }

    // Close the dialog
    void Close() noexcept;

    // Check if dialog is open
    [[nodiscard]] bool IsOpen() const noexcept { return open_; }

    // Get elapsed time
    [[nodiscard]] float GetElapsedSeconds() const noexcept;

    // Get config (for internal drawing)
    [[nodiscard]] const Config& GetConfig() const noexcept { return config_; }

private:
    Config config_;
    bool open_ = false;
    bool cancelled_ = false;
    float progress_ = 0.0f;
    std::string message_;
    std::chrono::steady_clock::time_point startTime_;
    bool minDurationMet_ = false;
};
