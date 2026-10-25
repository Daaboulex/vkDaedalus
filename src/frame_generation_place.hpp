#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace vkBasalt
{
    enum class FrameGenerationPlace
    {
        NotLoaded,
        BelowTheLayer,
        AboveTheLayer,
    };

    inline bool isLsfgLayerLibrary(const std::string& path)
    {
        const size_t      slash = path.rfind('/');
        const std::string name  = slash == std::string::npos ? path : path.substr(slash + 1);
        return name.rfind("liblsfg-vk-layer", 0) == 0;
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

    inline FrameGenerationPlace frameGenerationPlace(const std::vector<std::string>& librariesBelow, bool lsfgLoaded)
    {
        for (const std::string& library : librariesBelow)
            if (isLsfgLayerLibrary(library))
                return FrameGenerationPlace::BelowTheLayer;
        return lsfgLoaded ? FrameGenerationPlace::AboveTheLayer : FrameGenerationPlace::NotLoaded;
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

    inline const char* frameGenerationPlaceWords(FrameGenerationPlace place)
    {
        switch (place)
        {
            case FrameGenerationPlace::NotLoaded: return "lsfg-vk is not loaded";
            case FrameGenerationPlace::BelowTheLayer: return "lsfg-vk runs below the layer: effects before frame generation";
            case FrameGenerationPlace::AboveTheLayer: return "lsfg-vk runs above the layer: effects after frame generation";
        }
        return "lsfg-vk's place is unknown";
    }
}
