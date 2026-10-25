#include "imgui_overlay.hpp"

#include "frame_generation_place.hpp"
#include "latency_stats.hpp"
#include "logger.hpp"
#include "logical_device.hpp"
#include "overlay/frame_layout.hpp"
#include "overlay/hud.hpp"
#include "overlay/widgets.hpp"

#include "imgui.h"

namespace vkBasalt
{
    bool ImGuiOverlay::hudShowing() const
    {
        return settingsManager.getHud() && hudItemsFrom(settingsManager.getHudItems()).has_value();
    }

    void ImGuiOverlay::renderHud()
    {
        ImGuiIO&          io     = ImGui::GetIO();
        const FrameLayout layout = frameLayout(io.DisplaySize.x, io.DisplaySize.y, desktopScale().scale);
        const float       s      = layout.scale;
        applyFrameScale(s);
        ui::beginFrame(fonts);
        const OverlayPalette&         palette    = activePalette();
        const std::vector<HudItem>    items      = *hudItemsFrom(settingsManager.getHudItems());
        const std::optional<uint32_t> multiplier = std::find(items.begin(), items.end(), HudItem::Fps) != items.end() ? frameGenerationMultiplierHere() : std::nullopt;
        const bool                    after      = multiplier && effectsRunAfterFrameGeneration(pLogicalDevice->frameGenerationPlace);
        const std::optional<double>   effects    = latency.effectTimingOn && latency.effectsOn && latency.effectsSpanMilliseconds
                                                       ? std::optional<double>(*latency.effectsSpanMilliseconds)
                                                       : std::nullopt;
        const std::vector<std::string> lines =
            hudLines(items, frameStats(latency.lastSecondMs), frameStats(latency.lastTenSecondsMs), multiplier, after, effects);
        if (lines.empty())
            return;
        if (!hudLogged)
        {
            hudLogged = true;
            Logger::info("HUD shown while the overlay is closed: " + std::to_string(lines.size()) + (lines.size() == 1 ? " line (" : " lines (") +
                         settingsManager.getHudItems() + ")");
        }
        const float margin = ui::Size::groupGap * s;
        ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, palette.panel);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::Size::gap * s, ui::Size::gap * 0.5f * s));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::Size::surface * s);
        ImGui::Begin("vkDaedalus HUD", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                         | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        for (size_t i = 0; i < lines.size(); i++)
            ImGui::TextColored(i == 0 ? palette.bright : palette.secondary, "%s", lines[i].c_str());
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
}
