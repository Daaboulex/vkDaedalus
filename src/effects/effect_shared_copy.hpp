#ifndef EFFECT_SHARED_COPY_HPP_INCLUDED
#define EFFECT_SHARED_COPY_HPP_INCLUDED

#include <vector>

#include "effect.hpp"
#include "logical_device.hpp"

namespace vkBasalt
{
    enum class SharedCopyDirection
    {
        ImageToBuffer,
        BufferToImage
    };

    class SharedCopyEffect : public Effect
    {
    public:
        SharedCopyEffect(LogicalDevice*        pLogicalDevice,
                         VkExtent2D            imageExtent,
                         std::vector<VkImage>  images,
                         std::vector<VkBuffer> buffers,
                         SharedCopyDirection   direction);
        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;

    private:
        LogicalDevice*        pLogicalDevice;
        VkExtent2D            imageExtent;
        std::vector<VkImage>  images;
        std::vector<VkBuffer> buffers;
        SharedCopyDirection   direction;
    };

    class TimestampEffect : public Effect
    {
    public:
        TimestampEffect(LogicalDevice* pLogicalDevice, VkQueryPool pool, uint32_t queryOffset, VkPipelineStageFlagBits stage);
        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;

    private:
        LogicalDevice*          pLogicalDevice;
        VkQueryPool             pool;
        uint32_t                queryOffset;
        VkPipelineStageFlagBits stage;
    };
}

#endif
