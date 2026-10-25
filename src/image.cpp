#include "image.hpp"
#include "memory.hpp"
#include "buffer.hpp"
#include "format.hpp"

#include <algorithm>

namespace vkBasalt
{
    static VkResult submitAndWait(LogicalDevice* pLogicalDevice, VkCommandBuffer commandBuffer)
    {
        VkSubmitInfo submitInfo       = {};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffer;

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        VkFence fence = VK_NULL_HANDLE;
        if (pLogicalDevice->vkd.CreateFence(pLogicalDevice->device, &fenceInfo, nullptr, &fence) != VK_SUCCESS)
            fence = VK_NULL_HANDLE;

        const VkQueue  queue     = backgroundQueue(*pLogicalDevice);
        const VkResult submitted = pLogicalDevice->vkd.QueueSubmit(queue, 1, &submitInfo, fence);
        const VkResult waited    = submitted != VK_SUCCESS ? submitted
                                   : fence != VK_NULL_HANDLE
                                       ? waitForOurFences(pLogicalDevice->vkd.WaitForFences, pLogicalDevice->device, {fence}, UINT64_MAX)
                                       : drainQueues(pLogicalDevice->vkd.QueueWaitIdle, {queue});

        if (fence != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, fence, nullptr);
        return waited;
    }

