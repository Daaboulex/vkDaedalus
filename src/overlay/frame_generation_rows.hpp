#ifndef FRAME_GENERATION_ROWS_HPP_INCLUDED
#define FRAME_GENERATION_ROWS_HPP_INCLUDED

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frame_generation.hpp"
#include "overlay/frame_draw.hpp"
#include "overlay/ui_map.hpp"
#include "overlay/widgets.hpp"

namespace vkBasalt
{
    inline std::optional<std::filesystem::file_time_type> writeTimeOf(const std::string& path)
    {
        std::error_code unreadable;
        const auto      written = std::filesystem::last_write_time(path, unreadable);
        if (unreadable)
            return std::nullopt;
        return written;
    }

    inline constexpr std::string_view makoNeedsVkdaedalusRun =
        "MAKO loads only in a game started with vkdaedalus-run, which sets ENABLE_MAKO=1 for it. Launch option: vkdaedalus-run %command%";

    inline std::string joined(const std::vector<std::string>& names)
    {
        std::string out;
        for (const std::string& name : names)
            out += (out.empty() ? "" : ", ") + name;
        return out;
    }

    inline std::string factorText(float factor)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%.2f", factor);
        std::string shown = text;
        while (shown.back() == '0')
            shown.pop_back();
        if (shown.back() == '.')
            shown.pop_back();
        return shown + "x";
    }

    template<size_t N>
    std::string shownChoice(const std::array<FrameGenerationChoice, N>& choices, const std::string& name)
    {
        const auto at = std::find_if(choices.begin(), choices.end(), [&](const FrameGenerationChoice& choice) { return choice.name == name; });
        return at == choices.end() ? name : std::string(at->shown);
    }

    inline void whyGreyed(const std::string& why)
    {
        if (why.empty())
            return;
        ImGui::PushFont(nullptr, FontSize::caption);
        ImGui::PushStyleColor(ImGuiCol_Text, activePalette().warn);
        ImGui::TextWrapped("%s", why.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    template<size_t N>
    bool pickFrom(const char* label, const std::array<FrameGenerationChoice, N>& choices, std::string& value, float column,
                  const std::function<std::string(std::string_view)>& greyedWhy)
    {
        ui::label(label);
        ui::atColumn(column);
        const float          gap  = ui::px(ui::Size::gap);
        float                need = gap * static_cast<float>(N - 1);
        std::array<float, N> widths{};
        for (size_t i = 0; i < N; i++)
        {
            widths[i] = widthOnGrid(ui::buttonWidth(std::string(choices[i].shown).c_str(), ui::Kind::Quiet), gap);
            need += widths[i];
        }
        const float room   = ImGui::GetContentRegionAvail().x;
        bool        chosen = false;
        ImGui::PushID(label);
        if (need <= room)
            for (size_t i = 0; i < N; i++)
            {
                const std::string name(choices[i].name);
                const std::string why  = greyedWhy ? greyedWhy(name) : "";
                const std::string item = std::string(choices[i].shown) + "##" + label;
                if (i > 0)
                    ImGui::SameLine(0.0f, gap);
                ImGui::BeginDisabled(!why.empty());
                UiMap::nameNextItem(item.c_str());
                if (ui::tab(item.c_str(), value == name, widths[i]) && value != name)
                {
                    value  = name;
                    chosen = true;
                }
                ImGui::EndDisabled();
                if (!why.empty())
                    ImGui::SetItemTooltip("%s", why.c_str());
            }
        else
        {
            const std::string combo = std::string("##") + label;
            if (ui::beginCombo(combo.c_str(), shownChoice(choices, value).c_str(), ImGui::GetContentRegionAvail().x))
            {
                for (const FrameGenerationChoice& choice : choices)
                {
                    const std::string name(choice.name);
                    const std::string why  = greyedWhy ? greyedWhy(name) : "";
                    const std::string item = std::string(choice.shown) + "##" + label;
                    if (ui::option(item.c_str(), value == name, why.empty()) && value != name)
                    {
                        value  = name;
                        chosen = true;
                    }
                    if (!why.empty())
                        ImGui::SetItemTooltip("%s", why.c_str());
                }
                ui::endCombo();
            }
        }
        ImGui::PopID();
        return chosen;
    }
}

#endif
