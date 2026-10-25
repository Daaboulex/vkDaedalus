#ifndef LOGICAL_SWAPCHAIN_HPP_INCLUDED
#define LOGICAL_SWAPCHAIN_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>
#include <optional>

#include "effects/effect.hpp"

#include "vulkan_include.hpp"

#include "logical_device.hpp"
#include "overlay/swapchain_overlay.hpp"
#include "gpu_chain.hpp"
#include "latency_stats.hpp"

namespace vkBasalt
{
    class Config;

    struct LogicalSwapchain
    {
        LogicalDevice*                       pLogicalDevice;
        VkSwapchainCreateInfoKHR             swapchainCreateInfo;
        VkImageUsageFlags                    appImageUsage = 0;
        std::vector<uint32_t>                queueFamilyIndices;
        VkExtent2D                           imageExtent;
        VkFormat                             format;
        uint32_t                             imageCount;
        std::vector<VkImage>                 images;
        std::vector<VkImageView>             overlayImageViews;
        std::vector<VkImage>                 fakeImages;
        size_t                               maxEffectSlots = 0;
        std::vector<VkCommandBuffer>         commandBuffersEffect;
        VkImage                              effectDepthImage = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer>         commandBuffersNoEffect;
        std::vector<VkSemaphore>             semaphores;
        std::vector<VkSemaphore>             overlaySemaphores;
        SwapchainOverlay                     overlay;
        std::vector<std::shared_ptr<Effect>> effects;
        std::shared_ptr<Effect>              defaultTransfer;
        std::unique_ptr<GpuChain>            chain;
        std::vector<VkDeviceMemory>          fakeImageMemory;
        std::vector<VkFence>                 effectFences;
        std::vector<VkQueue>                 fencelessEffectQueues;
        VkQueryPool                         timingQueryPool = VK_NULL_HANDLE;
        uint32_t                            timingQueryStride = 0;
        std::vector<std::string>            timingEffectNames;
        std::vector<float>                  timingMilliseconds;
        std::vector<bool>                   timingValid;
        std::optional<float>                timingSpanMilliseconds;
        PresentIntervals                    presentIntervals;
        uint64_t                            ratesReportedIntervals = 0;
        double                              ratesReportedMs        = 0.0;
        std::vector<bool>                   timingSamplesPending;
        bool                                timingEnabled = false;
        bool                                timingReadErrorLogged = false;

        void keepCreateInfo(const VkSwapchainCreateInfoKHR& createInfo)
        {
            swapchainCreateInfo       = createInfo;
            swapchainCreateInfo.pNext = nullptr;
            if (createInfo.imageSharingMode == VK_SHARING_MODE_CONCURRENT)
                queueFamilyIndices.assign(createInfo.pQueueFamilyIndices, createInfo.pQueueFamilyIndices + createInfo.queueFamilyIndexCount);
            else
                queueFamilyIndices.clear();
            swapchainCreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
            swapchainCreateInfo.pQueueFamilyIndices   = queueFamilyIndices.empty() ? nullptr : queueFamilyIndices.data();
        }

        void destroy();
        void     waitForEffects();
        void freeCommandBuffers(std::vector<VkCommandBuffer>& commandBuffers);
        bool initializeEffectTimings(const std::vector<std::string>& effectNames);
        void destroyEffectTimings();
        void collectEffectTimings(uint32_t imageIndex);
        void markTimingSubmission(uint32_t imageIndex, bool effectCommandsSubmitted);
    };
}

#endif
