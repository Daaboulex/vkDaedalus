#include "imgui_overlay.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "frame_generation.hpp"
#include "frame_generation_place.hpp"
#include "logical_device.hpp"
#include "latency_stats.hpp"
#include "settings_manager.hpp"
#include "overlay/widgets.hpp"

#include "imgui.h"

namespace vkBasalt
{
    namespace
    {
        std::string milliseconds(double value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f ms", value);
            return text;
        }
    }

    namespace
    {
        std::string presentModeName(VkPresentModeKHR mode)
        {
            switch (mode)
            {
                case VK_PRESENT_MODE_IMMEDIATE_KHR: return "Immediate";
                case VK_PRESENT_MODE_MAILBOX_KHR: return "Mailbox";
                case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
                case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO relaxed";
                default: return "mode " + std::to_string(static_cast<int>(mode));
            }
        }

        std::string colourSpaceName(VkColorSpaceKHR space)
        {
            switch (space)
            {
                case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "sRGB";
                case VK_COLOR_SPACE_HDR10_ST2084_EXT: return "HDR10";
                case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return "scRGB";
                case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: return "scRGB non-linear";
                case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT: return "DCI-P3";
                case VK_COLOR_SPACE_BT2020_LINEAR_EXT: return "BT.2020 linear";
                default: return "colour space " + std::to_string(static_cast<int>(space));
            }
        }

        std::string formatName(VkFormat format)
        {
            switch (format)
            {
                case VK_FORMAT_B8G8R8A8_UNORM: return "BGRA8 UNORM";
                case VK_FORMAT_B8G8R8A8_SRGB: return "BGRA8 sRGB";
                case VK_FORMAT_R8G8B8A8_UNORM: return "RGBA8 UNORM";
                case VK_FORMAT_R8G8B8A8_SRGB: return "RGBA8 sRGB";
                case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "A2B10G10R10";
                case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "A2R10G10B10";
                case VK_FORMAT_R16G16B16A16_SFLOAT: return "RGBA16 float";
                default: return "format " + std::to_string(static_cast<int>(format));
            }
        }
    }

    std::optional<uint32_t> ImGuiOverlay::frameGenerationMultiplierHere()
    {
        if (activeGameName.empty() || !frameGenerationLayerLoaded() || !readFrameGenerationOnce() || !frameGenerationLiveHere()
            || !generatesFrames(frameGenerationProfileInUse()))
            return std::nullopt;
        return frameGenerationProfileInUse()->multiplier;
    }

    void ImGuiOverlay::renderLatencyView()
    {
        const OverlayPalette&         palette    = activePalette();
        const float                   scale      = ImGui::GetStyle().FontScaleMain;
        const FrameStats              second     = frameStats(latency.lastSecondMs);
        const FrameStats              frames     = frameStats(latency.lastTenSecondsMs);
        const std::optional<uint32_t> multiplier = frameGenerationMultiplierHere();
        const bool                    after      = multiplier && effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace);

