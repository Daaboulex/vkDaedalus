#ifndef DESKTOP_SCALE_HPP_INCLUDED
#define DESKTOP_SCALE_HPP_INCLUDED

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vkBasalt
{
    enum class ScaleSource
    {
        Chosen,
        WaylandOutput,
        XftDpi,
        Default
    };

    struct WaylandOutputFacts
    {
        int32_t modeWidth     = 0;
        int32_t modeHeight    = 0;
        int32_t logicalWidth  = 0;
        int32_t logicalHeight = 0;
        int32_t integerScale  = 0;
        int32_t refreshMilliHz = 0;
    };

    struct ScaleInputs
    {
        std::optional<float>            chosen;
        std::vector<WaylandOutputFacts> waylandOutputs;
        std::optional<float>            xftDpi;
        uint32_t                        swapchainWidth  = 0;
        uint32_t                        swapchainHeight = 0;
    };

    struct DesktopScale
    {
        float       scale  = 1.0f;
        ScaleSource source = ScaleSource::Default;
    };

    inline std::optional<float> xftDpiFrom(std::string_view resources)
    {
        while (!resources.empty())
        {
            const size_t           end  = resources.find('\n');
            const std::string_view line = resources.substr(0, end);
            resources                   = end == std::string_view::npos ? std::string_view() : resources.substr(end + 1);
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                continue;
            std::string_view name = line.substr(0, colon);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
                name.remove_suffix(1);
            if (name != "Xft.dpi")
                continue;
            const std::string value(line.substr(colon + 1));
            char*             parsed = nullptr;
            const float       dpi    = std::strtof(value.c_str(), &parsed);
            if (parsed == value.c_str() || !std::isfinite(dpi) || dpi <= 0.0f)
                return std::nullopt;
            return dpi;
        }
        return std::nullopt;
    }

    inline bool plausibleScale(float scale)
    {
        return std::isfinite(scale) && scale >= 0.5f && scale <= 4.0f;
    }

    inline std::optional<float> outputScale(const WaylandOutputFacts& output)
    {
        if (output.modeWidth > 0 && output.logicalWidth > 0)
        {
            const float ratio = static_cast<float>(output.modeWidth) / static_cast<float>(output.logicalWidth);
            if (plausibleScale(ratio))
                return ratio;
        }
        if (plausibleScale(static_cast<float>(output.integerScale)))
            return static_cast<float>(output.integerScale);
        return std::nullopt;
    }

    inline const WaylandOutputFacts* gameOutput(const ScaleInputs& inputs)
    {
        const WaylandOutputFacts* match = nullptr;
        for (const WaylandOutputFacts& output : inputs.waylandOutputs)
        {
            if (output.modeWidth != static_cast<int32_t>(inputs.swapchainWidth) || output.modeHeight != static_cast<int32_t>(inputs.swapchainHeight))
                continue;
            if (match != nullptr)
                return nullptr;
            match = &output;
        }
        if (match == nullptr && inputs.waylandOutputs.size() == 1)
            match = &inputs.waylandOutputs.front();
        return match;
    }

    inline std::optional<float> gameOutputScale(const ScaleInputs& inputs)
    {
        const WaylandOutputFacts* match = gameOutput(inputs);
        return match != nullptr ? outputScale(*match) : std::nullopt;
    }

    inline std::optional<double> gameOutputRefreshHz(const ScaleInputs& inputs)
    {
        const WaylandOutputFacts* match = gameOutput(inputs);
        if (match == nullptr || match->refreshMilliHz <= 0)
            return std::nullopt;
        return match->refreshMilliHz / 1000.0;
    }

    inline DesktopScale resolveDesktopScale(const ScaleInputs& inputs)
    {
        if (inputs.chosen && plausibleScale(*inputs.chosen))
            return {*inputs.chosen, ScaleSource::Chosen};
        if (const std::optional<float> output = gameOutputScale(inputs))
            return {*output, ScaleSource::WaylandOutput};
        if (inputs.xftDpi && plausibleScale(*inputs.xftDpi / 96.0f))
            return {*inputs.xftDpi / 96.0f, ScaleSource::XftDpi};
        return {};
    }
}

#endif
