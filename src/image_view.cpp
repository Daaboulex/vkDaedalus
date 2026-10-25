#include "image_view.hpp"

namespace vkBasalt
{
    std::vector<VkImageView> createImageViews(LogicalDevice*       pLogicalDevice,
                                              VkFormat             format,
                                              std::vector<VkImage> images,
                                              VkImageViewType      viewType,
                                              VkImageAspectFlags   aspectMask,
                                              uint32_t             mipLevels,
                                              VkImageUsageFlags    usage)
    {
        std::vector<VkImageView> imageViews(images.size());

        VkImageViewCreateInfo imageViewCreateInfo;

        imageViewCreateInfo.sType        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        const VkImageViewUsageCreateInfo usageCreateInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO, nullptr, usage};
        imageViewCreateInfo.pNext        = usage != 0 ? &usageCreateInfo : nullptr;
        imageViewCreateInfo.flags        = 0;
        imageViewCreateInfo.image        = VK_NULL_HANDLE;
        imageViewCreateInfo.viewType     = viewType;
        imageViewCreateInfo.format       = format;
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        imageViewCreateInfo.subresourceRange.aspectMask     = aspectMask;
        imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
        imageViewCreateInfo.subresourceRange.levelCount     = mipLevels;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount     = 1;

        for (uint32_t i = 0; i < images.size(); i++)
        {
            imageViewCreateInfo.image = images[i];
            VkResult result           = pLogicalDevice->vkd.CreateImageView(pLogicalDevice->device, &imageViewCreateInfo, nullptr, &(imageViews[i]));
            if (result != VK_SUCCESS)
            {
                for (uint32_t created = 0; created < i; created++)
                    pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, imageViews[created], nullptr);
                ASSERT_VULKAN(result);
            }
        }

        return imageViews;
    }

}