        ui::heading("Performance");
        ui::beginScrollArea("##performancebody");
        const float cellPaddingY = ui::px(2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui::px(12.0f), cellPaddingY));
        const bool table = ImGui::BeginTable("##performance", 2, ImGuiTableFlags_SizingStretchSame);
        ImGui::PopStyleVar();
        if (!table)
        {
            ui::endScrollArea();
            return;
        }

        ImGui::TableNextColumn();
        if (frames.count == 0)
            ui::note("Waiting for frames");
        else
        {
            const FrameRates rates = frameRatesOf(second.count > 0 ? second.fps : frames.fps, multiplier, after);
            ImGui::PushFont(fonts.bold, 26.0f);
            ImGui::Text("%.0f FPS", rates.gameFps);
            ImGui::PopFont();
            ImGui::SetItemTooltip("The game's own frames, %s, over the last %.1f s (%zu frame%s)", rates.gameMeasured ? "measured at present" : "estimated",
                                  (second.count > 0 ? second : frames).windowMs / 1000.0, (second.count > 0 ? second : frames).count,
                                  (second.count > 0 ? second : frames).count == 1 ? "" : "s");
            if (multiplier)
            {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                if (!rates.shownMeasured)
                {
                    ImGui::TextColored(palette.warn, "~");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                ImGui::TextColored(palette.muted, "%.0f shown", rates.shownFps);
                ImGui::SetItemTooltip(after ? "Every frame shown passes through vkDaedalus, so it is timed, and the game's own\n"
                                              "rate is it divided by lsfg-vk's multiplier, %ux. LATENCY.md has the formulas."
                                            : "vkDaedalus sees only the game's own frames, and lsfg-vk shows %ux as many.\n"
                                              "LATENCY.md has the formulas.",
                                      *multiplier);
            }
            ImGui::SameLine(0.0f, ui::px(ui::Size::groupGap));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(palette.muted, "%.1f ms average", frames.averageMs);
            ImGui::SetItemTooltip("The mean gap between the frames vkDaedalus sees, over the last %.1f s", frames.windowMs / 1000.0);
            const std::vector<float> plotted(latency.lastTenSecondsMs.begin(), latency.lastTenSecondsMs.end());
            const float              ceiling = static_cast<float>(plotCeilingMs(frames.averageMs, frames.slowestAverageMs));
            ImGui::PushStyleColor(ImGuiCol_PlotLines, palette.ok);
            ImGui::PushStyleColor(ImGuiCol_PlotLinesHovered, palette.warn);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, palette.well);
            ImGui::PlotLines("##frameintervals", plotted.data(), static_cast<int>(plotted.size()), 0, nullptr, 0.0f, ceiling, ImVec2(-FLT_MIN, ui::px(136.0f)));
            ImGui::PopStyleColor(3);
            const ImVec2 plotMin = ImGui::GetItemRectMin();
            const ImVec2 plotMax = ImGui::GetItemRectMax();
            const bool   hovered = ImGui::IsItemHovered();
            if (hovered && !plotted.empty())
            {
                const float  along = std::clamp((ImGui::GetIO().MousePos.x - plotMin.x) / std::max(1.0f, plotMax.x - plotMin.x), 0.0f, 0.9999f);
                const size_t index = static_cast<size_t>(along * static_cast<float>(plotted.size()));
                const size_t ago   = plotted.size() - 1 - index;
                ImGui::SetTooltip("%.2f ms, %zu frame%s ago\nThe plot reaches %.1f ms; a longer frame goes off its top.", plotted[index], ago,
                                  ago == 1 ? "" : "s", ceiling);
            }
            ImDrawList* draw   = ImGui::GetWindowDrawList();
            const float inset  = ui::px(8.0f);
            const float size   = FontSize::caption * scale;
            const float inner  = ImGui::GetStyle().FramePadding.y;
            const float span   = plotMax.y - plotMin.y - 2.0f * inner;
            const float avgY   = plotMax.y - inner - span * std::clamp(static_cast<float>(frames.averageMs) / std::max(ceiling, 0.001f), 0.0f, 1.0f);
            const ImU32 rule   = ImGui::GetColorU32(palette.edge);
            const ImU32 legend = ImGui::GetColorU32(palette.faint);
            for (float x = plotMin.x + inset; x < plotMax.x - inset; x += ui::px(8.0f))
                draw->AddLine(ImVec2(x, avgY), ImVec2(std::min(x + ui::px(4.0f), plotMax.x - inset), avgY), rule, 1.0f);
            char top[32];
            char average[32];
            std::snprintf(top, sizeof(top), "%.0f ms", ceiling);
            std::snprintf(average, sizeof(average), "%.1f avg", frames.averageMs);
            const float labelY = std::clamp(avgY - size - ui::px(2.0f), plotMin.y + size + ui::px(6.0f), plotMax.y - 2.0f * size - ui::px(6.0f));
            draw->AddText(fonts.body, size, ImVec2(plotMin.x + inset, plotMin.y + ui::px(4.0f)), legend, top);
            draw->AddText(fonts.body, size, ImVec2(plotMin.x + inset, labelY), legend, average);
            const ImVec2 windowSize = fonts.body->CalcTextSizeA(size, FLT_MAX, 0.0f, "last 10 s");
            draw->AddText(fonts.body, size, ImVec2(plotMax.x - inset - windowSize.x, plotMax.y - ui::px(4.0f) - windowSize.y), legend, "last 10 s");
        }

        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
        ui::sectionLabel("Swapchain");
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        ui::beginPanel();
        {
            const auto fact = [&](const char* name, const std::string& value, const char* tooltip) {
                ui::factRow(name, value.c_str());
                if (tooltip != nullptr)
                    ImGui::SetItemTooltip("%s", tooltip);
            };
            const FrameGenerationPlace place     = pLogicalDevice->frameGenerationPlace;
            const bool                 lsfgFirst = effectsRunAfterFrameGeneration(place);
            fact("Image", std::to_string(currentWidth) + "x" + std::to_string(currentHeight) + ", " + formatName(currentFormat) + ", " + colourSpaceName(swapchainColorSpace),
                 nullptr);
            fact("Present mode", presentModeName(swapchainPresentMode) + (lsfgFirst ? ", from lsfg-vk" : ", the game's"),
                 lsfgFirst ? "What lsfg-vk asked for: it runs before vkDaedalus"
                 : frameGenerationLayerLoaded() ? "What the game asked for; lsfg-vk, after the effects, may change it"
                                                : "What the game asked for");
            fact("Images", std::to_string(currentImageCount) + ", the game asked for " + std::to_string(swapchainMinImages),
                 "The images the swapchain holds against the game's minImageCount; a layer below can add more");
            ScaleInputs outputs;
            outputs.waylandOutputs  = desktopOutputs;
            outputs.swapchainWidth  = currentWidth;
            outputs.swapchainHeight = currentHeight;
            if (const std::optional<double> refresh = gameOutputRefreshHz(outputs))
            {
                char hz[32];
                std::snprintf(hz, sizeof(hz), "%.2f Hz", *refresh);
                fact("Refresh", hz, "The current mode of the display this swapchain fills, as the compositor reports it");
            }
            fact("Order", place == FrameGenerationPlace::NotLoaded ? "lsfg-vk not loaded" : lsfgFirst ? "lsfg-vk, then effects" : "effects, then lsfg-vk",
                 "Read from the Vulkan layers below vkDaedalus in this game, not from the profile");
        }
        ui::endPanel();

        ui::nextColumn();
        ImGui::AlignTextToFramePadding();
        ui::sectionLabel("Added per frame");
        ImGui::SetItemTooltip("Input to screen is not covered: it needs a camera or a light sensor on the display.");
        bool effectGpuTiming = settingsManager.getEffectGpuTiming();
        ui::alignRight(ui::px(ui::Size::box) + ui::px(ui::Size::gap) + ImGui::CalcTextSize("Per-effect GPU timing").x);
        if (ui::checkbox("Per-effect GPU timing", &effectGpuTiming))
        {
            settingsManager.setEffectGpuTiming(effectGpuTiming);
            saveOverlaySettings();
            applyRequested = true;
        }
        ImGui::SetItemTooltip("Saved for every game. Off: effects are not timed; a chain that crosses to another GPU still times its trip there and back.\n"
                              "Changing it rebuilds the active effects.");
        const EffectsSpanState span =
            effectsSpanState(latency.effectsOn, latency.effectTimingSetting, latency.effectTimingOn, latency.effectsSpanMilliseconds.has_value());
        const bool   effectsMeasured = span == EffectsSpanState::Measured;
        const bool   tripMeasured    = latency.handsOff && latency.roundTripMilliseconds.has_value();
        const double effectsMs       = effectsMeasured ? *latency.effectsSpanMilliseconds : 0.0;
        const double tripMs          = tripMeasured ? *latency.roundTripMilliseconds : 0.0;
        if (effectsMeasured || tripMeasured)
            ui::splitBar({{effectsMs, palette.accent}, {tripMs, palette.info}});
        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
        ui::beginPanel();
        const char* effectsHow = span == EffectsSpanState::EffectsOff     ? "The effects are off."
                                 : span == EffectsSpanState::TimingOff    ? "Turn on Per-effect GPU timing to measure them."
                                 : span == EffectsSpanState::NothingTimed ? "Per-effect GPU timing is on, but nothing is timed here:\nno effect runs on GPU 1, or its queue gives no timestamps."
                                 : span == EffectsSpanState::Waiting      ? "The first timing has not come back from the GPU yet."
                                                                          : "From the first effect's start to the last effect's end:\ntwo Vulkan timestamps in one command buffer.";
        ui::factRow("Effects on GPU 1", effectsMeasured ? milliseconds(effectsMs).c_str() : effectsSpanWord(span), effectsMeasured ? ui::Mark::Measured : ui::Mark::Absent,
                    &palette.accent);
        ImGui::SetItemTooltip("%s", effectsHow);
        if (latency.handsOff)
        {
            ui::factRow(tripLabel(latency.tripGpus).c_str(), tripMeasured ? milliseconds(tripMs).c_str() : "waiting", tripMeasured ? ui::Mark::Measured : ui::Mark::Absent,
                        &palette.info);
            ImGui::SetItemTooltip(tripMeasured ? "From GPU 1 finishing its copy out to it starting the copy back:\ntwo timestamps on one queue of GPU 1.\nThe GPUs card splits it into its parts."
                                               : "The first timing has not come back from the GPU yet.");
        }
        ui::factRow("Total", effectsMeasured || tripMeasured ? milliseconds(effectsMs + tripMs).c_str() : "not measured", effectsMeasured || tripMeasured ? ui::Mark::Sum : ui::Mark::Absent);
        ImGui::SetItemTooltip(!latency.handsOff ? "The measured parts added up; nothing estimated is in it."
                              : latency.rowsAfterReturn
                                  ? "The measured parts added up; nothing estimated is in it.\nGPU 1's copies out and back and the effects after the return are not measured."
                                  : "The measured parts added up; nothing estimated is in it.\nGPU 1's copies out and back are not measured.");
        ui::endPanel();
        if (frames.count > 0 && multiplier)
        {
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap)));
            ui::beginPanel();
            ui::factRow("lsfg-vk holds one frame", milliseconds(frameGenerationHoldMs(frames.averageMs, *multiplier, after)).c_str(), ui::Mark::Estimated);
            ImGui::SetItemTooltip(after ? "About one real frame: the average times the multiplier.\nA wait, not GPU work, so it is in no total."
                                        : "About one real frame: the average frame.\nA wait, not GPU work, so it is in no total.");
            ui::endPanel();
        }

        if (frames.count > 0)
        {
            char window[160];
            std::snprintf(window, sizeof(window), "Over the last %.1f s (%zu frame%s). Slowest 1%%: %s.\nLATENCY.md has every formula.", frames.windowMs / 1000.0, frames.count,
                          frames.count == 1 ? "" : "s", frames.slowestCount == 1 ? "the longest frame" : ("the mean of the " + std::to_string(frames.slowestCount) + " longest").c_str());
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
            ui::sectionLabel("Frame times, last 10 s");
            ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::gap) * 0.5f));
            ui::beginPanel();
            ui::factRow("Shortest", milliseconds(frames.minMs).c_str());
            ImGui::SetItemTooltip("%s", window);
            ui::factRow("Longest", milliseconds(frames.maxMs).c_str());
            ImGui::SetItemTooltip("%s", window);
            ui::factRow("Slowest 1%", milliseconds(frames.slowestAverageMs).c_str());
            ImGui::SetItemTooltip("%s", window);
            ui::endPanel();
        }
        ImGui::EndTable();
        ui::endScrollArea();

        ui::footer();
        if (multiplier)
        {
            ImGui::AlignTextToFramePadding();
            ui::rightAlign(ImGui::CalcTextSize("~ estimate").x);
            ImGui::TextColored(palette.warn, "~");
            ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x);
            ImGui::TextColored(palette.muted, "estimate");
            ImGui::SetItemTooltip("A figure after ~ is estimated, not measured.");
        }
    }
}
