#include "buffer.hpp"
#include "memory.hpp"

namespace vkBasalt
{
    void createBuffer(LogicalDevice*        pLogicalDevice,
                      VkDeviceSize          size,
                      VkBufferUsageFlags    usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer&             buffer,
                      VkDeviceMemory&       bufferMemory)
    {
        VkBufferCreateInfo bufferInfo = {};

        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = size;
        bufferInfo.usage       = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        bufferMemory    = VK_NULL_HANDLE;
        VkResult result = pLogicalDevice->vkd.CreateBuffer(pLogicalDevice->device, &bufferInfo, nullptr, &buffer);
        if (result != VK_SUCCESS)
        {
            buffer = VK_NULL_HANDLE;
            ASSERT_VULKAN(result);
        }
        const auto releaseAndThrow = [&](VkResult failed) {
            if (bufferMemory != VK_NULL_HANDLE)
                freeTrackedMemory(pLogicalDevice, bufferMemory, nullptr);
            pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, buffer, nullptr);
            bufferMemory = VK_NULL_HANDLE;
            buffer       = VK_NULL_HANDLE;
            ASSERT_VULKAN(failed);
        };

        VkMemoryRequirements memRequirements;
        pLogicalDevice->vkd.GetBufferMemoryRequirements(pLogicalDevice->device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};

        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryTypeIndex(pLogicalDevice, memRequirements.memoryTypeBits, properties);

        result = allocateTrackedMemory(pLogicalDevice, &allocInfo, nullptr, &bufferMemory);
        if (result != VK_SUCCESS)
        {
            bufferMemory = VK_NULL_HANDLE;
            releaseAndThrow(result);
        }

        result = pLogicalDevice->vkd.BindBufferMemory(pLogicalDevice->device, buffer, bufferMemory, 0);
        if (result != VK_SUCCESS)
            releaseAndThrow(result);
    }

}
