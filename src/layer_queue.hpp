#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    struct LayerQueueSlot
    {
        uint32_t family;
        uint32_t index;
    };

    struct LayerQueueRequest
    {
        std::vector<VkDeviceQueueCreateInfo> infos;
        std::vector<float>                   priorities;
        std::optional<LayerQueueSlot>        own;
    };

    inline LayerQueueRequest requestLayerQueue(const VkDeviceQueueCreateInfo* infos, uint32_t count,
                                               const std::vector<VkQueueFamilyProperties>& families)
    {
        LayerQueueRequest request;
        request.infos.assign(infos, infos + count);
        for (VkDeviceQueueCreateInfo& info : request.infos)
        {
            if (info.queueFamilyIndex >= families.size() || !(families[info.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                continue;
            if (info.flags != 0 || info.queueCount == 0 || info.queueCount >= families[info.queueFamilyIndex].queueCount
                || info.pQueuePriorities == nullptr)
                break;
            request.priorities.assign(info.pQueuePriorities, info.pQueuePriorities + info.queueCount);
            request.priorities.push_back(*std::max_element(request.priorities.begin(), request.priorities.end()));
            request.own            = LayerQueueSlot{info.queueFamilyIndex, info.queueCount};
            info.queueCount        = info.queueCount + 1;
            info.pQueuePriorities  = request.priorities.data();
            break;
        }
        return request;
    }
}
