#include "memory.hpp"
#include "logical_swapchain.hpp"
#include "effect_timing.hpp"
#include "overlay/imgui_overlay.hpp"

#include <algorithm>

namespace vkBasalt
{
    bool LogicalSwapchain::initializeEffectTimings(const std::vector<std::string>& effectNames)
    {
        destroyEffectTimings();

        const bool allFencesValid = effectFences.size() == imageCount
            && std::all_of(effectFences.begin(), effectFences.end(),
                           [](VkFence fence) { return fence != VK_NULL_HANDLE; });
        if (effectNames.empty() || imageCount == 0 || !allFencesValid
            || pLogicalDevice->timestampValidBits == 0
            || !(pLogicalDevice->timestampPeriodNanoseconds > 0.0f)
            || pLogicalDevice->vkd.CreateQueryPool == nullptr
            || pLogicalDevice->vkd.DestroyQueryPool == nullptr
            || pLogicalDevice->vkd.GetQueryPoolResults == nullptr
            || pLogicalDevice->vkd.CmdResetQueryPool == nullptr
            || pLogicalDevice->vkd.CmdWriteTimestamp == nullptr)
            return false;

        if (!validEffectTimingQueryAllocation(effectNames.size(), imageCount))
        {
            Logger::warn("too many effects/images for timestamp query allocation");
            return false;
        }

        timingQueryStride = static_cast<uint32_t>(effectNames.size() * 2u);
        VkQueryPoolCreateInfo queryInfo = {};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = timingQueryStride * imageCount;

        const VkResult result = pLogicalDevice->vkd.CreateQueryPool(
            pLogicalDevice->device, &queryInfo, nullptr, &timingQueryPool);
        if (result != VK_SUCCESS)
        {
            timingQueryPool = VK_NULL_HANDLE;
            timingQueryStride = 0;
            Logger::warn("could not create optional per-effect timestamp query pool: "
                         + std::to_string(result));
            return false;
        }

        timingEffectNames = effectNames;
        timingMilliseconds.assign(effectNames.size(), 0.0f);
        timingValid.assign(effectNames.size(), false);
        timingSamplesPending.assign(imageCount, false);
        timingEnabled = true;
        timingReadErrorLogged = false;
        Logger::debug("per-effect GPU timing enabled for "
                      + std::to_string(effectNames.size()) + " effect(s)");
        return true;
    }

    void LogicalSwapchain::destroyEffectTimings()
    {
        if (timingQueryPool != VK_NULL_HANDLE)
        {
            pLogicalDevice->vkd.DestroyQueryPool(pLogicalDevice->device, timingQueryPool, nullptr);
            timingQueryPool = VK_NULL_HANDLE;
        }
        timingQueryStride = 0;
        timingEffectNames.clear();
        timingMilliseconds.clear();
        timingValid.clear();
        timingSpanMilliseconds.reset();
        timingSamplesPending.clear();
        timingEnabled = false;
        timingReadErrorLogged = false;
    }

