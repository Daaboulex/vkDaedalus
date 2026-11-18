#ifndef MAKO_GPU_HPP_INCLUDED
#define MAKO_GPU_HPP_INCLUDED

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gpu_assignment.hpp"

namespace vkBasalt
{
    struct MakoGpu
    {
        std::string               name;
        uint32_t                  vendorId = 0;
        uint32_t                  deviceId = 0;
        std::optional<PciAddress> pci;
    };

    struct MakoGpuChoice
    {
        uint32_t number = 0;
        MakoGpu  gpu;
    };

    struct MakoGpus
    {
        MakoGpu                    game;
        std::vector<MakoGpuChoice> others;
    };

    inline std::string makoGpuIdText(uint32_t vendorId, uint32_t deviceId)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "0x%04X:0x%04X", vendorId & 0xFFFFu, deviceId & 0xFFFFu);
        return text;
    }

    inline bool makoGpuMatches(std::string_view named, const MakoGpu& gpu)
    {
        return named == gpu.name || named == makoGpuIdText(gpu.vendorId, gpu.deviceId)
               || (gpu.pci && named == std::to_string(gpu.pci->bus) + ":" + std::to_string(gpu.pci->device) + "." + std::to_string(gpu.pci->function));
    }

    inline bool makoGpusHaveTwins(const MakoGpus& gpus)
    {
        std::vector<const MakoGpu*> all = {&gpus.game};
        for (const MakoGpuChoice& other : gpus.others)
            all.push_back(&other.gpu);
        for (size_t i = 0; i < all.size(); i++)
            for (size_t j = i + 1; j < all.size(); j++)
                if (all[i]->vendorId == all[j]->vendorId && all[i]->deviceId == all[j]->deviceId)
                    return true;
        return false;
    }
}

#endif
