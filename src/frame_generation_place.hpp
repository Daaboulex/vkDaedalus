#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vkBasalt
{
    enum class FrameGenerationPlace
    {
        NotLoaded,
        BelowTheLayer,
        AboveTheLayer,
    };

    enum class FrameGenerationLayer
    {
        LsfgVk,
        Mako,
    };

    inline std::string_view frameGenerationLayerName(FrameGenerationLayer layer)
    {
        return layer == FrameGenerationLayer::Mako ? "mako" : "lsfg-vk";
    }

    inline std::optional<FrameGenerationLayer> frameGenerationLayerNamed(std::string_view name)
    {
        if (name == frameGenerationLayerName(FrameGenerationLayer::LsfgVk))
            return FrameGenerationLayer::LsfgVk;
        if (name == frameGenerationLayerName(FrameGenerationLayer::Mako))
            return FrameGenerationLayer::Mako;
        return std::nullopt;
    }

    inline FrameGenerationLayer defaultFrameGenerationLayer(bool lsfgVkInstalled, bool makoInstalled, bool makoEnabledByCaller)
    {
        return makoInstalled && (makoEnabledByCaller || !lsfgVkInstalled) ? FrameGenerationLayer::Mako : FrameGenerationLayer::LsfgVk;
    }

    inline std::optional<FrameGenerationLayer> loadedFrameGenerationLayerOf(bool lsfgVkMapped, bool makoMapped)
    {
        if (makoMapped)
            return FrameGenerationLayer::Mako;
        return lsfgVkMapped ? std::optional<FrameGenerationLayer>(FrameGenerationLayer::LsfgVk) : std::nullopt;
    }

    inline bool layerChangeWaits(std::optional<FrameGenerationLayer> loaded, FrameGenerationLayer profile)
    {
        return loaded && *loaded != profile;
    }

    inline bool loadedAtNextLaunch(FrameGenerationLayer layer, bool startedWithVkdaedalusRun, bool makoEnabled)
    {
        return layer == FrameGenerationLayer::LsfgVk || startedWithVkdaedalusRun || makoEnabled;
    }

    inline constexpr std::string_view lsfgLayerLibrary = "liblsfg-vk-layer";
    inline constexpr std::string_view makoLayerLibrary = "libmako-render.so";

    inline bool isLayerLibrary(const std::string& path, std::string_view library)
    {
        const size_t      slash = path.rfind('/');
        const std::string name  = slash == std::string::npos ? path : path.substr(slash + 1);
        return name.starts_with(library);
    }

    inline std::vector<std::string> layerLibrariesBelow(const std::vector<std::string>& librariesBelow)
    {
        std::vector<std::string> names;
        for (const std::string& library : librariesBelow)
        {
            const size_t      slash = library.rfind('/');
            const std::string name  = slash == std::string::npos ? library : library.substr(slash + 1);
            if (name.rfind("libvulkan.so", 0) != 0 && (names.empty() || names.back() != name))
                names.push_back(name);
        }
        return names;
    }

    inline FrameGenerationPlace frameGenerationPlace(const std::vector<std::string>& librariesBelow, bool loaded, std::string_view layerLibrary)
    {
        for (const std::string& library : librariesBelow)
            if (isLayerLibrary(library, layerLibrary))
                return FrameGenerationPlace::BelowTheLayer;
        return loaded ? FrameGenerationPlace::AboveTheLayer : FrameGenerationPlace::NotLoaded;
    }

    inline bool effectsRunAfterFrameGeneration(FrameGenerationPlace place)
    {
        return place == FrameGenerationPlace::AboveTheLayer;
    }

    struct PipelineStep
    {
        std::string           what;
        std::vector<uint32_t> gpus;

        bool operator==(const PipelineStep&) const = default;
    };

    inline std::vector<PipelineStep> pipelineSteps(FrameGenerationPlace place, bool generating, const std::vector<uint32_t>& effectGpus)
    {
        constexpr uint32_t    gameGpu = 1;
        std::vector<uint32_t> used;
        for (const uint32_t gpu : effectGpus)
            if (std::find(used.begin(), used.end(), gpu) == used.end())
                used.push_back(gpu);
        std::vector<PipelineStep> steps{{"Game", {gameGpu}}};
        if (generating && place == FrameGenerationPlace::AboveTheLayer)
            steps.push_back({"Frame generation", {gameGpu}});
        if (!used.empty())
            steps.push_back({"Effects", used});
        if (generating && place == FrameGenerationPlace::BelowTheLayer)
            steps.push_back({"Frame generation", {gameGpu}});
        steps.push_back({"Display", {gameGpu}});
        return steps;
    }

    inline std::optional<std::string> orderMismatchWords(FrameGenerationPlace place, bool profileWantsAfter, std::string_view layer)
    {
        const bool after = place == FrameGenerationPlace::AboveTheLayer;
        if (place == FrameGenerationPlace::NotLoaded || after == profileWantsAfter)
            return std::nullopt;
        return "the profile asks for effects " + std::string(profileWantsAfter ? "after" : "before") + " frame generation, but " + std::string(layer) + " runs "
               + (after ? "above" : "below") + " the layer, so the effects run " + (after ? "after" : "before")
               + " it: the order layers of vkdaedalus-run did not reach the game's Vulkan loader";
    }

    inline std::string frameGenerationPlaceWords(FrameGenerationPlace place, std::string_view layer)
    {
        switch (place)
        {
            case FrameGenerationPlace::NotLoaded: return std::string(layer) + " is not loaded";
            case FrameGenerationPlace::BelowTheLayer: return std::string(layer) + " runs below the layer: effects before frame generation";
            case FrameGenerationPlace::AboveTheLayer: return std::string(layer) + " runs above the layer: effects after frame generation";
        }
        return std::string(layer) + "'s place is unknown";
    }
}
