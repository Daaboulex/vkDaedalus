#include "imgui_overlay.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "effects/effect_registry.hpp"
#include "gpu_assignment.hpp"
#include "overlay/widgets.hpp"

#include "imgui.h"

namespace vkBasalt
{
    namespace
    {
        std::string gpuLabel(const OverlayGpu& gpu)
        {
            return "GPU " + std::to_string(gpu.number) + ": " + (gpu.name.empty() ? gpu.id : shortGpuName(gpu.name));
        }

        void gpuTooltip(const OverlayGpu& gpu)
        {
            ImGui::SetItemTooltip("%s\n%s", gpu.name.empty() ? "no name reported" : gpu.name.c_str(), gpu.id.c_str());
        }
    }

    void ImGuiOverlay::changeGpus(const std::vector<GpuListEntry>& list, const std::vector<std::pair<std::string, uint32_t>>& rows)
    {
        pEffectRegistry->setGpuList(list);
        for (const auto& [name, gpu] : rows)
            pEffectRegistry->setEffectGpu(name, gpu);
        applyRequested = true;
        profileDirty   = true;
        lastChangeTime = std::chrono::steady_clock::now();
    }

    void ImGuiOverlay::renderGpusView()
    {
        if (!pEffectRegistry)
            return;
        const OverlayPalette& palette = activePalette();
        const float           scale   = ImGui::GetStyle().FontScaleMain;

        ui::heading(state.gpus.size() + state.addableGpus.size() == 1 ? "GPU" : "GPUs");

        const GpuPlan               plan      = pEffectRegistry->gpuPlan();
        std::vector<GpuListEntry>   list      = plan.list;
        const std::vector<uint32_t> owns      = plan.ownGpus;
        const auto                  rowsMoved = [&](auto renumber) {
            std::vector<std::pair<std::string, uint32_t>> moved;
            for (size_t r = 0; r < plan.rows.size(); r++)
                if (const uint32_t to = renumber(owns[r]); to != owns[r])
                    moved.emplace_back(plan.rows[r], to);
            return moved;
        };

        ui::beginScrollArea("##gpusbody");
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(12.0f), ui::px(2.0f)));
        const bool table = ImGui::BeginTable("##gpus", 2, ImGuiTableFlags_SizingStretchSame);
        ImGui::PopStyleVar();
        if (!table)
        {
            ui::endScrollArea();
            return;
        }
        ImGui::TableNextColumn();
        ui::sectionLabel("Effects run on");
        const std::vector<uint32_t> runsOn  = gpusOfRows(plan.rows, plan.ownGpus, pEffectRegistry->getActiveEffects());
        const auto                  effects = [&](uint32_t number, const std::string& note) {
            const auto count = std::count(runsOn.begin(), runsOn.end(), number);
            return count == 0 ? note : note + ", " + std::to_string(count) + (count == 1 ? " effect" : " effects");
        };
        const float rowH          = ui::px(ui::Size::row);
        const float inRowH        = ui::px(ui::Size::inRow);
        const auto  rowBackground = [&]() {
            const ImVec2 at      = ImGui::GetCursorScreenPos();
            const ImVec2 to      = ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + rowH);
            const float  hover   = ui::eased(ImGui::GetID("row"), "hover", ImGui::IsMouseHoveringRect(at, to) && ImGui::IsWindowHovered());
            ImGui::GetWindowDrawList()->AddRectFilled(at, to, ImGui::GetColorU32(ui::mix(palette.row, ui::mix(palette.row, palette.accent, 0.07f), hover)),
                                                      ui::px(ui::Size::radius));
            return at;
        };
        const auto rowText = [&](const ImVec2& at, const std::string& number, const std::string& name, const std::string& note, bool dim) {
            ImDrawList* draw  = ImGui::GetWindowDrawList();
            const float big   = FontSize::body * scale;
            const float small = FontSize::caption * scale;
            const float top   = at.y + (rowH - big - small - 2.0f * scale) * 0.5f;
            draw->AddText(fonts.body, FontSize::label * scale, ImVec2(at.x + 12.0f * scale, at.y + (rowH - FontSize::label * scale) * 0.5f), ImGui::GetColorU32(dim ? palette.disabled : palette.muted), number.c_str());
            draw->AddText(fonts.bold, big, ImVec2(at.x + 32.0f * scale, top), ImGui::GetColorU32(dim ? palette.secondary : palette.text), name.c_str());
            draw->AddText(fonts.body, small, ImVec2(at.x + 32.0f * scale, top + big + 2.0f * scale), ImGui::GetColorU32(palette.muted), note.c_str());
        };
        const auto nextRow = [&](const ImVec2& at) {
            ImGui::SetCursorScreenPos(at);
            ImGui::Dummy(ImVec2(0.0f, rowH));
        };
        for (const OverlayGpu& gpu : state.gpus)
        {
            ImGui::PushID(static_cast<int>(gpu.number));
            const ImVec2      at     = rowBackground();
            const size_t      entry  = gpu.number - firstListedGpu;
            const std::string number = std::to_string(gpu.number);
            rowText(at, number, gpu.name.empty() ? gpu.id : shortGpuName(gpu.name),
                    gpu.number == gameGpu ? effects(gameGpu, "the game draws here") : gpu.runsChain ? effects(gpu.number, gpu.status) : gpu.status, false);
            ImGui::SetCursorScreenPos(at);
            ImGui::InvisibleButton("##gpuname", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, rowH));
            gpuTooltip(gpu);
            if (gpu.number >= firstListedGpu && entry < list.size())
            {
                const std::string up     = "Up##" + number;
                const std::string down   = "Down##" + number;
                const std::string remove = "Remove##" + number;
                const float       gap    = ImGui::GetStyle().ItemSpacing.x;
                const float       right  = at.x + ImGui::GetContentRegionAvail().x - ui::px(ui::Size::gap);
                const float       width  = ui::buttonWidth(up.c_str(), ui::Kind::Secondary, true) + ui::buttonWidth(down.c_str(), ui::Kind::Secondary, true)
                                    + inRowH + 2.0f * gap;
                ImGui::SetCursorScreenPos(ImVec2(right - width, at.y + (rowH - inRowH) * 0.5f));
                ImGui::BeginDisabled(gpu.number == firstListedGpu);
                if (ui::rowButton(up.c_str()))
                {
                    std::swap(list[entry], list[entry - 1]);
                    const uint32_t n = gpu.number;
                    changeGpus(list, rowsMoved([n](uint32_t own) { return ownGpuAfterSwap(own, n, n - 1); }));
                }
                ImGui::EndDisabled();
                ImGui::SetItemTooltip(gpu.number == firstListedGpu ? "First after the game's GPU" : "Move up");
                ImGui::SameLine(0.0f, gap);
                ImGui::BeginDisabled(entry + 1 >= list.size());
                if (ui::rowButton(down.c_str()))
                {
                    std::swap(list[entry], list[entry + 1]);
                    const uint32_t n = gpu.number;
                    changeGpus(list, rowsMoved([n](uint32_t own) { return ownGpuAfterSwap(own, n, n + 1); }));
                }
                ImGui::EndDisabled();
                ImGui::SetItemTooltip(entry + 1 >= list.size() ? "Last in the list" : "Move down");
                ImGui::SameLine(0.0f, gap);
                if (ui::iconButton(remove.c_str(), Icon::Close, "Take it out of the list: its effects go to GPU 1"))
                {
                    list.erase(list.begin() + static_cast<std::ptrdiff_t>(entry));
                    const uint32_t n = gpu.number;
                    changeGpus(list, rowsMoved([n](uint32_t own) { return ownGpuAfterRemove(own, n); }));
                }
            }
            nextRow(at);
            ImGui::PopID();
        }
        for (const OverlayGpu& addable : state.addableGpus)
        {
            ImGui::PushID(addable.id.c_str());
            const ImVec2 at = rowBackground();
            rowText(at, "", addable.name.empty() ? addable.id : shortGpuName(addable.name), "not in use", true);
            ImGui::SetCursorScreenPos(at);
            ImGui::InvisibleButton("##gpuname", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, rowH));
            gpuTooltip(addable);
            const std::string add  = "Add##" + addable.id;
            const float       addW = ui::buttonWidth(add.c_str(), ui::Kind::Secondary, true);
            ImGui::SetCursorScreenPos(ImVec2(at.x + ImGui::GetContentRegionAvail().x - ui::px(ui::Size::gap) - addW, at.y + (rowH - inRowH) * 0.5f));
            if (ui::rowButton(add.c_str()))
            {
                list.push_back({addable.id, parseGpuId(addable.id)});
                changeGpus(list, {});
            }
            ImGui::SetItemTooltip("Adds it as GPU %zu", list.size() + firstListedGpu);
            nextRow(at);
            ImGui::PopID();
        }

        renderRoundTrip();
        ui::nextColumn();
        renderGpuReadings();
        ImGui::EndTable();
        ui::endScrollArea();
        ui::footer();
        if (state.gpus.size() <= 1 && state.addableGpus.empty())
            ui::note("One GPU");
        else if (activeProfileName.empty())
            ui::note("Not saved");
        else
            ui::notef("Saved in profile %s", activeProfileName.c_str());
    }

    void ImGuiOverlay::renderGpuChoices(const std::string& effectName, uint32_t gpuBefore)
    {
        const uint32_t current = pEffectRegistry->getEffectGpu(effectName);
        for (const OverlayGpu& gpu : state.gpus)
        {
            const uint32_t own = gpu.number == gameGpu && gpuBefore == gameGpu ? 0u : gpu.number;
            if (ui::option(gpuLabel(gpu).c_str(), current == own))
                changeGpus(pEffectRegistry->gpuPlan().list, {{effectName, own}});
            gpuTooltip(gpu);
        }
        ui::menuSeparator();
        if (ui::option("Add GPU...", false))
            openSection = OverlaySection::Gpus;
    }

    void ImGuiOverlay::renderGpuMenu(const std::string& effectName, uint32_t gpuBefore)
    {
        if (!pEffectRegistry || state.gpus.size() <= 1)
            return;
        if (!ui::beginSubmenu("GPU"))
            return;
        renderGpuChoices(effectName, gpuBefore);
        ui::endSubmenu();
    }
}