    void LogicalSwapchain::collectEffectTimings(uint32_t imageIndex)
    {
        if (!timingEnabled || timingQueryPool == VK_NULL_HANDLE
            || imageIndex >= timingSamplesPending.size()
            || !timingSamplesPending[imageIndex])
            return;

        timingSamplesPending[imageIndex] = false;
        const uint32_t queryBase = effectTimingQueryBase(
            imageIndex, static_cast<uint32_t>(timingEffectNames.size()));
        std::vector<uint64_t> timestamps(timingQueryStride, 0);
        const VkResult result = pLogicalDevice->vkd.GetQueryPoolResults(
            pLogicalDevice->device, timingQueryPool, queryBase,
            timingQueryStride, timestamps.size() * sizeof(uint64_t),
            timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

        switch (effectTimingReadAction(result))
        {
            case EffectTimingReadAction::Skip:
                return;
            case EffectTimingReadAction::Disable:
                timingEnabled = false;
                std::fill(timingSamplesPending.begin(), timingSamplesPending.end(), false);
                if (!timingReadErrorLogged)
                {
                    Logger::warn("per-effect GPU timing disabled after timestamp read failure: "
                                 + std::to_string(result));
                    timingReadErrorLogged = true;
                }
                return;
            case EffectTimingReadAction::Accept:
                break;
        }

        for (size_t effectIndex = 0; effectIndex < timingEffectNames.size(); ++effectIndex)
        {
            const bool firstSample = !timingValid[effectIndex];
            const float sample = effectTimestampMilliseconds(
                timestamps[effectIndex * 2], timestamps[effectIndex * 2 + 1],
                pLogicalDevice->timestampValidBits,
                pLogicalDevice->timestampPeriodNanoseconds);
            timingMilliseconds[effectIndex] = smoothEffectTiming(
                timingMilliseconds[effectIndex], sample, timingValid[effectIndex]);
            timingValid[effectIndex] = true;
            if (firstSample)
            {
                Logger::debug("first per-effect GPU timing sample for "
                              + timingEffectNames[effectIndex] + ": "
                              + std::to_string(sample) + " ms");
            }
        }
        if (!timingEffectNames.empty() && timestamps.size() >= timingEffectNames.size() * 2)
        {
            const float span = effectTimestampMilliseconds(timestamps.front(), timestamps[timingEffectNames.size() * 2 - 1],
                                                           pLogicalDevice->timestampValidBits, pLogicalDevice->timestampPeriodNanoseconds);
            timingSpanMilliseconds = smoothEffectTiming(timingSpanMilliseconds.value_or(0.0f), span, timingSpanMilliseconds.has_value());
        }
    }

    void LogicalSwapchain::markTimingSubmission(uint32_t imageIndex, bool effectCommandsSubmitted)
    {
        if (imageIndex < timingSamplesPending.size())
            timingSamplesPending[imageIndex] =
                timingEnabled && effectCommandsSubmitted && timingQueryPool != VK_NULL_HANDLE;
    }

    void LogicalSwapchain::freeCommandBuffers(std::vector<VkCommandBuffer>& commandBuffers)
    {
        if (!commandBuffers.empty())
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, commandBuffers.size(), commandBuffers.data());
        commandBuffers.clear();
    }

    void LogicalSwapchain::waitForEffects()
    {
        std::vector<VkFence> fences;
        std::vector<VkQueue> fenceless;
        for (size_t i = 0; i < effectFences.size(); i++)
        {
            if (effectFences[i] != VK_NULL_HANDLE)
                fences.push_back(effectFences[i]);
            if (i < fencelessEffectQueues.size() && fencelessEffectQueues[i] != VK_NULL_HANDLE)
                fenceless.push_back(fencelessEffectQueues[i]);
        }
        waitForOurFences(pLogicalDevice->vkd.WaitForFences, pLogicalDevice->device, fences, 2'000'000'000ull);
        drainQueues(pLogicalDevice->vkd.QueueWaitIdle, std::move(fenceless));
    }

    void LogicalSwapchain::destroy()
    {
        if (imageCount > 0)
        {
            waitForEffects();
            waitForOurFences(pLogicalDevice->vkd.WaitForFences, pLogicalDevice->device, overlay.fences(), 2'000'000'000ull);
            if (pLogicalDevice->imguiOverlay)
                pLogicalDevice->imguiOverlay->detach(overlay);
            overlay.destroy(*pLogicalDevice);

            if (chain)
                chain->destroy(*pLogicalDevice);
            chain.reset();
            effects.clear();
            defaultTransfer.reset();

            freeCommandBuffers(commandBuffersEffect);
            freeCommandBuffers(commandBuffersNoEffect);
            destroyEffectTimings();

            for (VkImage image : fakeImages)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
            fakeImages.clear();

            for (VkDeviceMemory memory : fakeImageMemory)
                freeTrackedMemory(pLogicalDevice, memory, nullptr);
            fakeImageMemory.clear();

            for (VkFence fence : effectFences)
                pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, fence, nullptr);
            effectFences.clear();
            fencelessEffectQueues.clear();
            for (std::vector<VkSemaphore>* ring : {&semaphores, &overlaySemaphores})
            {
                for (VkSemaphore semaphore : *ring)
                    pLogicalDevice->vkd.DestroySemaphore(pLogicalDevice->device, semaphore, nullptr);
                ring->clear();
            }

            for (VkImageView view : overlayImageViews)
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, view, nullptr);
            overlayImageViews.clear();
        }
    }
}
