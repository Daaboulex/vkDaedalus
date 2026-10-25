#ifndef MEMORY_HPP_INCLUDED
#define MEMORY_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>
#include <optional>

#include "vulkan_include.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    uint32_t findMemoryTypeIndex(LogicalDevice* pLogicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

    std::optional<uint32_t> findMemoryTypeAvoiding(LogicalDevice*        pLogicalDevice,
                                                   uint32_t              typeFilter,
                                                   VkMemoryPropertyFlags wanted,
                                                   VkMemoryPropertyFlags avoided);

    VkResult allocateTrackedMemory(LogicalDevice*               pLogicalDevice,
                                   const VkMemoryAllocateInfo*  pAllocateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkDeviceMemory*              pMemory);

    void freeTrackedMemory(LogicalDevice*               pLogicalDevice,
                           VkDeviceMemory               memory,
                           const VkAllocationCallbacks* pAllocator);

    VkDeviceSize trackedMemoryBytes();
    VkDeviceSize trackedMemoryPeakBytes();
    void         setMemorySoftLimitBytes(VkDeviceSize bytes);
}

#endif
