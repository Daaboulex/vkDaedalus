#include "imgui_overlay.hpp"
#include "overlay/overlay_style.hpp"
#include "overlay/widgets.hpp"
#include "effects/effect_registry.hpp"
#include "effects/builtin/builtin_effects.hpp"
#include "settings_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>

#include "imgui.h"

namespace vkBasalt
{
    static bool matchesSearch(const std::string& text, const char* search)
    {
        if (!search || !search[0])
            return true;
        std::string lowerText = text;
        std::string lowerSearch = search;
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
        std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
        return lowerText.find(lowerSearch) != std::string::npos;
    }

    void ImGuiOverlay::renderAddEffectsView()
    {
        if (!pEffectRegistry)
            return;

        std::vector<std::string> selectedEffects = pEffectRegistry->getSelectedEffects();

        if (!ImGui::IsAnyItemActive())
        {
            ImGuiIO& io = ImGui::GetIO();
            for (int i = 0; i < io.InputQueueCharacters.Size; i++)
            {
                ImWchar c = io.InputQueueCharacters[i];
                if (isPrintableAscii(c))
                {
                    size_t len = strlen(addEffectsSearch);
                    if (len < sizeof(addEffectsSearch) - 1)
                    {
                        addEffectsSearch[len] = static_cast<char>(c);
                        addEffectsSearch[len + 1] = '\0';
                    }
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && addEffectsSearch[0] != '\0')
            {
                size_t len = strlen(addEffectsSearch);
                if (len > 0)
                    addEffectsSearch[len - 1] = '\0';
            }
        }

        size_t maxEffectsLimit = static_cast<size_t>(settingsManager.getMaxEffects());
        const OverlayPalette& palette = activePalette();
        const ImGuiStyle&     style   = ImGui::GetStyle();

        const std::vector<std::string> compilingEffects = effectCompileJobs.compiling();
        size_t currentCount = selectedEffects.size();
        size_t pendingCount = pendingAddEffects.size() + compilingEffects.size();
        size_t totalCount = currentCount + pendingCount;

        char headingText[48];
        if (insertPosition)
            std::snprintf(headingText, sizeof(headingText), "Insert at position %zu", *insertPosition + 1);
        else
            std::snprintf(headingText, sizeof(headingText), "Add effects");
        ui::heading(headingText);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(palette.muted, "%zu of %zu", totalCount, maxEffectsLimit);
        ImGui::SetItemTooltip("Effects in the chain and chosen here, of the effect limit in Settings");

        const std::vector<std::string> builtinEffects = BuiltInEffects::instance().getTypeNames();

        auto isNameUsed = [&](const std::string& name) {
            if (std::find(selectedEffects.begin(), selectedEffects.end(), name) != selectedEffects.end())
                return true;
            for (const auto& p : pendingAddEffects)
                if (p.instanceName == name)
                    return true;
            if (std::find(compilingEffects.begin(), compilingEffects.end(), name) != compilingEffects.end())
                return true;
            return false;
        };

        auto getNextInstanceName = [&](const std::string& effectType) -> std::string {
            if (!isNameUsed(effectType))
                return effectType;
            for (int n = 2; n <= 99; n++)
            {
                std::string candidate = effectType + "." + std::to_string(n);
                if (!isNameUsed(candidate))
                    return candidate;
            }
            return effectType + ".99";
        };

        const bool safeAntiCheat = settingsManager.getSafeAntiCheat();
        auto isDepthEffect = [&](const std::string& effectType) -> bool {
            const auto it = state.effectPaths.find(effectType);
            return it != state.effectPaths.end()
                && depthEffectPaths.count(std::filesystem::path(it->second).lexically_normal().string()) > 0;
        };

        const auto isBuiltIn = [&](const std::string& effectType) {
            return std::find(builtinEffects.begin(), builtinEffects.end(), effectType) != builtinEffects.end();
        };

        auto renderAddButton = [&](const std::string& effectType, const std::string& tooltip = "") {
            const bool atLimit      = totalCount >= maxEffectsLimit;
            const bool depthBlocked = safeAntiCheat && isDepthEffect(effectType);
            if (ui::listItem(effectType.c_str(), isBuiltIn(effectType) ? "built in" : "ReShade", !atLimit && !depthBlocked))
            {
                std::string instanceName = getNextInstanceName(effectType);
                pendingAddEffects.push_back({instanceName, effectType});
            }
            if (depthBlocked)
                ImGui::SetItemTooltip("Reads depth, so Safe Anti-Cheat blocks it");
            else if (atLimit)
                ImGui::SetItemTooltip("The chain is at the effect limit set in Settings");
            else if (!tooltip.empty())
                ImGui::SetItemTooltip("%s", tooltip.c_str());
        };

        const float gap         = ui::px(ui::Size::gap);
        const float columnWidth = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const auto  beginWell   = [&](const char* id) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.well);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ui::px(ui::Size::radius));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::px(6.0f), ui::px(6.0f)));
            ImGui::BeginChild(id, ImVec2(columnWidth, ui::heightLeftAboveFooter()), ImGuiChildFlags_AlwaysUseWindowPadding);
        };
        const auto endWell = [&] {
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        };

        const float listsTop = ImGui::GetCursorPosY() + ui::px(ui::Size::control) + style.ItemSpacing.y;
        ImGui::BeginGroup();
        bool hasSearch = addEffectsSearch[0] != '\0';
        ui::textField("##search", addEffectsSearch, sizeof(addEffectsSearch), "Search", columnWidth, ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SetCursorPosY(listsTop);
        beginWell("EffectList");

        std::vector<std::string> sortedCurrentConfig = state.currentConfigEffects;
        std::vector<std::string> sortedDefaultConfig = state.defaultConfigEffects;
        std::sort(sortedCurrentConfig.begin(), sortedCurrentConfig.end());
        std::sort(sortedDefaultConfig.begin(), sortedDefaultConfig.end());

        const auto anyMatch = [&](const std::vector<std::string>& names) {
            return std::any_of(names.begin(), names.end(), [&](const std::string& name) { return matchesSearch(name, addEffectsSearch); });
        };
        const auto renderGroup = [&](const std::vector<std::string>& names, const std::string& title, bool withPaths) {
            if (!anyMatch(names))
                return false;
            if (!hasSearch)
                ui::sectionLabel(title.c_str());
            for (const auto& effectType : names)
            {
                if (!matchesSearch(effectType, addEffectsSearch))
                    continue;
                const BuiltInEffectDef* builtIn = BuiltInEffects::instance().getDef(effectType);
                const auto              it      = state.effectPaths.find(effectType);
                renderAddButton(effectType, builtIn != nullptr ? builtIn->purpose : withPaths && it != state.effectPaths.end() ? it->second : "");
            }
            return true;
        };
        const bool hasBuiltinMatches = renderGroup(builtinEffects, "Built in", true);
        const bool hasCurrentMatches = renderGroup(sortedCurrentConfig, "ReShade, " + state.configFileName, true);
        const bool hasDefaultMatches = renderGroup(sortedDefaultConfig, "ReShade, every shader folder", true);

        if (hasSearch && !hasBuiltinMatches && !hasCurrentMatches && !hasDefaultMatches)
            ui::notef("No effects match '%s'", addEffectsSearch);

        endWell();
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, gap);

        ImGui::BeginGroup();
        ui::sectionLabel("Adding");
        if (!pendingAddEffects.empty())
        {
            ImGui::SameLine();
            ImGui::TextColored(palette.muted, "%zu", pendingAddEffects.size());
        }
        ImGui::SetCursorPosY(listsTop);
        beginWell("PendingList");
        for (size_t i = 0; i < pendingAddEffects.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i));
            const std::string removeLabel = "x##" + pendingAddEffects[i].instanceName;
            const ImVec2      rowMin      = ImGui::GetCursorScreenPos();
            const float       rowW        = ImGui::GetContentRegionAvail().x;
            const float       rowH        = ui::px(ui::Size::row);
            const float       button      = ui::px(ui::Size::inRow);
            ImDrawList*       list        = ImGui::GetWindowDrawList();
            list->AddRectFilled(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH), ImGui::GetColorU32(palette.row), ui::px(ui::Size::radius));
            const float removeX = rowMin.x + rowW - ui::px(ui::Size::gap) - button;
            ImGui::SetCursorScreenPos(ImVec2(removeX, rowMin.y + (rowH - button) * 0.5f));
            if (ui::iconButton(removeLabel.c_str(), Icon::Close, "Take it back out"))
            {
                pendingAddEffects.erase(pendingAddEffects.begin() + i);
                ImGui::PopID();
                continue;
            }
            const auto& [instanceName, effectType] = pendingAddEffects[i];
            const float nameX = rowMin.x + ui::px(12.0f);
            list->AddText(ImVec2(nameX, rowMin.y + (rowH - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(palette.text), instanceName.c_str());
            const char*  tag     = instanceName != effectType ? effectType.c_str() : (isBuiltIn(effectType) ? "built in" : "ReShade");
            const float  tagSize = FontSize::caption * ui::px(1.0f);
            const ImVec2 tagText = fonts.body->CalcTextSizeA(tagSize, FLT_MAX, 0.0f, tag);
            list->AddText(fonts.body, tagSize, ImVec2(removeX - ui::px(ui::Size::gap) - tagText.x, rowMin.y + (rowH - tagSize) * 0.5f), ImGui::GetColorU32(palette.muted), tag);
            ImGui::SetCursorScreenPos(rowMin);
            ImGui::Dummy(ImVec2(rowW, rowH));
            ImGui::PopID();
        }

        if (pendingAddEffects.empty())
            ui::note("Nothing chosen");
        endWell();
        ImGui::EndGroup();

        ui::footer();

        const size_t      chosen   = pendingAddEffects.size();
        const std::string addLabel = chosen == 0   ? std::string("Add###addchosen")
                                     : chosen == 1 ? std::string("Add 1 effect###addchosen")
                                                   : "Add " + std::to_string(chosen) + " effects###addchosen";
        ui::rightAlign(ui::buttonWidth("Cancel") + style.ItemSpacing.x + ui::buttonWidth(addLabel.c_str(), ui::Kind::Primary));
        const bool cancelled = ui::button("Cancel");
        ImGui::SameLine();
        ImGui::BeginDisabled(chosen == 0);
        const bool addChosen = ui::button(addLabel.c_str(), ui::Kind::Primary);
        ImGui::EndDisabled();
        if (chosen == 0)
            ImGui::SetItemTooltip("Click an effect on the left first");
        if (addChosen)
        {
            EffectAddBatch batch;
            batch.position = insertPosition;
            for (const auto& [instanceName, effectType] : pendingAddEffects)
                batch.effects.push_back({instanceName, effectType, pEffectRegistry->effectFilePathFor(effectType)});
            if (!batch.effects.empty())
                startAddedEffects(std::move(batch));
            pendingAddEffects.clear();
            insertPosition.reset();
            inSelectionMode = false;
            addEffectsSearch[0] = '\0';
        }
        if (cancelled)
        {
            pendingAddEffects.clear();
            insertPosition.reset();
            inSelectionMode = false;
            addEffectsSearch[0] = '\0';
        }
    }

}