    template<typename Record>
    static VkResult recordAndSubmitOnce(LogicalDevice* pLogicalDevice, Record record)
    {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = pLogicalDevice->commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkResult        result        = allocateDispatchableCommandBuffers(pLogicalDevice, allocInfo, &commandBuffer);
        if (result != VK_SUCCESS)
            return result;

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = pLogicalDevice->vkd.BeginCommandBuffer(commandBuffer, &beginInfo);
        if (result == VK_SUCCESS)
        {
            record(commandBuffer);
            result = pLogicalDevice->vkd.EndCommandBuffer(commandBuffer);
        }
        if (result == VK_SUCCESS)
            result = submitAndWait(pLogicalDevice, commandBuffer);

        pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device, pLogicalDevice->commandPool, 1, &commandBuffer);
        return result;
    }

    std::vector<VkImage> createImages(LogicalDevice*        pLogicalDevice,
                                      uint32_t              count,
                                      VkExtent3D            extent,
                                      VkFormat              format,
                                      VkImageUsageFlags     usage,
                                      VkMemoryPropertyFlags properties,
                                      VkDeviceMemory&       imageMemory,
                                      uint32_t              mipLevels,
                                      VkImageType           imageType)
    {
        std::vector<VkImage> images(count);

        const VkFormat                    formats[]     = {convertToUNORM(format), convertToSRGB(format)};
        const VkImageFormatListCreateInfo formatList    = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, nullptr, 2, formats};
        const bool                        mutableFormat = formats[0] != formats[1];

        VkImageCreateInfo imageCreateInfo;
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.pNext = mutableFormat ? &formatList : nullptr;
        imageCreateInfo.flags = mutableFormat ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
        imageCreateInfo.imageType             = imageType;
        imageCreateInfo.format                = format;
        imageCreateInfo.extent                = extent;
        imageCreateInfo.mipLevels             = mipLevels;
        imageCreateInfo.arrayLayers           = 1;
        imageCreateInfo.samples               = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling                = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage                 = usage;
        imageCreateInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.queueFamilyIndexCount = 0;
        imageCreateInfo.pQueueFamilyIndices   = nullptr;
        imageCreateInfo.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

        imageMemory = VK_NULL_HANDLE;
        const auto releaseAndThrow = [&](VkResult failed) {
            for (VkImage image : images)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
            if (imageMemory != VK_NULL_HANDLE)
                freeTrackedMemory(pLogicalDevice, imageMemory, nullptr);
            imageMemory = VK_NULL_HANDLE;
            ASSERT_VULKAN(failed);
        };

        VkResult result;
        for (uint32_t i = 0; i < count; i++)
        {
            result = pLogicalDevice->vkd.CreateImage(pLogicalDevice->device, &imageCreateInfo, nullptr, &(images[i]));
            if (result != VK_SUCCESS)
            {
                images[i] = VK_NULL_HANDLE;
                releaseAndThrow(result);
            }
        }
        VkMemoryRequirements memoryRequirements;
        pLogicalDevice->vkd.GetImageMemoryRequirements(pLogicalDevice->device, images[0], &memoryRequirements);

        if (memoryRequirements.size % memoryRequirements.alignment != 0)
        {
            memoryRequirements.size = (memoryRequirements.size / memoryRequirements.alignment + 1) * memoryRequirements.alignment;
        }

        VkMemoryAllocateInfo memoryAllocateInfo;
        memoryAllocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext           = nullptr;
        memoryAllocateInfo.allocationSize  = memoryRequirements.size * count;
        memoryAllocateInfo.memoryTypeIndex = findMemoryTypeIndex(pLogicalDevice, memoryRequirements.memoryTypeBits, properties);

        result = allocateTrackedMemory(pLogicalDevice, &memoryAllocateInfo, nullptr, &imageMemory);
        if (result != VK_SUCCESS)
        {
            imageMemory = VK_NULL_HANDLE;
            releaseAndThrow(result);
        }

        for (uint32_t i = 0; i < count; i++)
        {
            result = pLogicalDevice->vkd.BindImageMemory(pLogicalDevice->device, images[i], imageMemory, memoryRequirements.size * i);
            if (result != VK_SUCCESS)
                releaseAndThrow(result);
        }
        return images;
    }

    void
    uploadToImage(LogicalDevice* pLogicalDevice, VkImage image, VkExtent3D extent, uint32_t size, const unsigned char* writeData, uint32_t mipLevels)
    {
        VkBuffer       stagingBuffer;
        VkDeviceMemory stagingMemory;

        createBuffer(pLogicalDevice,
                     size,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer,
                     stagingMemory);
        void*    data;
        VkResult result = pLogicalDevice->vkd.MapMemory(pLogicalDevice->device, stagingMemory, 0, size, 0, &data);
        if (result == VK_SUCCESS)
        {
            std::memcpy(data, writeData, size);
            pLogicalDevice->vkd.UnmapMemory(pLogicalDevice->device, stagingMemory);
            result = recordAndSubmitOnce(pLogicalDevice, [&](VkCommandBuffer commandBuffer) {
                VkImageMemoryBarrier memoryBarrier;
                memoryBarrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                memoryBarrier.pNext                           = nullptr;
                memoryBarrier.srcAccessMask                   = 0;
                memoryBarrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
                memoryBarrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
                memoryBarrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                memoryBarrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                memoryBarrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                memoryBarrier.image                           = image;
                memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                memoryBarrier.subresourceRange.baseMipLevel   = 0;
                memoryBarrier.subresourceRange.levelCount     = 1;
                memoryBarrier.subresourceRange.baseArrayLayer = 0;
                memoryBarrier.subresourceRange.layerCount     = 1;

                pLogicalDevice->vkd.CmdPipelineBarrier(
                    commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

                VkBufferImageCopy region;
                region.bufferOffset                    = 0;
                region.bufferRowLength                 = 0;
                region.bufferImageHeight               = 0;
                region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel       = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount     = 1;
                region.imageOffset                     = {0, 0, 0};
                region.imageExtent                     = extent;

                pLogicalDevice->vkd.CmdCopyBufferToImage(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                pLogicalDevice->vkd.CmdPipelineBarrier(
                    commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

                generateMipMaps(pLogicalDevice, commandBuffer, image, extent, mipLevels, VK_FILTER_LINEAR);
            });
        }

        freeTrackedMemory(pLogicalDevice, stagingMemory, nullptr);
        pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, stagingBuffer, nullptr);
        ASSERT_VULKAN(result);
    }

    void clearAndReadyImages(LogicalDevice* pLogicalDevice, std::vector<VkImage> images, uint32_t mipLevels)
    {
        ASSERT_VULKAN(recordAndSubmitOnce(pLogicalDevice, [&](VkCommandBuffer commandBuffer) {
            VkImageMemoryBarrier memoryBarrier;
            memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            memoryBarrier.pNext               = nullptr;
            memoryBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            memoryBarrier.srcAccessMask       = 0;
            memoryBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            memoryBarrier.subresourceRange.baseMipLevel   = 0;
            memoryBarrier.subresourceRange.levelCount     = mipLevels;
            memoryBarrier.subresourceRange.baseArrayLayer = 0;
            memoryBarrier.subresourceRange.layerCount     = 1;

            for (auto& image : images)
            {
                memoryBarrier.image = image;
                pLogicalDevice->vkd.CmdPipelineBarrier(
                    commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
            }

            const VkClearColorValue zero = {};
            for (auto& image : images)
            {
                pLogicalDevice->vkd.CmdClearColorImage(
                    commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &memoryBarrier.subresourceRange);
            }

            memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            for (auto& image : images)
            {
                memoryBarrier.image = image;
                pLogicalDevice->vkd.CmdPipelineBarrier(
                    commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
            }
        }));
    }

    void generateMipMaps(
        LogicalDevice* pLogicalDevice, VkCommandBuffer commandBuffer, VkImage image, VkExtent3D extent, uint32_t mipLevels, VkFilter filter)
    {
        if (mipLevels < 2)
            return;

        constexpr VkPipelineStageFlags readers = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        VkImageMemoryBarrier barrier            = {};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        const auto transition = [&](uint32_t             level,
                                    uint32_t             levels,
                                    VkImageLayout        from,
                                    VkImageLayout        to,
                                    VkAccessFlags        srcAccess,
                                    VkAccessFlags        dstAccess,
                                    VkPipelineStageFlags srcStage,
                                    VkPipelineStageFlags dstStage) {
            barrier.subresourceRange.baseMipLevel = level;
            barrier.subresourceRange.levelCount   = levels;
            barrier.oldLayout                     = from;
            barrier.newLayout                     = to;
            barrier.srcAccessMask                 = srcAccess;
            barrier.dstAccessMask                 = dstAccess;
            pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        };

        transition(0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT,
                   VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        transition(1, mipLevels - 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, readers,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);

        int32_t width  = static_cast<int32_t>(extent.width);
        int32_t height = static_cast<int32_t>(extent.height);
        int32_t depth  = static_cast<int32_t>(extent.depth);
        for (uint32_t i = 1; i < mipLevels; i++)
        {
            VkImageBlit blit                   = {};
            blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel       = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = 1;
            blit.srcOffsets[1]                 = {width, height, depth};

            width  = std::max(width / 2, 1);
            height = std::max(height / 2, 1);
            depth  = std::max(depth / 2, 1);

            blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel       = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount     = 1;
            blit.dstOffsets[1]                 = {width, height, depth};

            pLogicalDevice->vkd.CmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter);

            transition(i, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        }

        transition(0, mipLevels, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, readers);
    }
}
