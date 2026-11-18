#include "imgui_overlay.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "effects/effect_registry.hpp"
#include "gpu_assignment.hpp"
#include "mako_status.hpp"
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
        ui::sectionLabel("Per GPU");
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
        const auto rowText = [&](const ImVec2& at, const std::string& number, const std::string& name, const std::string& note, bool dim, bool badgeLine) {
            ImDrawList* draw  = ImGui::GetWindowDrawList();
            const float big   = FontSize::body * scale;
            const float small = FontSize::caption * scale;
            const float line  = badgeLine ? ui::rowBadgeHeight() : small;
            const float top   = at.y + (rowH - big - line - 2.0f * scale) * 0.5f;
            const float noteY = top + big + 2.0f * scale;
            draw->AddText(fonts.body, FontSize::label * scale, ImVec2(at.x + 12.0f * scale, at.y + (rowH - FontSize::label * scale) * 0.5f), ImGui::GetColorU32(dim ? palette.disabled : palette.muted), number.c_str());
            draw->AddText(fonts.bold, big, ImVec2(at.x + 32.0f * scale, top), ImGui::GetColorU32(dim ? palette.secondary : palette.text), name.c_str());
            draw->AddText(fonts.body, small, ImVec2(at.x + 32.0f * scale, noteY + (line - small) * 0.5f), ImGui::GetColorU32(palette.muted), note.c_str());
            const float noteW = note.empty() ? 0.0f : fonts.body->CalcTextSizeA(small, FLT_MAX, 0.0f, note.c_str()).x + ui::px(8.0f);
            return ImVec2(at.x + 32.0f * scale + noteW, noteY);
        };
        const auto nextRow = [&](const ImVec2& at) {
            ImGui::SetCursorScreenPos(at);
            ImGui::Dummy(ImVec2(0.0f, rowH));
        };
        const std::vector<std::string> active  = pEffectRegistry->getActiveEffects();
        const std::vector<uint32_t>    rowGpus = effectiveGpus(plan.ownGpus);
        const FrameGenerationState        fg       = frameGenerationState();
        const bool                        mako     = fg.layer == FrameGenerationLayer::Mako;
        const bool                        scales   = mako && fg.blocker.empty() && makoScaling().run == MakoScalingRun::Scaling;
        const FrameGenerationTab*         chosen   = mako && fg.blocker.empty() ? frameGenerationTabChosen(fg.layer) : nullptr;
        const bool                        lossless = chosen != nullptr && chosen->makoModel == "lossless-scaling";
        const std::optional<MakoGpuClaim> claim    = mako && fg.blocker.empty() ? makoGpuClaimHere() : std::nullopt;
        const std::vector<uint32_t>       fgGpus   = claim ? claim->gpus : std::vector<uint32_t>{gameGpu};
        const auto                        generatesOn = [&](uint32_t number) {
            return fg.on && fg.blocker.empty() && std::find(fgGpus.begin(), fgGpus.end(), number) != fgGpus.end();
        };
        const auto hasWork = [&](uint32_t number) {
            if (generatesOn(number) || (number == gameGpu && scales))
                return true;
            for (const std::string& name : active)
            {
                const auto row = std::find(plan.rows.begin(), plan.rows.end(), name);
                if ((row == plan.rows.end() ? gameGpu : rowGpus[static_cast<size_t>(row - plan.rows.begin())]) == number)
                    return true;
            }
            return false;
        };
        const std::string fgTip = !mako ? "lsfg-vk generates frames on the GPU the game renders on.\nIt takes each frame as memory only that GPU can import, and has no GPU setting."
                                  : lossless
                                      ? (claim ? makoGpuClaimLine(*claim) : std::string("MAKO generates frames on the GPU its file names when the game starts, the game's unless another is picked"))
                                            + ".\nPick its GPU in the GPU row of the Frame generation card; MAKO reads it when the game starts."
                                      : (claim ? makoGpuClaimLine(*claim) + ".\n" : std::string("MAKO generates frames on the GPU the game renders on.\n"))
                                            + "MAKO FG does not start its frame generation on a GPU other than the game's.";
        const Badge fgBadge = claim && !claim->generating ? Badge{Tone::Bad, Icon::Warning, "Frame generation, MAKO"}
                              : lossless                  ? Badge{Tone::Info, std::nullopt, "Frame generation, MAKO"}
                                                          : Badge{Tone::Info, Icon::Lock, std::string("Frame generation, ") + (mako ? "MAKO" : "lsfg-vk")};
        const auto work = [&](uint32_t number, const ImVec2& from, float right) {
            const float               gap = ui::px(ui::Size::gap);
            std::vector<Badge>        shown;
            std::vector<std::string>  ids;
            std::vector<std::string>  tips;
            std::vector<std::string>  effects;
            if (generatesOn(number))
            {
                shown.push_back(fgBadge);
                ids.push_back("Frame generation##pergpu");
                tips.push_back(fgTip);
                effects.emplace_back();
            }
            if (number == gameGpu && scales)
            {
                shown.push_back({Tone::Info, Icon::Lock, "Scaling, MAKO"});
                ids.push_back("Scaling##pergpu");
                tips.push_back("MAKO scales inside its own layer, on the GPU the game renders on.");
                effects.emplace_back();
            }
            for (const std::string& name : active)
            {
                const auto     row  = std::find(plan.rows.begin(), plan.rows.end(), name);
                const uint32_t runs = row == plan.rows.end() ? gameGpu : rowGpus[static_cast<size_t>(row - plan.rows.begin())];
                if (runs != number)
                    continue;
                shown.push_back({Tone::Muted, std::nullopt, name});
                ids.push_back(name + "##pergpu");
                tips.push_back("Runs on GPU " + std::to_string(number) + ": pick another GPU for it");
                effects.push_back(name);
            }
            const auto gpuBefore = [&](const std::string& name) {
                const auto   row = std::find(plan.rows.begin(), plan.rows.end(), name);
                const size_t r   = static_cast<size_t>(row - plan.rows.begin());
                return row == plan.rows.end() || r == 0 ? gameGpu : rowGpus[r - 1];
            };
            float  x     = from.x;
            size_t drawn = 0;
            for (; drawn < shown.size(); drawn++)
            {
                const float  width = ui::badgeWidth(shown[drawn], true);
                const size_t left  = shown.size() - drawn - 1;
                const float  more  = left == 0 ? 0.0f : gap + ui::badgeWidth(Badge{Tone::Muted, std::nullopt, "+" + std::to_string(left)}, true);
                if (x + width + more > right)
                    break;
                const bool clickable = !effects[drawn].empty();
                if (ui::badgeAt(shown[drawn], ImVec2(x, from.y), ids[drawn].c_str(), tips[drawn].c_str(), clickable))
                    ImGui::OpenPopup(("pergpu " + effects[drawn]).c_str());
                if (clickable && ui::beginPopup(("pergpu " + effects[drawn]).c_str()))
                {
                    renderGpuChoices(effects[drawn], gpuBefore(effects[drawn]));
                    ui::endPopup();
                }
                x += width + gap;
            }
            if (drawn < shown.size())
            {
                const std::string label = "+" + std::to_string(shown.size() - drawn);
                const std::string popup = "pergpu more " + std::to_string(number);
                if (ui::badgeAt(Badge{Tone::Muted, std::nullopt, label}, ImVec2(x, from.y), (label + "##pergpu").c_str(), "More on this GPU", true))
                    ImGui::OpenPopup(popup.c_str());
                if (ui::beginPopup(popup.c_str()))
                {
                    for (size_t i = drawn; i < shown.size(); i++)
                    {
                        if (effects[i].empty())
                            ui::badge(shown[i], tips[i].c_str(), true);
                        else if (ui::beginSubmenu(effects[i].c_str()))
                        {
                            renderGpuChoices(effects[i], gpuBefore(effects[i]));
                            ui::endSubmenu();
                        }
                    }
                    ui::endPopup();
                }
            }
            return !shown.empty();
        };
        for (const OverlayGpu& gpu : state.gpus)
        {
            ImGui::PushID(static_cast<int>(gpu.number));
            const ImVec2      at     = rowBackground();
            const size_t      entry  = gpu.number - firstListedGpu;
            const std::string number = std::to_string(gpu.number);
            const float       rowW   = ImGui::GetContentRegionAvail().x;
            const bool        busy   = hasWork(gpu.number);
            const ImVec2      line   = rowText(at, number, gpu.name.empty() ? gpu.id : shortGpuName(gpu.name),
                                               gpu.number == gameGpu ? "the game draws here" : gpu.status, false, busy);
            float             limit  = at.x + rowW - ui::px(ui::Size::gap);
            ImGui::SetCursorScreenPos(at);
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##gpuname", ImVec2(rowW * 0.5f, rowH));
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
                limit = right - width - ui::px(ui::Size::gap);
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
            if (busy)
                work(gpu.number, line, limit);
            nextRow(at);
            ImGui::PopID();
        }
        for (const OverlayGpu& addable : state.addableGpus)
        {
            ImGui::PushID(addable.id.c_str());
            const ImVec2 at   = rowBackground();
            const bool   busy = generatesOn(addable.number);
            const ImVec2 line = rowText(at, "", addable.name.empty() ? addable.id : shortGpuName(addable.name), busy ? "" : "not in use", true, busy);
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
            if (busy)
                ui::badgeAt(fgBadge, line, "Frame generation##pergpu", fgTip.c_str());
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
