#include "updates_page.h"

#include "changelog_service.h"
#include "launcher_assets.h"
#include "launcher_data.h"
#include "update_time_utils.h"
#include "window/fonts.h"
#include "../window/theme.h"
#include "../window/ui_format.h"
#include "../window/widgets.h"
#include "../window/localization.h"
#include "../updater/update_service.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "shell32.lib")

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace LauncherUpdates {
    namespace {

        ImU32 C_PANEL(int alpha = 250) { return IM_COL32(0, 0, 0, alpha); }
        ImU32 C_CARD(int alpha = 255) { return IM_COL32(0, 0, 0, alpha); }
        constexpr ImU32 C_GOLD = IM_COL32(212, 175, 55, 255);
        constexpr ImU32 C_GOLD_LT = IM_COL32(242, 214, 109, 255);
        constexpr ImU32 C_TEXT = IM_COL32(240, 240, 243, 255);
        constexpr ImU32 C_MUTED = IM_COL32(145, 145, 154, 255);
        constexpr ImU32 C_MUTED2 = IM_COL32(103, 103, 112, 255);

        char g_search[160]{};
        int g_module_filter = 0;
        int g_category_filter = -1;
        int g_sort = 0;
        bool g_opened = false;

        std::unordered_set<std::string> g_session_new;
        std::unordered_set<std::string> g_known_ids;
        std::unordered_set<std::string> g_expanded;

        struct CardView {
            const Launcher::ChangelogRelease* release{};
            const Launcher::ChangelogModule* module{};
            std::string id;
        };

        struct ModuleFilter {
            std::string id;
            std::string name;
        };

        ImVec4 ToVec4(ImU32 color) {
            return ImGui::ColorConvertU32ToFloat4(color);
        }

        std::string LowerAscii(const std::string& value) {
            std::string result = value;

            for (char& character : result) {
                const unsigned char byte =
                    static_cast<unsigned char>(character);

                if (byte < 128) {
                    character = static_cast<char>(
                        std::tolower(byte));
                }
            }

            return result;
        }

        const char* ChangeTypeIcon(
            Launcher::ChangeType type) {
            switch (type) {
            case Launcher::ChangeType::Added:
                return "✨";

            case Launcher::ChangeType::Improved:
                return "🚀";

            case Launcher::ChangeType::Fixed:
                return "🔧";

            case Launcher::ChangeType::Performance:
                return "⚡";

            case Launcher::ChangeType::Compatibility:
                return "🔄";

            case Launcher::ChangeType::Security:
                return "🔒";

            case Launcher::ChangeType::Removed:
                return "🗑";

            case Launcher::ChangeType::Breaking:
                return "⚠";

            case Launcher::ChangeType::Maintenance:
                return "🔧";

            default:
                return "•";
            }
        }

        const char* ReleaseChangeTypeLabel(
            Launcher::ChangeType type) {
            switch (type) {
            case Launcher::ChangeType::Added:
                return "Adicionado";

            case Launcher::ChangeType::Improved:
                return "Melhorado";

            case Launcher::ChangeType::Fixed:
                return "Corrigido";

            case Launcher::ChangeType::Performance:
                return "Desempenho";

            case Launcher::ChangeType::Compatibility:
                return "Compatibilidade";

            case Launcher::ChangeType::Security:
                return "Segurança";

            case Launcher::ChangeType::Removed:
                return "Removido";

            case Launcher::ChangeType::Breaking:
                return "Alteração importante";

            case Launcher::ChangeType::Maintenance:
                return "Manutenção";

            default:
                return "Alteração";
            }
        }

        std::string PublicReleaseUrl(
            const Launcher::ChangelogRelease& release) {
            if (!release.releaseUrl.empty()) {
                return release.releaseUrl;
            }

            return
                "https://github.com/"
                "rodrigomatossilva07-rgb/"
                "OmniGhost-Updates/releases/tag/" +
                release.tag;
        }

        std::size_t CategoryGroupCount(
            const Launcher::ChangelogRelease& release) {
            const Launcher::ChangeType order[] = {
                Launcher::ChangeType::Breaking,
                Launcher::ChangeType::Added,
                Launcher::ChangeType::Improved,
                Launcher::ChangeType::Performance,
                Launcher::ChangeType::Compatibility,
                Launcher::ChangeType::Fixed,
                Launcher::ChangeType::Security,
                Launcher::ChangeType::Removed,
                Launcher::ChangeType::Maintenance
            };

            std::size_t count = 0;

            for (const Launcher::ChangelogModule& module :
                release.modules) {
                for (Launcher::ChangeType type : order) {
                    const bool exists =
                        std::any_of(
                            module.changes.begin(),
                            module.changes.end(),
                            [type](
                                const Launcher::ChangelogChange& change) {
                                    return change.type == type;
                            });

                    if (exists) {
                        ++count;
                    }
                }
            }

            return count;
        }

        std::size_t ChangeCount(
            const Launcher::ChangelogRelease& release) {
            std::size_t count = 0;

            for (const Launcher::ChangelogModule& module :
                release.modules) {
                count += module.changes.size();
            }

            return count;
        }

        std::string MonthGroup(
            const std::string& iso) {
            if (iso.size() < 7) {
                return "MAIS ANTIGAS";
            }

            static const char* months[] = {
                "",
                "JANEIRO",
                "FEVEREIRO",
                "MARÇO",
                "ABRIL",
                "MAIO",
                "JUNHO",
                "JULHO",
                "AGOSTO",
                "SETEMBRO",
                "OUTUBRO",
                "NOVEMBRO",
                "DEZEMBRO"
            };

            int year = 0;
            int month = 0;
            int day = 0;

            if (!UpdateTime::ParseIsoDatePrefix(
                iso,
                year,
                month,
                day)) {
                return "MAIS ANTIGAS";
            }

            return
                std::string(months[month]) +
                " " +
                std::to_string(year);
        }

        bool ContainsSearch(
            const CardView& card) {
            if (g_search[0] == '\0') {
                return true;
            }

            if (card.release == nullptr ||
                card.module == nullptr) {
                return false;
            }

            const std::string query =
                LowerAscii(g_search);

            const std::string fields[] = {
                card.module->id,
                card.module->name,
                card.module->version,
                card.release->version,
                card.release->title,
                card.release->summary
            };

            for (const std::string& field : fields) {
                if (LowerAscii(field).find(query) !=
                    std::string::npos) {
                    return true;
                }
            }

            for (const Launcher::ChangelogChange& change :
                card.module->changes) {
                if (LowerAscii(change.text).find(query) !=
                    std::string::npos) {
                    return true;
                }
            }

            return false;
        }

        bool HasCategory(
            const CardView& card) {
            if (g_category_filter < 0) {
                return true;
            }

            if (card.module == nullptr) {
                return false;
            }

            const auto selected =
                static_cast<Launcher::ChangeType>(
                    g_category_filter);

            return std::any_of(
                card.module->changes.begin(),
                card.module->changes.end(),
                [selected](
                    const Launcher::ChangelogChange& change) {
                        return change.type == selected;
                });
        }

        ImU32 CategoryColor(
            Launcher::ChangeType type) {
            switch (type) {
            case Launcher::ChangeType::Added:
                return IM_COL32(
                    110,
                    192,
                    126,
                    255);

            case Launcher::ChangeType::Improved:
                return IM_COL32(
                    102,
                    151,
                    211,
                    255);

            case Launcher::ChangeType::Fixed:
                return IM_COL32(
                    213,
                    132,
                    72,
                    255);

            case Launcher::ChangeType::Performance:
                return IM_COL32(
                    214,
                    176,
                    72,
                    255);

            case Launcher::ChangeType::Compatibility:
                return IM_COL32(
                    118,
                    126,
                    205,
                    255);

            case Launcher::ChangeType::Security:
                return IM_COL32(
                    153,
                    112,
                    190,
                    255);

            case Launcher::ChangeType::Removed:
                return IM_COL32(
                    187,
                    82,
                    82,
                    255);

            case Launcher::ChangeType::Breaking:
                return IM_COL32(
                    242,
                    194,
                    74,
                    255);

            default:
                return C_GOLD;
            }
        }

        void DrawFallbackLogo(
            ImDrawList* draw,
            const Launcher::GameDefinition* game,
            ImVec2 minimum,
            ImVec2 maximum) {
            if (draw == nullptr) {
                return;
            }

            const ImU32 accent =
                game != nullptr
                ? IM_COL32(
                    game->accent.r,
                    game->accent.g,
                    game->accent.b,
                    255)
                : C_GOLD;

            draw->AddRectFilled(
                minimum,
                maximum,
                IM_COL32(
                    10,
                    10,
                    13,
                    255),
                7.0f);

            draw->AddRect(
                minimum,
                maximum,
                (accent & 0x00FFFFFFu) |
                (110u << 24),
                7.0f);

            const char* mark =
                game != nullptr &&
                game->fallback_mark != nullptr
                ? game->fallback_mark
                : "O";

            const ImVec2 size =
                ImGui::CalcTextSize(mark);

            draw->AddText(
                ImVec2(
                    (minimum.x +
                        maximum.x -
                        size.x) *
                    0.5f,
                    (minimum.y +
                        maximum.y -
                        size.y) *
                    0.5f),
                accent,
                mark);
        }

        void DrawLogo(
            ImDrawList* draw,
            const Launcher::GameDefinition* game,
            ImVec2 minimum,
            ImVec2 maximum) {
            if (draw == nullptr) {
                return;
            }

            if (game == nullptr) {
                DrawFallbackLogo(
                    draw,
                    nullptr,
                    minimum,
                    maximum);

                return;
            }

            const LauncherAssets::Texture texture =
                LauncherAssets::Logo(
                    game->launch_id);

            if (!texture ||
                texture.width <= 0 ||
                texture.height <= 0) {
                DrawFallbackLogo(
                    draw,
                    game,
                    minimum,
                    maximum);

                return;
            }

            const float boxWidth =
                maximum.x - minimum.x;

            const float boxHeight =
                maximum.y - minimum.y;

            const float ratio =
                static_cast<float>(texture.width) /
                static_cast<float>(texture.height);

            float width = boxWidth;
            float height = width / ratio;

            if (height > boxHeight) {
                height = boxHeight;
                width = height * ratio;
            }

            const ImVec2 imageMinimum(
                (minimum.x +
                    maximum.x -
                    width) *
                0.5f,
                (minimum.y +
                    maximum.y -
                    height) *
                0.5f);

            draw->AddImage(
                texture.id,
                imageMinimum,
                ImVec2(
                    imageMinimum.x + width,
                    imageMinimum.y + height));
        }

        void PushControlStyle() {
            ImGui::PushStyleColor(
                ImGuiCol_FrameBg,
                ToVec4(
                    IM_COL32(
                        22,
                        22,
                        27,
                        255)));

            ImGui::PushStyleColor(
                ImGuiCol_FrameBgHovered,
                ToVec4(
                    IM_COL32(
                        29,
                        29,
                        35,
                        255)));

            ImGui::PushStyleColor(
                ImGuiCol_FrameBgActive,
                ToVec4(
                    IM_COL32(
                        31,
                        31,
                        38,
                        255)));

            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ToVec4(
                    IM_COL32(
                        22,
                        22,
                        27,
                        255)));

            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ToVec4(
                    IM_COL32(
                        38,
                        34,
                        24,
                        255)));

            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                ToVec4(
                    IM_COL32(
                        48,
                        41,
                        24,
                        255)));

            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ToVec4(
                    IM_COL32(
                        212,
                        175,
                        55,
                        65)));

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ToVec4(C_TEXT));

            ImGui::PushStyleColor(
                ImGuiCol_PopupBg,
                ToVec4(
                    IM_COL32(
                        17,
                        17,
                        21,
                        255)));

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameRounding,
                5.0f);

            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameBorderSize,
                1.0f);
        }

        void PopControlStyle() {
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(9);
        }

        bool FiltersActive() {
            return
                g_search[0] != '\0' ||
                g_module_filter != 0 ||
                g_category_filter >= 0 ||
                g_sort != 0;
        }

        void DrawCategoryBadge(
            Launcher::ChangeType type) {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ToVec4(
                    CategoryColor(type)));

            ImGui::TextUnformatted(
                Launcher::ChangeTypeLabel(type));

            ImGui::PopStyleColor();
        }

        void DrawEmptyState(
            const char* title,
            const char* description) {
            CyberWidgets::EmptyState(title, description);
        }

        void DrawLatestReleasePanel(
            const Launcher::ChangelogRelease& release,
            const std::string& channel) {

            const std::size_t categoryCount =
                CategoryGroupCount(release);

            const std::size_t changeCount =
                ChangeCount(release);

            // Compact featured card — never dominate the page.
            // Full change list lives in expandable history cards below.
            const float estimatedHeight =
                210.0f +
                static_cast<float>((std::min)(changeCount, static_cast<std::size_t>(3))) *
                22.0f +
                static_cast<float>((std::min)(categoryCount, static_cast<std::size_t>(4))) *
                18.0f;

            const float panelHeight =
                (std::min)(
                    (std::max)(estimatedHeight, 180.0f),
                    260.0f);

            ImGui::PushStyleColor(
                ImGuiCol_ChildBg,
                ToVec4(
                    IM_COL32(
                        18,
                        18,
                        22,
                        255)));

            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ToVec4(
                    IM_COL32(
                        212,
                        175,
                        55,
                        95)));

            ImGui::PushStyleVar(
                ImGuiStyleVar_ChildRounding,
                9.0f);

            ImGui::PushStyleVar(
                ImGuiStyleVar_ChildBorderSize,
                1.0f);

            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(
                    20.0f,
                    18.0f));

            ImGui::BeginChild(
                "##github_release_view",
                ImVec2(
                    0.0f,
                    panelHeight),
                true,
                estimatedHeight > panelHeight
                ? ImGuiWindowFlags_AlwaysVerticalScrollbar
                : ImGuiWindowFlags_None);

            ImFont* titleFont =
                CyberFonts::GetTitleFont();

            if (titleFont != nullptr) {
                ImGui::PushFont(titleFont);
            }

            const std::string displayTitle =
                release.title.empty()
                ? "OmniGhost " + UiFormat::Version(release.version)
                : release.title;

            ImGui::TextColored(
                ToVec4(C_TEXT),
                "%s",
                displayTitle.c_str());

            if (titleFont != nullptr) {
                ImGui::PopFont();
            }

            ImGui::SameLine();

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ToVec4(C_GOLD));

            ImGui::TextUnformatted(Loc::Tr("launcher.latest"));

            ImGui::PopStyleColor();

            const std::string author =
                release.author.empty()
                ? "rodrigomatossilva07-rgb"
                : release.author;

            const std::string authorLine =
                "@" +
                author +
                " publicou esta versão " +
                UpdateTime::RelativePublishedTime(
                    release.publishedAt);

            ImGui::TextColored(
                ToVec4(C_MUTED),
                "%s",
                authorLine.c_str());

            ImGui::Spacing();

            const std::string displayTag =
                release.tag.empty()
                ? UiFormat::Version(release.version)
                : release.tag;

            ImGui::TextColored(
                ToVec4(C_GOLD_LT),
                "%s",
                displayTag.c_str());

            if (!release.commit.empty()) {
                ImGui::SameLine();

                ImGui::TextColored(
                    ToVec4(C_MUTED2),
                    "· %s",
                    release.commit.c_str());
            }

            ImGui::Separator();
            ImGui::Spacing();

            if (!release.summary.empty()) {
                ImGui::PushTextWrapPos(
                    ImGui::GetCursorPosX() +
                    ImGui::GetContentRegionAvail().x);

                ImGui::TextColored(
                    ToVec4(C_TEXT),
                    "%s",
                    release.summary.c_str());

                ImGui::PopTextWrapPos();
                ImGui::Spacing();
            }

            if (ImGui::BeginTable(
                "##release_metadata",
                3,
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableSetupColumn(
                    "Versão");

                ImGui::TableSetupColumn(
                    "Canal");

                ImGui::TableSetupColumn(
                    "Tag");

                ImGui::TableHeadersRow();
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);

                const std::string formattedReleaseVersion = UiFormat::Version(release.version);
                ImGui::TextColored(
                    ToVec4(C_GOLD_LT),
                    "%s",
                    formattedReleaseVersion.c_str());

                ImGui::TableSetColumnIndex(1);

                const char* displayChannel =
                    channel.empty() || channel == "stable" ? "Estável" :
                    channel == "beta" ? "Beta" : channel.c_str();
                ImGui::TextColored(
                    ToVec4(C_GOLD_LT),
                    "%s",
                    displayChannel);

                ImGui::TableSetColumnIndex(2);

                ImGui::TextColored(
                    ToVec4(C_GOLD_LT),
                    "%s",
                    displayTag.c_str());

                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const Launcher::ChangeType order[] = {
                Launcher::ChangeType::Breaking,
                Launcher::ChangeType::Added,
                Launcher::ChangeType::Improved,
                Launcher::ChangeType::Performance,
                Launcher::ChangeType::Compatibility,
                Launcher::ChangeType::Fixed,
                Launcher::ChangeType::Security,
                Launcher::ChangeType::Removed,
                Launcher::ChangeType::Maintenance
            };

            for (const Launcher::ChangelogModule& module :
                release.modules) {
                const std::string moduleName =
                    module.name.empty()
                    ? module.id
                    : module.name;

                ImGui::TextColored(
                    ToVec4(C_TEXT),
                    "%s",
                    moduleName.c_str());

                if (!module.version.empty()) {
                    ImGui::SameLine();

                    const std::string moduleVersion = UiFormat::Version(module.version);
                    ImGui::TextColored(
                        ToVec4(C_MUTED2),
                        "%s",
                        moduleVersion.c_str());
                }

                ImGui::Spacing();

                for (Launcher::ChangeType type :
                order) {
                    const bool hasType =
                        std::any_of(
                            module.changes.begin(),
                            module.changes.end(),
                            [type](
                                const Launcher::ChangelogChange& change) {
                                    return change.type == type;
                            });

                    if (!hasType) {
                        continue;
                    }

                    ImGui::TextColored(
                        ToVec4(
                            CategoryColor(type)),
                        "%s %s",
                        ChangeTypeIcon(type),
                        ReleaseChangeTypeLabel(type));

                    for (const Launcher::ChangelogChange& change :
                        module.changes) {
                        if (change.type != type) {
                            continue;
                        }

                        ImGui::Bullet();
                        ImGui::SameLine();

                        ImGui::PushTextWrapPos(
                            ImGui::GetCursorPosX() +
                            ImGui::GetContentRegionAvail().x -
                            8.0f);

                        ImGui::TextColored(
                            ToVec4(C_MUTED),
                            "%s",
                            change.text.c_str());

                        ImGui::PopTextWrapPos();
                    }

                    ImGui::Spacing();
                }

                ImGui::Separator();
                ImGui::Spacing();
            }

            ImGui::TextColored(
                ToVec4(C_MUTED2),
                "%s",
                "O conteúdo acima é gerado a partir "
                "dos mesmos dados usados na versão publicada no GitHub.");

            if (CyberWidgets::Button(
                "Abrir versão no GitHub", CyberWidgets::ButtonStyle::Secondary)) {
                const std::string url =
                    PublicReleaseUrl(release);

                if (!url.empty()) {
                    ShellExecuteA(
                        nullptr,
                        "open",
                        url.c_str(),
                        nullptr,
                        nullptr,
                        SW_SHOWNORMAL);
                }
            }

            ImGui::EndChild();

            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);
        }

        void MarkRead(
            const CardView& card) {
            Launcher::MarkUpdateRead(
                card.id.c_str());

            g_session_new.erase(
                card.id);
        }

        void DrawUpdateCard(
            const CardView& card,
            float width) {
            if (card.release == nullptr ||
                card.module == nullptr ||
                width <= 0.0f) {
                return;
            }

            const Launcher::GameDefinition* game =
                Launcher::FindGame(
                    card.module->id.c_str());

            const bool isNew =
                g_session_new.contains(
                    card.id);

            const bool expanded =
                g_expanded.contains(
                    card.id);

            const std::size_t visibleChanges =
                expanded
                ? card.module->changes.size()
                : (std::min<std::size_t>)(
                    card.module->changes.size(),
                    3);

            const float cardHeight =
                142.0f +
                static_cast<float>(
                    visibleChanges) *
                38.0f +
                (
                    !expanded &&
                    card.module->changes.size() >
                    visibleChanges
                    ? 32.0f
                    : 0.0f
                    );

            const ImVec2 cardMinimum =
                ImGui::GetCursorScreenPos();

            const ImVec2 cardMaximum(
                cardMinimum.x + width,
                cardMinimum.y + cardHeight);

            ImDrawList* draw =
                ImGui::GetWindowDrawList();

            draw->AddRectFilled(
                cardMinimum,
                cardMaximum,
                C_CARD(),
                8.0f);

            draw->AddRect(
                cardMinimum,
                cardMaximum,
                IM_COL32(
                    212,
                    175,
                    55,
                    isNew
                    ? 115
                    : 45),
                8.0f);

            if (isNew) {
                draw->AddRectFilled(
                    cardMinimum,
                    ImVec2(
                        cardMinimum.x + 3.0f,
                        cardMaximum.y),
                    C_GOLD,
                    8.0f);
            }

            ImGui::PushID(
                card.id.c_str());

            ImGui::SetCursorScreenPos(
                ImVec2(
                    cardMinimum.x + 16.0f,
                    cardMinimum.y + 12.0f));

            ImGui::InvisibleButton(
                "##expand_header",
                ImVec2(
                    (std::max)(
                        width - 32.0f,
                        1.0f),
                    68.0f));

            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(
                    ImGuiMouseCursor_Hand);
            }

            if (ImGui::IsItemClicked()) {
                if (expanded) {
                    g_expanded.erase(
                        card.id);
                }
                else {
                    g_expanded.insert(
                        card.id);
                }

                MarkRead(card);
            }

            DrawLogo(
                draw,
                game,
                ImVec2(
                    cardMinimum.x + 18.0f,
                    cardMinimum.y + 14.0f),
                ImVec2(
                    cardMinimum.x + 62.0f,
                    cardMinimum.y + 58.0f));

            const float textX =
                cardMinimum.x + 74.0f;

            const std::string moduleName =
                card.module->name.empty()
                ? card.module->id
                : card.module->name;

            draw->AddText(
                ImVec2(
                    textX,
                    cardMinimum.y + 12.0f),
                C_TEXT,
                moduleName.c_str());

            const std::string displayVersion = UiFormat::Version(
                card.module->version.empty()
                ? card.release->version
                : card.module->version);

            const std::string metadata =
                displayVersion +
                "  ·  " +
                UpdateTime::DateDisplay(
                    card.release->publishedAt);

            draw->AddText(
                ImVec2(
                    textX,
                    cardMinimum.y + 34.0f),
                C_MUTED,
                metadata.c_str());

            draw->AddText(
                ImVec2(
                    textX,
                    cardMinimum.y + 57.0f),
                C_GOLD_LT,
                card.release->title.c_str());

            draw->AddText(
                ImVec2(
                    cardMaximum.x - 27.0f,
                    cardMinimum.y + 31.0f),
                C_MUTED,
                expanded
                ? "⌃"
                : "⌄");

            if (isNew) {
                draw->AddRectFilled(
                    ImVec2(
                        cardMaximum.x - 84.0f,
                        cardMinimum.y + 12.0f),
                    ImVec2(
                        cardMaximum.x - 36.0f,
                        cardMinimum.y + 32.0f),
                    IM_COL32(
                        65,
                        53,
                        19,
                        255),
                    4.0f);

                draw->AddText(
                    ImVec2(
                        cardMaximum.x - 73.0f,
                        cardMinimum.y + 15.0f),
                    C_GOLD,
                    "Novo");
            }

            ImGui::SetCursorScreenPos(
                ImVec2(
                    cardMinimum.x + 18.0f,
                    cardMinimum.y + 88.0f));

            ImGui::PushTextWrapPos(
                cardMaximum.x - 18.0f);

            ImGui::TextColored(
                ToVec4(C_MUTED),
                "%s",
                card.release->summary.c_str());

            ImGui::PopTextWrapPos();

            float changeY =
                cardMinimum.y + 119.0f;

            for (std::size_t index = 0;
                index < visibleChanges;
                ++index) {
                const Launcher::ChangelogChange& change =
                    card.module->changes[index];

                ImGui::SetCursorScreenPos(
                    ImVec2(
                        cardMinimum.x + 18.0f,
                        changeY));

                DrawCategoryBadge(
                    change.type);

                ImGui::SameLine(
                    0.0f,
                    14.0f);

                ImGui::PushTextWrapPos(
                    cardMaximum.x - 20.0f);

                ImGui::TextColored(
                    ToVec4(C_MUTED),
                    "%s",
                    change.text.c_str());

                ImGui::PopTextWrapPos();

                changeY += 38.0f;
            }

            if (!expanded &&
                card.module->changes.size() >
                visibleChanges) {
                ImGui::SetCursorScreenPos(
                    ImVec2(
                        cardMinimum.x + 18.0f,
                        cardMaximum.y - 29.0f));

                if (CyberWidgets::Button(
                    "Ver alterações", CyberWidgets::ButtonStyle::Ghost, ImVec2(128.0f, 26.0f))) {
                    g_expanded.insert(
                        card.id);

                    MarkRead(card);
                }
            }
            else if (
                expanded &&
                card.module->changes.size() > 3) {
                ImGui::SetCursorScreenPos(
                    ImVec2(
                        cardMinimum.x + 18.0f,
                        cardMaximum.y - 25.0f));

                ImGui::TextColored(
                    ToVec4(C_GOLD),
                    "%s",
                    "Ocultar alterações");
            }

            ImGui::SetCursorScreenPos(
                ImVec2(
                    cardMinimum.x,
                    cardMaximum.y + 14.0f));

            ImGui::Dummy(
                ImVec2(
                    width,
                    1.0f));

            ImGui::PopID();
        }

        std::vector<CardView> BuildCards(
            const Launcher::ChangelogSnapshot& snapshot) {
            std::vector<CardView> cards;

            for (const Launcher::ChangelogRelease& release :
                snapshot.releases) {
                for (const Launcher::ChangelogModule& module :
                    release.modules) {
                    if (module.changes.empty()) {
                        continue;
                    }

                    cards.push_back(
                        CardView{
                            &release,
                            &module,
                            Launcher::ChangelogService::CardId(
                                release,
                                module)
                        });
                }
            }

            return cards;
        }

        std::vector<ModuleFilter> BuildModuleFilters(
            const std::vector<CardView>& cards) {
            std::vector<ModuleFilter> filters;

            for (const CardView& card : cards) {
                if (card.module == nullptr) {
                    continue;
                }

                const auto found =
                    std::find_if(
                        filters.begin(),
                        filters.end(),
                        [&card](
                            const ModuleFilter& item) {
                                return
                                    item.id ==
                                    card.module->id;
                        });

                if (found == filters.end()) {
                    filters.push_back(
                        ModuleFilter{
                            card.module->id,
                            card.module->name.empty()
                                ? card.module->id
                                : card.module->name
                        });
                }
            }

            std::sort(
                filters.begin(),
                filters.end(),
                [](
                    const ModuleFilter& left,
                    const ModuleFilter& right) {
                        return left.name < right.name;
                });

            return filters;
        }

        void SynchronizeUnread(
            const std::vector<CardView>& cards) {
            for (const CardView& card : cards) {
                if (card.id.empty()) {
                    continue;
                }

                if (g_known_ids.insert(
                    card.id).second &&
                    !Launcher::IsUpdateRead(
                        card.id.c_str())) {
                    g_session_new.insert(
                        card.id);
                }
            }
        }

    } // namespace

    void Reset() {
        g_search[0] = '\0';
        g_module_filter = 0;
        g_category_filter = -1;
        g_sort = 0;
        g_opened = false;

        g_session_new.clear();
        g_known_ids.clear();
        g_expanded.clear();
    }

    void OnOpened() {
        if (g_opened) {
            return;
        }

        g_opened = true;

        auto& service =
            Launcher::ChangelogService::Instance();

        service.Initialize();
        service.RefreshAsync(false);
    }

    void Draw(
        const ImVec2& display,
        float contentTop,
        float contentBottom) {
        OnOpened();

        auto& changelogService =
            Launcher::ChangelogService::Instance();

        const Launcher::ChangelogSnapshot snapshot =
            changelogService.GetSnapshot();

        std::vector<CardView> allCards =
            BuildCards(snapshot);

        SynchronizeUnread(allCards);

        const std::vector<ModuleFilter> moduleFilters =
            BuildModuleFilters(allCards);

        if (g_module_filter < 0 ||
            g_module_filter >
            static_cast<int>(
                moduleFilters.size())) {
            g_module_filter = 0;
        }

        const float horizontal =
            display.x < 720.0f
            ? 16.0f
            : 32.0f;

        const float calculatedWidth =
            display.x -
            horizontal * 2.0f;

        const float pageWidth =
            (std::max)(
                1.0f,
                (std::min)(
                    calculatedWidth,
                    1360.0f));

        const float pageX =
            (display.x - pageWidth) *
            0.5f;

        const float pageHeight =
            (std::max)(
                1.0f,
                display.y -
                contentTop -
                contentBottom);

        ImGui::SetNextWindowPos(
            ImVec2(
                pageX,
                contentTop));

        ImGui::SetNextWindowSize(
            ImVec2(
                pageWidth,
                pageHeight));

        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            ImVec4(
                0.0f,
                0.0f,
                0.0f,
                0.0f));

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(
                0.0f,
                0.0f,
                0.0f,
                0.0f));

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(
                0.0f,
                0.0f));

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowBorderSize,
            0.0f);

        ImGui::Begin(
            "##updates_page",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground);

        ImFont* title =
            CyberFonts::GetTitleFont();

        ImFont* body =
            CyberFonts::GetBodyFont();

        if (body != nullptr) {
            ImGui::PushFont(body);
        }

        if (title != nullptr) {
            ImGui::PushFont(title);
        }

        ImGui::TextColored(
            ToVec4(C_GOLD),
            "%s",
            "OMNI // RELEASE NOTES");
        ImGui::Dummy(ImVec2(0.0f, CyberTheme::Spacing::Xs));
        ImGui::TextColored(
            ToVec4(C_TEXT),
            "%s",
            "Atualizações");

        if (title != nullptr) {
            ImGui::PopFont();
        }

        ImGui::TextColored(
            ToVec4(C_MUTED),
            "%s",
            "Histórico publicado, organizado por produto e componente. "
            "Apenas alterações verificadas são apresentadas aqui.");

        const std::string lastDate =
            snapshot.releases.empty()
            ? "—"
            : UpdateTime::DateDisplay(
                snapshot.releases.front().publishedAt);

        ImGui::Spacing();

        ImGui::TextColored(
            ToVec4(C_GOLD),
            "%zu",
            snapshot.releases.size());

        ImGui::SameLine();

        ImGui::TextColored(
            ToVec4(C_MUTED),
            "%s",
            "atualizações");

        ImGui::SameLine(170.0f);

        ImGui::TextColored(
            ToVec4(C_MUTED2),
            "%s",
            "Última atualização:");

        ImGui::SameLine();

        ImGui::TextColored(
            ToVec4(C_TEXT),
            "%s",
            lastDate.c_str());

        if (display.x >= 760.0f) {
            const auto updateState =
                OmniGhost::Update::UpdateService::
                Instance().
                GetSnapshot();

            const bool checkingExecutable =
                updateState.status ==
                OmniGhost::Update::Status::Checking ||
                updateState.status ==
                OmniGhost::Update::Status::Downloading;

            const bool checkingChangelog =
                changelogService.IsBusy();

            ImGui::SameLine(
                (std::max)(
                    pageWidth - 190.0f,
                    0.0f));

            if (CyberWidgets::LoadingButton(
                    "Procurar atualizações", "A procurar…",
                    checkingExecutable || checkingChangelog,
                    CyberWidgets::ButtonStyle::Secondary,
                    ImVec2(CyberTheme::Px(180.0f), CyberTheme::Px(34.0f)))) {
                OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
                changelogService.RefreshAsync(true);
            }
        }

        ImGui::Spacing();

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ToVec4(C_PANEL()));

        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ToVec4(
                IM_COL32(
                    212,
                    175,
                    55,
                    42)));

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            7.0f);

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildBorderSize,
            1.0f);

        const bool narrow =
            display.x < 760.0f;

        const float filterHeight =
            narrow
            ? 164.0f
            : 54.0f;

        ImGui::BeginChild(
            "##update_filters",
            ImVec2(
                0.0f,
                filterHeight),
            true);

        PushControlStyle();

        const float fieldWidth =
            narrow
            ? ImGui::GetContentRegionAvail().x
            : 230.0f;

        CyberWidgets::InputField(
            "##update_search",
            g_search,
            sizeof(g_search),
            "Pesquisar atualizações...",
            0,
            fieldWidth);

        if (narrow) {
            ImGui::Spacing();
        }
        else {
            ImGui::SameLine();
        }

        const char* modulePreview =
            "Todos os componentes";

        if (g_module_filter > 0 &&
            static_cast<std::size_t>(
                g_module_filter - 1) <
            moduleFilters.size()) {
            modulePreview =
                moduleFilters[
                    static_cast<std::size_t>(
                        g_module_filter - 1)].
                name.c_str();
        }

        ImGui::SetNextItemWidth(
            narrow
            ? fieldWidth
            : 180.0f);

        if (ImGui::BeginCombo(
            "##module_filter",
            modulePreview)) {
            if (ImGui::Selectable(
                "Todos os componentes",
                g_module_filter == 0)) {
                g_module_filter = 0;
            }

            for (std::size_t index = 0;
                index < moduleFilters.size();
                ++index) {
                const bool selected =
                    g_module_filter ==
                    static_cast<int>(
                        index + 1);

                if (ImGui::Selectable(
                    moduleFilters[index].
                    name.c_str(),
                    selected)) {
                    g_module_filter =
                        static_cast<int>(
                            index + 1);
                }
            }

            ImGui::EndCombo();
        }

        if (narrow) {
            ImGui::Spacing();
        }
        else {
            ImGui::SameLine();
        }

        const char* categoryPreview =
            g_category_filter < 0
            ? "Todas as categorias"
            : Launcher::ChangeTypeLabel(
                static_cast<
                Launcher::ChangeType>(
                    g_category_filter));

        ImGui::SetNextItemWidth(
            narrow
            ? fieldWidth
            : 190.0f);

        if (ImGui::BeginCombo(
            "##category_filter",
            categoryPreview)) {
            if (ImGui::Selectable(
                "Todas as categorias",
                g_category_filter < 0)) {
                g_category_filter = -1;
            }

            const Launcher::ChangeType categories[] = {
                Launcher::ChangeType::Added,
                Launcher::ChangeType::Improved,
                Launcher::ChangeType::Fixed,
                Launcher::ChangeType::Performance,
                Launcher::ChangeType::Compatibility,
                Launcher::ChangeType::Security,
                Launcher::ChangeType::Removed,
                Launcher::ChangeType::Breaking
            };

            for (Launcher::ChangeType category :
            categories) {
                const int value =
                    static_cast<int>(category);

                if (ImGui::Selectable(
                    Launcher::ChangeTypeLabel(
                        category),
                    g_category_filter ==
                    value)) {
                    g_category_filter =
                        value;
                }
            }

            ImGui::EndCombo();
        }

        if (narrow) {
            ImGui::Spacing();
        }
        else {
            ImGui::SameLine();
        }

        const char* sortLabels[] = {
            "Mais recentes",
            "Mais antigas",
            "Apenas não lidas"
        };

        if (g_sort < 0 ||
            g_sort >=
            static_cast<int>(
                std::size(sortLabels))) {
            g_sort = 0;
        }

        ImGui::SetNextItemWidth(
            narrow
            ? fieldWidth
            : 145.0f);

        if (ImGui::BeginCombo(
            "##sort_updates",
            sortLabels[g_sort])) {
            for (int index = 0;
                index <
                static_cast<int>(
                    std::size(sortLabels));
                    ++index) {
                if (ImGui::Selectable(
                    sortLabels[index],
                    g_sort == index)) {
                    g_sort = index;
                }
            }

            ImGui::EndCombo();
        }

        if (FiltersActive()) {
            if (!narrow) {
                ImGui::SameLine();
            }

            if (CyberWidgets::Button(
                "Limpar filtros", CyberWidgets::ButtonStyle::Ghost)) {
                g_search[0] = '\0';
                g_module_filter = 0;
                g_category_filter = -1;
                g_sort = 0;
            }
        }

        PopControlStyle();

        ImGui::EndChild();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        if (!snapshot.message.empty() &&
            snapshot.status !=
            Launcher::ChangelogStatus::Loading) {
            ImGui::Spacing();

            const ImU32 statusColor =
                snapshot.status ==
                Launcher::ChangelogStatus::Error
                ? IM_COL32(
                    213,
                    132,
                    72,
                    255)
                : C_MUTED;

            ImGui::TextColored(
                ToVec4(statusColor),
                "%s",
                snapshot.message.c_str());
        }

        ImGui::Spacing();

        const bool latestPanelVisible =
            !FiltersActive() &&
            !snapshot.releases.empty();

        std::vector<CardView> filtered;

        filtered.reserve(
            allCards.size());

        for (const CardView& card :
            allCards) {
            if (card.release == nullptr ||
                card.module == nullptr) {
                continue;
            }

            if (latestPanelVisible &&
                card.release->id ==
                snapshot.releases.front().id) {
                continue;
            }

            if (g_module_filter > 0) {
                const std::size_t selectedIndex =
                    static_cast<std::size_t>(
                        g_module_filter - 1);

                if (selectedIndex >=
                    moduleFilters.size()) {
                    continue;
                }

                if (card.module->id !=
                    moduleFilters[selectedIndex].id) {
                    continue;
                }
            }

            if (!HasCategory(card) ||
                !ContainsSearch(card)) {
                continue;
            }

            if (g_sort == 2 &&
                Launcher::IsUpdateRead(
                    card.id.c_str())) {
                continue;
            }

            filtered.push_back(card);
        }

        std::sort(
            filtered.begin(),
            filtered.end(),
            [](
                const CardView& left,
                const CardView& right) {
                    if (left.release == nullptr ||
                        left.module == nullptr) {
                        return false;
                    }

                    if (right.release == nullptr ||
                        right.module == nullptr) {
                        return true;
                    }

                    if (left.release->publishedAt !=
                        right.release->publishedAt) {
                        return
                            left.release->publishedAt >
                            right.release->publishedAt;
                    }

                    return
                        left.module->name <
                        right.module->name;
            });

        if (g_sort == 1) {
            std::reverse(
                filtered.begin(),
                filtered.end());
        }

        ImGui::BeginChild(
            "##updates_scroll",
            ImVec2(
                0.0f,
                0.0f),
            false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (snapshot.status ==
            Launcher::ChangelogStatus::Loading &&
            allCards.empty()) {
            CyberWidgets::TextLine("A carregar atualizações…", CyberWidgets::TextTone::Secondary);
            ImGui::Spacing();
            for (int skeleton = 0; skeleton < 3; ++skeleton) {
                CyberWidgets::SkeletonLine(CyberTheme::Px(190.0f), CyberTheme::Px(16.0f));
                ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Xs));
                CyberWidgets::SkeletonLine(-1.0f, CyberTheme::Px(54.0f));
                ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Lg));
            }
        }
        else if (
            (
                snapshot.status ==
                Launcher::ChangelogStatus::Empty ||
                snapshot.status ==
                Launcher::ChangelogStatus::Idle
                ) &&
            allCards.empty()) {
            DrawEmptyState(
                "Ainda não existem atualizações publicadas",
                "As alterações do launcher e dos módulos "
                "dos jogos serão apresentadas aqui quando "
                "existir uma nova versão.");
        }
        else if (
            snapshot.status ==
            Launcher::ChangelogStatus::Error &&
            allCards.empty()) {
            DrawEmptyState(
                "Não foi possível carregar o histórico de atualizações",
                "Verifica a ligação e utiliza o botão "
                "Procurar atualizações para tentar novamente.");
        }
        else {
            if (latestPanelVisible) {
                ImGui::TextColored(
                    ToVec4(C_GOLD),
                    "%s",
                    "VERSÃO MAIS RECENTE");

                ImGui::Spacing();

                DrawLatestReleasePanel(
                    snapshot.releases.front(),
                    snapshot.channel);

                ImGui::Spacing();
            }

            if (filtered.empty()) {
                if (!latestPanelVisible) {
                    DrawEmptyState(
                        "Nenhuma atualização encontrada",
                        "Experimenta alterar os filtros "
                        "ou a pesquisa.");
                }
            }
            else {
                if (latestPanelVisible) {
                    ImGui::TextColored(
                        ToVec4(C_GOLD),
                        "%s",
                        "HISTÓRICO POR JOGO E COMPONENTE");

                    ImGui::Spacing();
                }

                const float timelineX =
                    ImGui::GetCursorScreenPos().x +
                    10.0f;

                const float cardOffset =
                    30.0f;

                const float cardWidth =
                    (std::max)(
                        1.0f,
                        ImGui::GetContentRegionAvail().x -
                        cardOffset -
                        8.0f);

                std::string lastGroup;

                for (const CardView& card :
                    filtered) {
                    if (card.release == nullptr) {
                        continue;
                    }

                    const std::string group =
                        MonthGroup(
                            card.release->publishedAt);

                    if (group != lastGroup) {
                        ImGui::SetCursorPosX(
                            cardOffset);

                        ImGui::TextColored(
                            ToVec4(C_GOLD),
                            "%s",
                            group.c_str());

                        lastGroup = group;
                    }

                    ImDrawList* draw =
                        ImGui::GetWindowDrawList();

                    const ImVec2 marker(
                        timelineX,
                        ImGui::GetCursorScreenPos().y +
                        18.0f);

                    draw->AddCircleFilled(
                        marker,
                        3.5f,
                        C_GOLD,
                        10);

                    draw->AddLine(
                        ImVec2(
                            timelineX,
                            marker.y + 5.0f),
                        ImVec2(
                            timelineX,
                            marker.y + 260.0f),
                        IM_COL32(
                            212,
                            175,
                            55,
                            42),
                        1.0f);

                    ImGui::SetCursorPosX(
                        cardOffset);

                    DrawUpdateCard(
                        card,
                        cardWidth);
                }
            }
        }

        ImGui::EndChild();

        if (body != nullptr) {
            ImGui::PopFont();
        }

        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

} // namespace LauncherUpdates
