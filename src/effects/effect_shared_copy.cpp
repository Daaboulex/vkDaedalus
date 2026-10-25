#include "effect_shared_copy.hpp"

namespace vkBasalt
{
    namespace
    {
        VkBufferMemoryBarrier ownership(VkBuffer buffer, uint32_t from, uint32_t to, VkAccessFlags src, VkAccessFlags dst)
        {
            VkBufferMemoryBarrier barrier{};
            barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask       = src;
            barrier.dstAccessMask       = dst;
            barrier.srcQueueFamilyIndex = from;
            barrier.dstQueueFamilyIndex = to;
            barrier.buffer              = buffer;
            barrier.offset              = 0;
            barrier.size                = VK_WHOLE_SIZE;
            return barrier;
        }

        VkImageMemoryBarrier layout(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask               = src;
            barrier.dstAccessMask               = dst;
            barrier.oldLayout                   = from;
            barrier.newLayout                   = to;
            barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                       = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            return barrier;
        }
    }

    SharedCopyEffect::SharedCopyEffect(LogicalDevice*        pLogicalDevice,
                                       VkExtent2D            imageExtent,
                                       std::vector<VkImage>  images,
                                       std::vector<VkBuffer> buffers,
                                       SharedCopyDirection   direction)
        : pLogicalDevice(pLogicalDevice), imageExtent(imageExtent), images(std::move(images)), buffers(std::move(buffers)), direction(direction)
    {
    }

    void SharedCopyEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        const DeviceDispatch& vkd     = pLogicalDevice->vkd;
        const uint32_t        own     = pLogicalDevice->queueFamilyIndex;
        const uint32_t        foreign = VK_QUEUE_FAMILY_FOREIGN_EXT;
        const VkImage         image   = images[imageIndex];
        const VkBuffer        buffer  = buffers[imageIndex];

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent                 = {imageExtent.width, imageExtent.height, 1};

        if (direction == SharedCopyDirection::ImageToBuffer)
        {
            const VkBufferMemoryBarrier acquire = ownership(buffer, foreign, own, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
            const VkImageMemoryBarrier  toCopy  = layout(image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                                         VK_ACCESS_TRANSFER_READ_BIT);
            vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 1, &toCopy);
            vkd.CmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
            const VkBufferMemoryBarrier release = ownership(buffer, own, foreign, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
            const VkImageMemoryBarrier  back    = layout(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 0);
            vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 1, &release, 1, &back);
            return;
        }

        const VkBufferMemoryBarrier acquire = ownership(buffer, foreign, own, 0, VK_ACCESS_TRANSFER_READ_BIT);
        const VkImageMemoryBarrier  toCopy  = layout(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 1, &toCopy);
        vkd.CmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        const VkBufferMemoryBarrier release = ownership(buffer, own, foreign, 0, 0);
        const VkImageMemoryBarrier  back =
            layout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT);
        vkd.CmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &release, 1, &back);
    }

    TimestampEffect::TimestampEffect(LogicalDevice* pLogicalDevice, VkQueryPool pool, uint32_t queryOffset, VkPipelineStageFlagBits stage)
        : pLogicalDevice(pLogicalDevice), pool(pool), queryOffset(queryOffset), stage(stage)
    {
    }

    void TimestampEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        const uint32_t query = imageIndex * 2 + queryOffset;
        pLogicalDevice->vkd.CmdResetQueryPool(commandBuffer, pool, query, 1);
        pLogicalDevice->vkd.CmdWriteTimestamp(commandBuffer, stage, pool, query);
    }
}
