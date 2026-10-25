#include "memory.hpp"
#include "logger.hpp"

#include <algorithm>
#include <mutex>
#include <map>
#include <unordered_map>
#include <utility>

namespace vkBasalt
{
    uint32_t findMemoryTypeIndex(LogicalDevice* pLogicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
        pLogicalDevice->vki.GetPhysicalDeviceMemoryProperties(pLogicalDevice->physicalDevice, &physicalDeviceMemoryProperties);
        for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        Logger::err("Found no correct memory type");
        return 0x70AD;
    }

    std::optional<uint32_t> findMemoryTypeAvoiding(LogicalDevice*        pLogicalDevice,
                                                   uint32_t              typeFilter,
                                                   VkMemoryPropertyFlags wanted,
                                                   VkMemoryPropertyFlags avoided)
    {
        VkPhysicalDeviceMemoryProperties memory;
        pLogicalDevice->vki.GetPhysicalDeviceMemoryProperties(pLogicalDevice->physicalDevice, &memory);
        for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
        {
            const VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
            if ((typeFilter & (1u << i)) && (flags & wanted) == wanted && !(flags & avoided))
                return i;
        }
        return std::nullopt;
    }
}

namespace vkBasalt
{
    namespace
    {
        std::mutex                                       g_memoryMutex;
        std::map<std::pair<VkDevice, VkDeviceMemory>, VkDeviceSize> g_memorySizes;
        VkDeviceSize                                     g_memoryBytes     = 0;
        VkDeviceSize                                     g_memoryPeakBytes = 0;
        VkDeviceSize                                     g_memorySoftLimit = 0;
        bool                                             g_softLimitWarned = false;
    }

    VkResult allocateTrackedMemory(LogicalDevice*               pLogicalDevice,
                                   const VkMemoryAllocateInfo*  pAllocateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkDeviceMemory*              pMemory)
    {
        const VkResult result = pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, pAllocateInfo, pAllocator, pMemory);
        if (result != VK_SUCCESS)
            return result;

        VkDeviceSize total    = 0;
        bool         announce = false;
        {
            std::lock_guard<std::mutex> lock(g_memoryMutex);
            g_memorySizes[{pLogicalDevice->device, *pMemory}] = pAllocateInfo->allocationSize;
            g_memoryBytes += pAllocateInfo->allocationSize;
            g_memoryPeakBytes = std::max(g_memoryPeakBytes, g_memoryBytes);
            total             = g_memoryBytes;

            if (g_memorySoftLimit != 0 && g_memoryBytes > g_memorySoftLimit && !g_softLimitWarned)
            {
                g_softLimitWarned = true;
                announce          = true;
            }
        }

        if (announce)
            Logger::warn("effects are holding " + std::to_string(total / (1024 * 1024)) + " MiB of video memory, above the "
                         + std::to_string(g_memorySoftLimit / (1024 * 1024)) + " MiB asked for");

        return VK_SUCCESS;
    }

    void freeTrackedMemory(LogicalDevice* pLogicalDevice, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
    {
        if (memory != VK_NULL_HANDLE)
        {
            std::lock_guard<std::mutex> lock(g_memoryMutex);
            const auto                  it = g_memorySizes.find({pLogicalDevice->device, memory});
            if (it != g_memorySizes.end())
            {
                g_memoryBytes -= std::min(g_memoryBytes, it->second);
                g_memorySizes.erase(it);
            }
        }

        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, memory, pAllocator);
    }

    VkDeviceSize trackedMemoryBytes()
    {
        std::lock_guard<std::mutex> lock(g_memoryMutex);
        return g_memoryBytes;
    }

    VkDeviceSize trackedMemoryPeakBytes()
    {
        std::lock_guard<std::mutex> lock(g_memoryMutex);
        return g_memoryPeakBytes;
    }

    void setMemorySoftLimitBytes(VkDeviceSize bytes)
    {
        std::lock_guard<std::mutex> lock(g_memoryMutex);
        g_memorySoftLimit = bytes;
        g_softLimitWarned = false;
    }
}
