#ifndef VULKAN_INCLUDE_HPP_INCLUDED
#define VULKAN_INCLUDE_HPP_INCLUDED

#define VK_NO_PROTOTYPES

#pragma GCC system_header
#include "vulkan/vulkan.h"
#include "vulkan/vk_layer.h"

#include <stdexcept>
#include <string>

#include "logger.hpp"

#define ASSERT_VULKAN(val) ::vkBasalt::throwIfVulkanFailed((val), __FILE__, __LINE__)

namespace vkBasalt
{
    inline void throwIfVulkanFailed(VkResult result, const char* file, int line)
    {
        if (result != VK_SUCCESS)
            throw std::runtime_error("Vulkan call failed in " + std::string(file) + ":" + std::to_string(line) + " with VkResult "
                                     + std::to_string(result));
    }

    template<typename DispatchableType, typename SuperDispatchableType>
    inline void initializeDispatchTable(DispatchableType dispatchableObject, SuperDispatchableType source)
    {
        *reinterpret_cast<void**>(dispatchableObject) = *reinterpret_cast<void**>(source);
    }
}

#endif
