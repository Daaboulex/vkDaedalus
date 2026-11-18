#ifndef PRESENT_COUNTER_HPP_INCLUDED
#define PRESENT_COUNTER_HPP_INCLUDED

#include "vulkan_include.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace vkBasalt
{
    struct PresentTotals
    {
        uint64_t intervals    = 0;
        double   milliseconds = 0.0;
    };

    std::optional<std::vector<double>> presentsBelowFrameGeneration(VkDevice device, std::chrono::steady_clock::time_point now, std::chrono::milliseconds span);
    std::optional<PresentTotals>       presentTotalsBelowFrameGeneration(VkDevice device);
}

#endif
