#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "latency_stats.hpp"

namespace vkBasalt
{
    enum class HudItem
    {
        Fps,
        FrameTime,
        SlowestOnePercent,
        EffectsTime,
    };

    struct HudItemName
    {
        HudItem          item;
        std::string_view word;
        std::string_view shown;
    };

    inline constexpr std::array<HudItemName, 4> hudItemNames = {{
        {HudItem::Fps, "fps", "FPS"},
        {HudItem::FrameTime, "frametime", "Frame time"},
        {HudItem::SlowestOnePercent, "slowest", "Slowest 1%"},
        {HudItem::EffectsTime, "effects", "Effects time"},
    }};

    struct HudPreset
    {
        std::string_view     shown;
        std::vector<HudItem> items;
    };

    inline const std::vector<HudPreset>& hudPresets()
    {
        static const std::vector<HudPreset> presets = {
            {"FPS only", {HudItem::Fps}},
            {"Basic", {HudItem::Fps, HudItem::FrameTime}},
            {"Detailed", {HudItem::Fps, HudItem::FrameTime, HudItem::SlowestOnePercent, HudItem::EffectsTime}},
        };
        return presets;
    }

    inline std::string hudItemsWord(const std::vector<HudItem>& items)
    {
        std::string out;
        for (const HudItemName& name : hudItemNames)
            if (std::find(items.begin(), items.end(), name.item) != items.end())
                out += (out.empty() ? "" : ",") + std::string(name.word);
        return out;
    }

    inline std::optional<std::vector<HudItem>> hudItemsFrom(std::string_view text)
    {
        std::vector<HudItem> items;
        for (size_t start = 0;;)
        {
            const size_t           comma = text.find(',', start);
            const std::string_view word  = text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
            const auto             named = std::find_if(hudItemNames.begin(), hudItemNames.end(), [&](const HudItemName& name) { return name.word == word; });
            if (named == hudItemNames.end())
                return std::nullopt;
            if (std::find(items.begin(), items.end(), named->item) == items.end())
                items.push_back(named->item);
            if (comma == std::string_view::npos)
                break;
            start = comma + 1;
        }
        std::sort(items.begin(), items.end());
        return items;
    }

    struct HudWindows
    {
        bool lastSecond     = false;
        bool lastTenSeconds = false;
    };

    inline HudWindows hudWindowsFor(const std::vector<HudItem>& items)
    {
        const auto has = [&](HudItem item) { return std::find(items.begin(), items.end(), item) != items.end(); };
        return {has(HudItem::Fps) || has(HudItem::FrameTime), has(HudItem::SlowestOnePercent)};
    }

    inline std::vector<std::string> hudLines(const std::vector<HudItem>& items, const FrameStats& lastSecond, const FrameStats& lastTenSeconds,
                                             std::optional<uint32_t> multiplier, bool effectsAfterFrameGeneration, std::optional<double> effectsMs)
    {
        const auto has = [&](HudItem item) { return std::find(items.begin(), items.end(), item) != items.end(); };
        std::vector<std::string> lines;
        char                     text[64];
        if (has(HudItem::Fps) && lastSecond.count > 0)
        {
            const FrameRates rates = frameRatesOf(lastSecond.fps, multiplier, effectsAfterFrameGeneration);
            std::snprintf(text, sizeof(text), "%.0f FPS", rates.gameFps);
            lines.push_back(text);
            if (multiplier && *multiplier >= 2)
            {
                std::snprintf(text, sizeof(text), "%.0f shown", rates.shownFps);
                lines.push_back(text);
            }
        }
        if (has(HudItem::FrameTime) && lastSecond.count > 0)
        {
            std::snprintf(text, sizeof(text), "%.1f ms", lastSecond.averageMs);
            lines.push_back(text);
        }
        if (has(HudItem::SlowestOnePercent) && lastTenSeconds.count > 0)
        {
            std::snprintf(text, sizeof(text), "1%% %.1f ms", lastTenSeconds.slowestAverageMs);
            lines.push_back(text);
        }
        if (has(HudItem::EffectsTime) && effectsMs)
        {
            std::snprintf(text, sizeof(text), "effects %.2f ms", *effectsMs);
            lines.push_back(text);
        }
        return lines;
    }
}
