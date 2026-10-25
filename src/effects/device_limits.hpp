#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    inline std::string imageLimitProblem(VkResult supported, const VkImageFormatProperties& properties, VkExtent3D extent, uint32_t levels)
    {
        if (supported != VK_SUCCESS)
            return " cannot be made here (" + std::to_string(supported) + ")";
        if (extent.width > properties.maxExtent.width || extent.height > properties.maxExtent.height || extent.depth > properties.maxExtent.depth)
            return " is larger than this GPU makes, " + std::to_string(properties.maxExtent.width) + "x" + std::to_string(properties.maxExtent.height)
                   + "x" + std::to_string(properties.maxExtent.depth);
        if (levels > properties.maxMipLevels)
            return " has more mip levels than this GPU makes, " + std::to_string(properties.maxMipLevels);
        return "";
    }

    inline std::string dispatchLimitProblem(const VkPhysicalDeviceLimits& limits, const std::array<uint32_t, 3>& dispatch)
    {
        for (int axis = 0; axis < 3; axis++)
        {
            if (dispatch[axis] > limits.maxComputeWorkGroupCount[axis])
                return "dispatches " + std::to_string(dispatch[axis]) + " groups along axis " + std::to_string(axis) + ", and this GPU takes "
                       + std::to_string(limits.maxComputeWorkGroupCount[axis]);
        }
        return "";
    }

    inline std::string threadGroupLimitProblem(const VkPhysicalDeviceLimits& limits, const std::array<uint32_t, 3>& size)
    {
        const uint64_t invocations = uint64_t{size[0]} * size[1] * size[2];
        if (size[0] <= limits.maxComputeWorkGroupSize[0] && size[1] <= limits.maxComputeWorkGroupSize[1]
            && size[2] <= limits.maxComputeWorkGroupSize[2] && invocations <= limits.maxComputeWorkGroupInvocations)
            return "";
        return "runs " + std::to_string(size[0]) + "x" + std::to_string(size[1]) + "x" + std::to_string(size[2])
               + " threads per group, and this GPU takes " + std::to_string(limits.maxComputeWorkGroupSize[0]) + "x"
               + std::to_string(limits.maxComputeWorkGroupSize[1]) + "x" + std::to_string(limits.maxComputeWorkGroupSize[2]) + ", at most "
               + std::to_string(limits.maxComputeWorkGroupInvocations) + " in all";
    }

    inline float heldLodBias(const VkPhysicalDeviceLimits& limits, float lodBias)
    {
        return std::clamp(lodBias, -limits.maxSamplerLodBias, limits.maxSamplerLodBias);
    }
}
