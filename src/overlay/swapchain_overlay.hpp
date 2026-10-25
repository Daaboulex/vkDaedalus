#ifndef SWAPCHAIN_OVERLAY_HPP_INCLUDED
#define SWAPCHAIN_OVERLAY_HPP_INCLUDED

#include <vector>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    struct LogicalDevice;

    struct OverlayBuffer
    {
        VkBuffer       buffer   = VK_NULL_HANDLE;
        VkDeviceMemory memory   = VK_NULL_HANDLE;
        VkDeviceSize   capacity = 0;
        void*          mapped   = nullptr;
    };

    struct OverlayDraw
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence         fence         = VK_NULL_HANDLE;
        VkFramebuffer   framebuffer   = VK_NULL_HANDLE;
        OverlayBuffer   vertices;
        OverlayBuffer   indices;
    };

    struct SwapchainOverlay
    {
        VkCommandPool            commandPool = VK_NULL_HANDLE;
        VkFormat                 format      = VK_FORMAT_UNDEFINED;
        VkExtent2D               extent      = {0, 0};
        std::vector<OverlayDraw> draws;

        std::vector<VkFence> fences() const;
        bool                 restoreFence(LogicalDevice& device, uint32_t imageIndex);
        void                 destroy(LogicalDevice& device);
    };

    void destroyOverlayBuffer(LogicalDevice& device, OverlayBuffer& buffer);
}

#endif
