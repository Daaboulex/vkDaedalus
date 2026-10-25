#include "fake_swapchain.hpp"
#include "memory.hpp"
#include "format.hpp"

namespace vkBasalt
{
    std::vector<VkImage> createFakeSwapchainImages(LogicalDevice*           pLogicalDevice,
                                                   VkSwapchainCreateInfoKHR swapchainCreateInfo,
                                                   uint32_t                 count,
                                                   VkDeviceMemory&          deviceMemory)
    {
        std::vector<VkImage> fakeImages(count);

        const VkFormat formats[] = {convertToUNORM(swapchainCreateInfo.imageFormat), convertToSRGB(swapchainCreateInfo.imageFormat)};
        const VkImageFormatListCreateInfo formatList = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, nullptr, 2, formats};
        const bool                        mutableFormat = formats[0] != formats[1];

        VkImageCreateInfo imageCreateInfo;
        imageCreateInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.pNext         = mutableFormat ? &formatList : nullptr;
        imageCreateInfo.flags         = mutableFormat ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
        imageCreateInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format        = swapchainCreateInfo.imageFormat;
        imageCreateInfo.extent.width  = swapchainCreateInfo.imageExtent.width;
        imageCreateInfo.extent.height = swapchainCreateInfo.imageExtent.height;
        imageCreateInfo.extent.depth  = 1;
        imageCreateInfo.mipLevels     = 1;
        imageCreateInfo.arrayLayers   = swapchainCreateInfo.imageArrayLayers;
        imageCreateInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage         = swapchainCreateInfo.imageUsage | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageCreateInfo.sharingMode           = swapchainCreateInfo.imageSharingMode;
        imageCreateInfo.queueFamilyIndexCount = swapchainCreateInfo.queueFamilyIndexCount;
        imageCreateInfo.pQueueFamilyIndices   = swapchainCreateInfo.pQueueFamilyIndices;
        imageCreateInfo.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

        auto abandon = [&](const std::string& why) {
            Logger::err("fake swapchain images: " + why + " -- effects disabled for this swapchain, passing frames through");
            if (deviceMemory != VK_NULL_HANDLE)
            {
                freeTrackedMemory(pLogicalDevice, deviceMemory, nullptr);
                deviceMemory = VK_NULL_HANDLE;
            }
            for (VkImage image : fakeImages)
            {
                if (image != VK_NULL_HANDLE)
                    pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
            }
            return std::vector<VkImage>();
        };

        VkResult result;
        for (uint32_t i = 0; i < count; i++)
        {
            result = pLogicalDevice->vkd.CreateImage(pLogicalDevice->device, &imageCreateInfo, nullptr, &(fakeImages[i]));
            if (result != VK_SUCCESS)
                return abandon("image " + std::to_string(i) + " of " + std::to_string(count) + " failed: " + std::to_string(result));
        }

        VkMemoryRequirements memoryRequirements;
        pLogicalDevice->vkd.GetImageMemoryRequirements(pLogicalDevice->device, fakeImages[0], &memoryRequirements);

        Logger::debug("fake image size: " + std::to_string(memoryRequirements.size));
        Logger::debug("fake image alignment: " + std::to_string(memoryRequirements.alignment));

        if (memoryRequirements.size % memoryRequirements.alignment != 0)
        {
            memoryRequirements.size = (memoryRequirements.size / memoryRequirements.alignment + 1) * memoryRequirements.alignment;
        }

        VkMemoryAllocateInfo memoryAllocateInfo;
        memoryAllocateInfo.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext          = nullptr;
        memoryAllocateInfo.allocationSize = memoryRequirements.size * count;
        memoryAllocateInfo.memoryTypeIndex =
            findMemoryTypeIndex(pLogicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        result = allocateTrackedMemory(pLogicalDevice, &memoryAllocateInfo, nullptr, &deviceMemory);
        if (result != VK_SUCCESS)
        {
            deviceMemory = VK_NULL_HANDLE;
            return abandon(std::to_string(memoryAllocateInfo.allocationSize) + " bytes for " + std::to_string(count)
                           + " images failed: " + std::to_string(result));
        }

        for (uint32_t i = 0; i < count; i++)
        {
            result = pLogicalDevice->vkd.BindImageMemory(pLogicalDevice->device, fakeImages[i], deviceMemory, memoryRequirements.size * i);
            if (result != VK_SUCCESS)
                return abandon("binding image " + std::to_string(i) + " failed: " + std::to_string(result));
        }
        return fakeImages;
    }
}
