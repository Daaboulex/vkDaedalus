#include "gpu_chain.hpp"

#include <unistd.h>

namespace vkBasalt
{
    namespace
    {
        bool importSyncFd(HandOffSide side, VkSemaphore semaphore, int fd, HandOff& handOff, const std::string& what)
        {
            VkImportSemaphoreFdInfoKHR import{};
            import.sType      = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
            import.semaphore  = semaphore;
            import.flags      = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
            import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
            import.fd         = fd;
            const VkResult vr = side.vkd->ImportSemaphoreFdKHR(side.device, &import);
            if (vr == VK_SUCCESS)
                return true;
            if (fd >= 0)
                close(fd);
            handOff.failure = what + " returned " + std::to_string(vr);
            return false;
        }

        std::string where(uint32_t gpu)
        {
            return gpu > 1 ? "GPU " + std::to_string(gpu) : "the game's GPU";
        }
    }

    HandOff submitThroughChain(HandOffSide                     game,
                               const std::vector<HandOffSide>& segmentSides,
                               GpuChain&                       chain,
                               uint32_t                        index,
                               VkSubmitInfo                    head,
                               VkSemaphore                     presentSignal,
                               VkFence                         effectFence)
    {
        HandOff handOff;
        head.signalSemaphoreCount = 1;
        head.pSignalSemaphores    = &chain.headDone[index];
        VkResult vr               = game.vkd->QueueSubmit(game.queue, 1, &head, VK_NULL_HANDLE);
        if (vr != VK_SUCCESS)
        {
            handOff.failure = "the head's submit on the game's GPU returned " + std::to_string(vr);
            return handOff;
        }
        handOff.headRan = true;

        HandOffSide previous     = game;
        VkSemaphore previousDone = chain.headDone[index];
        uint32_t    previousGpu  = 1;
        for (size_t k = 0; k < chain.segments.size(); k++)
        {
            ChainSegment&      segment = chain.segments[k];
            const HandOffSide& side    = segmentSides[k];
            const bool         last    = k + 1 == chain.segments.size();

            VkSemaphoreGetFdInfoKHR get{};
            get.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
            get.semaphore  = previousDone;
            get.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
            int fd         = -1;
            vr             = previous.vkd->GetSemaphoreFdKHR(previous.device, &get, &fd);
            if (vr != VK_SUCCESS)
            {
                if (k == 0)
                    handOff.fallbackWait = chain.headDone[index];
                handOff.failure = "exporting the hand-off from " + where(previousGpu) + " returned " + std::to_string(vr);
                return handOff;
            }
            if (!importSyncFd(side, segment.start[index], fd, handOff, "importing the hand-off on " + where(segment.gpu)))
                return handOff;

            const bool fenced = !segment.fences.empty();
            if (fenced)
            {
                vr = side.vkd->ResetFences(side.device, 1, &segment.fences[index]);
                if (vr != VK_SUCCESS)
                {
                    handOff.failure = "resetting " + where(segment.gpu) + "'s fence returned " + std::to_string(vr);
                    return handOff;
                }
            }
            const VkPipelineStageFlags anyStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkSubmitInfo               onSegment{};
            onSegment.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            onSegment.waitSemaphoreCount   = 1;
            onSegment.pWaitSemaphores      = &segment.start[index];
            onSegment.pWaitDstStageMask    = &anyStage;
            onSegment.commandBufferCount   = 1;
            onSegment.pCommandBuffers      = &segment.commandBuffers[index];
            onSegment.signalSemaphoreCount = 1;
            onSegment.pSignalSemaphores    = last ? &presentSignal : &segment.done[index];
            const VkFence fence            = last ? effectFence : fenced ? segment.fences[index] : VK_NULL_HANDLE;
            vr                             = side.vkd->QueueSubmit(side.queue, 1, &onSegment, fence);
            if (vr != VK_SUCCESS)
            {
                if (fenced)
                {
                    VkFenceCreateInfo signalled{};
                    signalled.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                    signalled.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                    side.vkd->DestroyFence(side.device, segment.fences[index], nullptr);
                    if (side.vkd->CreateFence(side.device, &signalled, nullptr, &segment.fences[index]) != VK_SUCCESS)
                        segment.fences[index] = VK_NULL_HANDLE;
                }
                handOff.failure = "the submit on " + where(segment.gpu) + " returned " + std::to_string(vr);
                return handOff;
            }
            previous     = side;
            previousDone = segment.done[index];
            previousGpu  = segment.gpu;
        }
        handOff.handedOver = true;
        return handOff;
    }
}
