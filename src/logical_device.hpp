#ifndef LOGICAL_DEVICE_HPP_INCLUDED
#define LOGICAL_DEVICE_HPP_INCLUDED
#include <algorithm>
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>
#include <unordered_map>

#include "vulkan_include.hpp"
#include "vkdispatch.hpp"
#include "logger.hpp"
#include "frame_generation_place.hpp"

namespace vkBasalt
{
    struct DeviceOffload;

    class ImGuiOverlay;

    struct LogicalDevice
    {
        DeviceDispatch           vkd;
        InstanceDispatch         vki;
        VkDevice                 device         = VK_NULL_HANDLE;
        VkPhysicalDevice         physicalDevice = VK_NULL_HANDLE;
        VkInstance               instance       = VK_NULL_HANDLE;
        VkQueue                  queue          = VK_NULL_HANDLE;
        VkQueue                  ownQueue       = VK_NULL_HANDLE;
        std::unordered_map<VkQueue, uint32_t> queueFamilies;
        uint32_t                 queueFamilyIndex = 0;
        VkCommandPool            commandPool    = VK_NULL_HANDLE;
        float                    timestampPeriodNanoseconds = 0.0f;
        VkPhysicalDeviceLimits   limits = {};
        std::unordered_map<VkFormat, VkFormatFeatureFlags> optimalFeatures;
        uint32_t                 timestampValidBits = 0;
        bool                     supportsMutableFormat = false;
        bool                     supportsStorageImageWithoutFormat = false;
        bool                     isHelper = false;
        bool                     sharingEnabled = false;
        FrameGenerationPlace     frameGenerationPlace = FrameGenerationPlace::NotLoaded;
        std::vector<std::string> layersBelow;
        std::shared_ptr<DeviceOffload> offload;
        struct DepthImage
        {
            VkImage     image  = VK_NULL_HANDLE;
            VkFormat    format = VK_FORMAT_UNDEFINED;
            VkImageView view   = VK_NULL_HANDLE;
        };
        std::vector<DepthImage>  depthImages;


        std::unique_ptr<ImGuiOverlay> imguiOverlay;
    };

    inline VkQueue backgroundQueue(const LogicalDevice& device)
    {
        return device.ownQueue != VK_NULL_HANDLE ? device.ownQueue : device.queue;
    }

    inline VkQueue submitQueueFor(const LogicalDevice& device, VkQueue presentQueue)
    {
        const auto family = device.queueFamilies.find(presentQueue);
        if (family != device.queueFamilies.end() && family->second == device.queueFamilyIndex)
            return presentQueue;
        return backgroundQueue(device);
    }

    inline VkResult allocateDispatchableCommandBuffers(LogicalDevice*                     pLogicalDevice,
                                                       const VkCommandBufferAllocateInfo& info,
                                                       VkCommandBuffer*                   commandBuffers)
    {
        VkResult result = pLogicalDevice->vkd.AllocateCommandBuffers(pLogicalDevice->device, &info, commandBuffers);
        if (result == VK_SUCCESS)
            for (uint32_t i = 0; i < info.commandBufferCount; i++)
                initializeDispatchTable(commandBuffers[i], pLogicalDevice->device);
        return result;
    }

    inline VkResult createSignalledFence(LogicalDevice* pLogicalDevice, VkFence* pFence)
    {
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags             = VK_FENCE_CREATE_SIGNALED_BIT;
        const VkResult result       = pLogicalDevice->vkd.CreateFence(pLogicalDevice->device, &fenceInfo, nullptr, pFence);
        if (result != VK_SUCCESS)
            *pFence = VK_NULL_HANDLE;
        return result;
    }

    inline VkResult waitForOurFences(PFN_vkWaitForFences waitForFences, VkDevice device, const std::vector<VkFence>& fences, uint64_t patienceNs)
    {
        if (fences.empty())
            return VK_SUCCESS;
        const uint32_t count  = static_cast<uint32_t>(fences.size());
        const VkResult waited = waitForFences(device, count, fences.data(), VK_TRUE, patienceNs);
        if (waited == VK_SUCCESS)
            return waited;
        Logger::err("a fence wait failed: " + std::to_string(waited) + ", waiting again with no deadline");
        const VkResult again = waitForFences(device, count, fences.data(), VK_TRUE, UINT64_MAX);
        if (again != VK_SUCCESS)
            Logger::err("the second fence wait failed: " + std::to_string(again));
        return again;
    }

    inline VkResult drainQueues(PFN_vkQueueWaitIdle queueWaitIdle, std::vector<VkQueue> queues)
    {
        std::sort(queues.begin(), queues.end());
        queues.erase(std::unique(queues.begin(), queues.end()), queues.end());
        VkResult result = VK_SUCCESS;
        for (VkQueue queue : queues)
        {
            const VkResult drained = queueWaitIdle(queue);
            if (drained == VK_SUCCESS)
                continue;
            Logger::err("the queue drain failed: " + std::to_string(drained));
            if (result == VK_SUCCESS)
                result = drained;
        }
        return result;
    }
}

#endif
